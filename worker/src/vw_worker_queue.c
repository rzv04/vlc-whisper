#include "vw_worker_queue.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "vw_protocol_codec.h"

// ponytail: mutex, not lock-free as originally planned. "Drop oldest AUDIO from a full ring" requires
// evicting a slot mid-queue, which a lock-free SPSC ring cannot do safely (consumer may be reading
// it); the worker has no realtime constraint (unlike the plugin's VLC callback, Rule 4), so a mutex
// is the simplest correct design. Revisit only if the worker ever gains a hard realtime budget.

struct vw_worker_queue {
  vw_worker_frame_t* slots;  // ring buffer of `capacity` slots
  size_t capacity;
  size_t head;  // next write index (unbounded; slot index = head % capacity)
  size_t tail;  // next read index (unbounded; slot index = tail % capacity)
  pthread_mutex_t mutex;
  _Atomic uint64_t dropped_audio_us;
};

vw_worker_queue_t* vw_worker_queue_create(size_t capacity) {
  if (capacity == 0) {
    return NULL;
  }
  vw_worker_queue_t* q = (vw_worker_queue_t*)calloc(1, sizeof(vw_worker_queue_t));
  if (!q) {
    return NULL;
  }
  q->slots = (vw_worker_frame_t*)calloc(capacity, sizeof(vw_worker_frame_t));
  if (!q->slots) {
    free(q);
    return NULL;
  }
  q->capacity = capacity;
  if (pthread_mutex_init(&q->mutex, NULL) != 0) {
    free(q->slots);
    free(q);
    return NULL;
  }
  return q;
}

void vw_worker_queue_destroy(vw_worker_queue_t* q) {
  if (!q) {
    return;
  }
  // Caller guarantees quiescence: no concurrent push/pop while destroying.
  for (size_t i = q->tail; i < q->head; i++) {
    if (q->slots[i % q->capacity].payload) {
      free(q->slots[i % q->capacity].payload);
    }
  }
  pthread_mutex_destroy(&q->mutex);
  free(q->slots);
  free(q);
}

// Returns the duration of an audio frame in microseconds, or 0 if the payload is invalid or not an audio frame.
static uint64_t vw_worker_queue_audio_duration_us(const uint8_t* payload, uint32_t payload_len) {
  vw_msg_audio_t audio;
  if (!payload || !vw_protocol_decode_payload(VW_MSG_AUDIO_PCM, payload, payload_len, &audio)) {
    return 0;  // undecodable payload: account zero duration, still evict
  }
  return audio.duration_us > 0 ? (uint64_t)audio.duration_us : 0;
}

bool vw_worker_queue_push(vw_worker_queue_t* q, uint16_t type, uint8_t* payload, uint32_t payload_len) {
  if (!q) {
    free(payload);
    return false;
  }
  pthread_mutex_lock(&q->mutex);

  // Fast path: room available.
  if (q->head - q->tail < q->capacity) {
    size_t idx = q->head % q->capacity;
    q->slots[idx].type = type;
    q->slots[idx].payload_len = payload_len;
    q->slots[idx].payload = payload;
    q->head++;
    pthread_mutex_unlock(&q->mutex);
    return true;
  }

  // Full: evict the oldest AUDIO frame so the incoming frame (possibly a control frame) fits.
  // Control frames are never evicted; the queue is full of audio in practice, so this finds a slot.
  size_t evict = q->capacity;  // sentinel: not found
  for (size_t i = q->tail; i < q->head; i++) {
    if (q->slots[i % q->capacity].type == VW_MSG_AUDIO_PCM) {
      evict = i;
      break;
    }
  }
  if (evict != q->capacity) {
    vw_worker_frame_t* victim = &q->slots[evict % q->capacity];
    atomic_fetch_add_explicit(&q->dropped_audio_us,
                              vw_worker_queue_audio_duration_us(victim->payload, victim->payload_len),
                              memory_order_relaxed);
    free(victim->payload);
    // Shift everything after the evicted slot one position left, keeping FIFO order of survivors.
    for (size_t i = evict; i + 1 < q->head; i++) {
      q->slots[i % q->capacity] = q->slots[(i + 1) % q->capacity];
    }
    q->head--;
    size_t idx = q->head % q->capacity;
    q->slots[idx].type = type;
    q->slots[idx].payload_len = payload_len;
    q->slots[idx].payload = payload;
    q->head++;
    pthread_mutex_unlock(&q->mutex);
    return true;
  }

  // Full with no evictable AUDIO frame (all-control queue). An incoming AUDIO is dropped (counted)
  // — never sacrifice a control for audio. An incoming CONTROL evicts the OLDEST control so the
  // newest one always lands, but a queued SHUTDOWN is never evicted by a non-terminal control:
  // only a newer SHUTDOWN supersedes an older one. Reachable only in a pathological burst of
  // controls; the main loop pops controls immediately.
  if (type == VW_MSG_AUDIO_PCM) {
    atomic_fetch_add_explicit(&q->dropped_audio_us, vw_worker_queue_audio_duration_us(payload, payload_len),
                              memory_order_relaxed);
    free(payload);
    pthread_mutex_unlock(&q->mutex);
    return false;
  }
  // Evict the oldest control — skipping SHUTDOWN when the incoming frame is not itself a SHUTDOWN
  // (dropping the only shutdown the worker will see would leave it running until the pipe breaks).
  // If every queued control is SHUTDOWN, evicting one is harmless: the rest still deliver it.
  size_t evict_ctrl = q->tail;
  if (type != VW_MSG_SHUTDOWN) {
    for (size_t i = q->tail; i < q->head; i++) {
      if (q->slots[i % q->capacity].type != VW_MSG_SHUTDOWN) {
        evict_ctrl = i;
        break;
      }
    }
  }
  vw_worker_frame_t* victim = &q->slots[evict_ctrl % q->capacity];
  atomic_fetch_add_explicit(&q->dropped_audio_us,
                            vw_worker_queue_audio_duration_us(victim->payload, victim->payload_len),
                            memory_order_relaxed);
  free(victim->payload);
  for (size_t i = evict_ctrl; i + 1 < q->head; i++) {
    q->slots[i % q->capacity] = q->slots[(i + 1) % q->capacity];
  }
  q->head--;
  size_t idx = q->head % q->capacity;
  q->slots[idx].type = type;
  q->slots[idx].payload_len = payload_len;
  q->slots[idx].payload = payload;
  q->head++;
  pthread_mutex_unlock(&q->mutex);
  return true;
}

bool vw_worker_queue_pop(vw_worker_queue_t* q, vw_worker_frame_t* out) {
  if (!q || !out) {
    return false;
  }
  pthread_mutex_lock(&q->mutex);
  if (q->head == q->tail) {
    pthread_mutex_unlock(&q->mutex);
    return false;
  }
  *out = q->slots[q->tail % q->capacity];
  q->tail++;
  pthread_mutex_unlock(&q->mutex);
  return true;
}

uint64_t vw_worker_queue_get_dropped_audio_us(const vw_worker_queue_t* q) {
  if (!q) {
    return 0;
  }
  return atomic_load_explicit(&q->dropped_audio_us, memory_order_relaxed);
}
