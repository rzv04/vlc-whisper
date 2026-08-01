#ifndef VW_QUEUE_H_
#define VW_QUEUE_H_

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vw_audio_capture.h"

// Single-producer single-consumer (SPSC) lockless queue for audio chunks
typedef struct vw_spsc_queue {
  vw_audio_chunk_t* ring_buffer;
  size_t capacity_chunks;
  _Atomic size_t head;  // Producer write index
  _Atomic size_t tail;  // Consumer read index
  _Atomic uint64_t audio_dropped_us;
} vw_spsc_queue_t;

// Allocates a lockless SPSC queue with the specified chunk capacity. Allocates capacity+1
// internally to distinguish full/empty states safely without requiring locks.
vw_spsc_queue_t* vw_spsc_queue_create(size_t capacity_chunks);

// Destroys the SPSC queue and frees its ring buffer. Must only be called after all
// producer/consumer threads have safely stopped to avoid use-after-free race conditions.
void vw_spsc_queue_destroy(vw_spsc_queue_t* queue);

// Pushes an audio chunk into the queue using C11 stdatomic lock-free semantics. If full,
// safely drops the chunk and atomically increments dropped_us (playback wins backpressure).
bool vw_spsc_queue_push(vw_spsc_queue_t* queue, const vw_audio_chunk_t* chunk);

// Pops an audio chunk from the queue into the provided memory location. Lock-free
// (stdatomic). Returns NULL instantly if empty; never blocks or waits for data.
vw_audio_chunk_t* vw_spsc_queue_pop(vw_spsc_queue_t* queue, vw_audio_chunk_t* chunk);

// Retrieves the total monotonically increasing duration (in microseconds) of dropped audio due to queue overflow.
// Uses a relaxed atomic load for fast, lock-free reading by any thread.
uint64_t vw_spsc_queue_get_dropped_microseconds(const vw_spsc_queue_t* queue);

#endif  // VW_QUEUE_H_
