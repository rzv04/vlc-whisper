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
  size_t evict = 0;
  bool evict_found = false;
  for (size_t i = q->tail; i < q->head; i++) {
    if (q->slots[i % q->capacity].type == VW_MSG_AUDIO_PCM) {
      evict = i;
      evict_found = true;
      break;
    }
  }
  if (evict_found) {
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
  // — never sacrifice a control for audio. An incoming CONTROL evicts only a control the incoming
  // supersedes or the worker never needs: PAUSE/RESUME (stateless), a same-type control, or — for
  // SHUTDOWN — anything. A required incoming (START/STOP) additionally supersedes the oldest
  // non-SHUTDOWN control; a queued SHUTDOWN is never evicted by a non-SHUTDOWN incoming. Only a
  // soft incoming (PAUSE/RESUME) can be dropped; required incomings always land. Reachable only in
  // a pathological burst of controls; the main loop pops controls immediately.
  if (type == VW_MSG_AUDIO_PCM) {
    atomic_fetch_add_explicit(&q->dropped_audio_us, vw_worker_queue_audio_duration_us(payload, payload_len),
                              memory_order_relaxed);
    free(payload);
    pthread_mutex_unlock(&q->mutex);
    return false;
  }
  // Incoming SHUTDOWN supersedes every queued control — evict the oldest, whatever it is (a newer
  // SHUTDOWN replaces an older one, so at least one SHUTDOWN always survives). Any other incoming
  // control evicts only a control it supersedes or the worker never needs: oldest PAUSE/RESUME
  // (stateless no-ops in the worker loop), oldest same-type, or — for a required incoming
  // (START/STOP) — the oldest non-SHUTDOWN control, since the newest session directive supersedes
  // the oldest. A queued SHUTDOWN is never evicted by a non-SHUTDOWN incoming. A required incoming
  // is dropped only when every queued control is SHUTDOWN (the worker is exiting anyway, so the
  size_t evict_ctrl = 0;
  bool evict_ctrl_found = false;
  if (type == VW_MSG_SHUTDOWN) {
    evict_ctrl = q->tail;
    evict_ctrl_found = true;
  } else {
    bool required = (type == VW_MSG_START_SESSION || type == VW_MSG_STOP_SESSION);
    for (size_t i = q->tail; i < q->head; i++) {
      uint16_t queued = q->slots[i % q->capacity].type;
      if (queued == VW_MSG_PAUSE || queued == VW_MSG_RESUME || queued == type) {
        evict_ctrl = i;
        evict_ctrl_found = true;
        break;
      }
    }
    if (!evict_ctrl_found && required) {
      // Newest required directive supersedes the oldest non-SHUTDOWN one; never a queued SHUTDOWN.
      for (size_t i = q->tail; i < q->head; i++) {
        if (q->slots[i % q->capacity].type != VW_MSG_SHUTDOWN) {
          evict_ctrl = i;
          evict_ctrl_found = true;
          break;
        }
      }
    }
  }
  if (!evict_ctrl_found) {
    // Reachable only when nothing evictable exists: a soft incoming with no soft/same-type queued
    // (dropping PAUSE/RESUME is harmless), or a required incoming into an all-SHUTDOWN queue (the
    // worker is exiting, so the directive is moot). Never sacrifice a queued required transition.
    free(payload);
    pthread_mutex_unlock(&q->mutex);
    return false;
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
