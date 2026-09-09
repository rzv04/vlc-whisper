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

static bool vw_worker_queue_is_lifecycle_control(uint16_t type) {
  return type == VW_MSG_START_SESSION || type == VW_MSG_STOP_SESSION || type == VW_MSG_PAUSE || type == VW_MSG_RESUME ||
         type == VW_MSG_SHUTDOWN;
}

static bool vw_worker_queue_frame_session_id(const vw_worker_frame_t* frame, vw_session_id_t* out_session_id) {
  if (!frame || !out_session_id || !frame->payload) return false;
  if (frame->type == VW_MSG_AUDIO_PCM) {
    vw_msg_audio_t audio;
    if (!vw_protocol_decode_payload(VW_MSG_AUDIO_PCM, frame->payload, frame->payload_len, &audio)) return false;
    *out_session_id = audio.session_id;
    return true;
  }
  if (frame->type == VW_MSG_POSITION) {
    vw_msg_position_t position;
    if (!vw_protocol_decode_payload(VW_MSG_POSITION, frame->payload, frame->payload_len, &position)) return false;
    *out_session_id = position.session_id;
    return true;
  }
  return false;
}

static bool vw_worker_queue_control_applies_to_preceding_epoch(const vw_worker_queue_t* q, size_t lifecycle) {
  const vw_worker_frame_t* control = &q->slots[lifecycle % q->capacity];
  if (control->type == VW_MSG_SHUTDOWN) return true;

  size_t preceding_epoch = q->head;
  for (size_t i = q->tail; i < lifecycle; i++) {
    uint16_t type = q->slots[i % q->capacity].type;
    if (type == VW_MSG_AUDIO_PCM || type == VW_MSG_POSITION) preceding_epoch = i;
  }
  if (preceding_epoch == q->head) return true;

  vw_session_id_t preceding_session;
  const vw_worker_frame_t* preceding_frame = &q->slots[preceding_epoch % q->capacity];
  if (!vw_worker_queue_frame_session_id(preceding_frame, &preceding_session)) return false;

  if (control->type == VW_MSG_START_SESSION) {
    vw_msg_start_t start;
    if (!control->payload ||
        !vw_protocol_decode_payload(VW_MSG_START_SESSION, control->payload, control->payload_len, &start)) {
      return false;
    }
    // A START for the same epoch is a duplicate and must stay FIFO. A different session replaces the epoch, so
    // preceding AUDIO/POSITION state is obsolete and may be discarded before promoting the START.
    return memcmp(start.session_id.bytes, preceding_session.bytes, VW_SESSION_ID_BYTES) != 0;
  }

  vw_msg_control_t lifecycle_control;
  if (!control->payload ||
      !vw_protocol_decode_payload(control->type, control->payload, control->payload_len, &lifecycle_control)) {
    return false;
  }
  return memcmp(lifecycle_control.session_id.bytes, preceding_session.bytes, VW_SESSION_ID_BYTES) == 0;
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
  // directive is moot).
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
  memset(&q->slots[q->tail % q->capacity], 0, sizeof(q->slots[q->tail % q->capacity]));
  q->tail++;
  pthread_mutex_unlock(&q->mutex);
  return true;
}

bool vw_worker_queue_pop_prioritized(vw_worker_queue_t* q, vw_worker_frame_t* out) {
  if (!q || !out) {
    return false;
  }
  pthread_mutex_lock(&q->mutex);
  if (q->head == q->tail) {
    pthread_mutex_unlock(&q->mutex);
    return false;
  }

  // Preserve HELLO ordering for authentication. After that, promote only a lifecycle control consistent with the
  // nearest preceding AUDIO/POSITION epoch; invalid or wrong-session controls remain FIFO for worker validation.
  if (q->slots[q->tail % q->capacity].type != VW_MSG_HELLO) {
    size_t lifecycle = q->head;
    for (size_t i = q->tail; i < q->head; i++) {
      if (vw_worker_queue_is_lifecycle_control(q->slots[i % q->capacity].type) &&
          vw_worker_queue_control_applies_to_preceding_epoch(q, i)) {
        lifecycle = i;
        break;
      }
    }
    if (lifecycle > q->tail && lifecycle < q->head) {
      uint16_t lifecycle_type = q->slots[lifecycle % q->capacity].type;
      if (lifecycle_type == VW_MSG_PAUSE || lifecycle_type == VW_MSG_RESUME) {
        vw_msg_control_t lifecycle_control;
        size_t latest_position = q->head;
        vw_worker_frame_t* control = &q->slots[lifecycle % q->capacity];
        if (control->payload &&
            vw_protocol_decode_payload(lifecycle_type, control->payload, control->payload_len, &lifecycle_control)) {
          for (size_t i = q->tail; i < lifecycle; i++) {
            if (q->slots[i % q->capacity].type != VW_MSG_POSITION) continue;
            vw_session_id_t position_session;
            if (vw_worker_queue_frame_session_id(&q->slots[i % q->capacity], &position_session) &&
                memcmp(position_session.bytes, lifecycle_control.session_id.bytes, VW_SESSION_ID_BYTES) == 0) {
              latest_position = i;
            }
          }
        }
        if (latest_position < lifecycle) {
          // POSITION carries playhead/seek state that PAUSE/RESUME do not. Apply only the newest same-session
          // snapshot first, while discarding stale PCM and older POSITION frames ahead of the lifecycle transition.
          *out = q->slots[latest_position % q->capacity];
          size_t write = q->tail;
          const size_t old_head = q->head;
          for (size_t i = q->tail; i < lifecycle; i++) {
            vw_worker_frame_t* frame = &q->slots[i % q->capacity];
            if (i == latest_position) continue;
            if (frame->type == VW_MSG_AUDIO_PCM) {
              atomic_fetch_add_explicit(&q->dropped_audio_us,
                                        vw_worker_queue_audio_duration_us(frame->payload, frame->payload_len),
                                        memory_order_relaxed);
              free(frame->payload);
              continue;
            }
            if (frame->type == VW_MSG_POSITION) {
              free(frame->payload);
              continue;
            }
            if (write != i) q->slots[write % q->capacity] = *frame;
            write++;
          }
          for (size_t i = lifecycle; i < old_head; i++) {
            if (write != i) q->slots[write % q->capacity] = q->slots[i % q->capacity];
            write++;
          }
          for (size_t i = write; i < old_head; i++) {
            memset(&q->slots[i % q->capacity], 0, sizeof(q->slots[i % q->capacity]));
          }
          q->head = write;
          pthread_mutex_unlock(&q->mutex);
          return true;
        }
      }

      *out = q->slots[lifecycle % q->capacity];
      size_t write = q->tail;
      const size_t old_head = q->head;
      for (size_t i = q->tail; i < lifecycle; i++) {
        vw_worker_frame_t* frame = &q->slots[i % q->capacity];
        if (frame->type == VW_MSG_AUDIO_PCM) {
          atomic_fetch_add_explicit(&q->dropped_audio_us,
                                    vw_worker_queue_audio_duration_us(frame->payload, frame->payload_len),
                                    memory_order_relaxed);
          free(frame->payload);
          continue;
        }
        if (frame->type == VW_MSG_POSITION) {
          // STOP/replacement START/SHUTDOWN supersede position state. For PAUSE/RESUME, any preserved same-session
          // POSITION was already returned above, so no stale snapshot can replay after the lifecycle transition.
          free(frame->payload);
          continue;
        }
        if (write != i) q->slots[write % q->capacity] = *frame;
        write++;
      }
      for (size_t i = lifecycle + 1; i < old_head; i++) {
        if (write != i) q->slots[write % q->capacity] = q->slots[i % q->capacity];
        write++;
      }
      for (size_t i = write; i < old_head; i++) {
        memset(&q->slots[i % q->capacity], 0, sizeof(q->slots[i % q->capacity]));
      }
      q->head = write;
      pthread_mutex_unlock(&q->mutex);
      return true;
    }
  }

  *out = q->slots[q->tail % q->capacity];
  memset(&q->slots[q->tail % q->capacity], 0, sizeof(q->slots[q->tail % q->capacity]));
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
