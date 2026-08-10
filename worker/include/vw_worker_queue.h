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

// Allocates a bounded FIFO queue holding IPC frames with owned payload buffers. Capacity limits
// outstanding frames; returns NULL on allocation failure. Not realtime: uses a mutex internally.
vw_worker_queue_t* vw_worker_queue_create(size_t capacity);

// Frees the queue and every payload still queued in it. Safe on NULL. Caller must ensure no thread
// pushes or pops concurrently while the queue is being destroyed.
void vw_worker_queue_destroy(vw_worker_queue_t* q);

// Pushes one frame, taking ownership of payload (freed if the frame is dropped). On overflow, drops
// the oldest queued audio frame; control frames are never dropped to make room for audio. On an
// all-control overflow, evicts only PAUSE/RESUME (stateless no-ops), a same-type control the
// incoming supersedes, or — for SHUTDOWN — any control; queued START/STOP/SHUTDOWN transitions are
// never evicted for an unrelated control. Returns true if accepted; false when the frame is dropped
// (a counted AUDIO drop, or a control dropped because the all-control queue held nothing safe to
// evict — keep the queued transitions rather than lose one).
bool vw_worker_queue_push(vw_worker_queue_t* q, uint16_t type, uint8_t* payload, uint32_t payload_len);

// Pops the oldest frame into out, transferring payload ownership to the caller, who must free it
// when done. Returns false when the queue is empty; never blocks.
bool vw_worker_queue_pop(vw_worker_queue_t* q, vw_worker_frame_t* out);

// Returns the total microseconds of audio dropped by the overflow policy, via a relaxed atomic load
// so any thread may read it without taking the queue lock.
uint64_t vw_worker_queue_get_dropped_audio_us(const vw_worker_queue_t* q);

#endif  // VW_WORKER_QUEUE_H_
