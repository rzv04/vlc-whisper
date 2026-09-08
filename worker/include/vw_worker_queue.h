#ifndef VW_WORKER_QUEUE_H_
#define VW_WORKER_QUEUE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A queued IPC frame: message type tag, payload length, and the payload bytes
// (a malloc'd block owned by the queue slot until popped; NULL when length is 0).
typedef struct vw_worker_frame {
  uint16_t type;         // vw_message_type_t
  uint32_t payload_len;  // 0 for zero-payload frames
  uint8_t* payload;      // owned block; NULL when payload_len == 0
} vw_worker_frame_t;

typedef struct vw_worker_queue vw_worker_queue_t;  // opaque

// Capacity for the worker's inbound IPC frame queue (512 slots provides ~10.2s of backlog at 20ms
// frame cadence to absorb Whisper batch inference compute spikes without dropping audio frames).
#define VW_WORKER_FRAME_QUEUE_CAPACITY 512U

// Allocates a bounded mutex-backed frame queue with owned payload slots. Returns NULL for zero capacity or allocation
// failure; worker code may call it outside realtime callbacks.
vw_worker_queue_t* vw_worker_queue_create(size_t capacity);

// Frees the queue and every queued payload. Safe on NULL; callers must guarantee no concurrent producer or consumer
// accesses while destruction is in progress.
void vw_worker_queue_destroy(vw_worker_queue_t* q);

// Takes ownership of one frame and enqueues it under the bounded overflow policy. Returns false only when the incoming
// frame is dropped; detailed eviction rules live in the implementation.
bool vw_worker_queue_push(vw_worker_queue_t* q, uint16_t type, uint8_t* payload, uint32_t payload_len);

// Pops the oldest frame and transfers payload ownership to the caller. Returns false when empty and never blocks beyond
// the queue's short internal mutex critical section.
bool vw_worker_queue_pop(vw_worker_queue_t* q, vw_worker_frame_t* out);

// Pops with worker lifecycle priority after HELLO, promoting session controls ahead of queued PCM and accounting any
// obsolete audio discarded before the promoted transition.
bool vw_worker_queue_pop_prioritized(vw_worker_queue_t* q, vw_worker_frame_t* out);

// Returns total microseconds of audio discarded by queue overflow or lifecycle invalidation using a relaxed atomic
// load, so readers do not need the queue mutex.
uint64_t vw_worker_queue_get_dropped_audio_us(const vw_worker_queue_t* q);

#endif  // VW_WORKER_QUEUE_H_
