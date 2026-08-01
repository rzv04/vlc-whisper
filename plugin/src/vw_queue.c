#include "vw_queue.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

#include "vw_audio_capture.h"

vw_spsc_queue_t* vw_spsc_queue_create(size_t capacity_chunks) {
  if (capacity_chunks == 0) {
    return NULL;
  }
  vw_spsc_queue_t* q = (vw_spsc_queue_t*)calloc(1, sizeof(vw_spsc_queue_t));
  if (q) {
    q->capacity_chunks = capacity_chunks;
    // Allocate capacity + 1 to distinguish full from empty
    q->ring_buffer = (vw_audio_chunk_t*)calloc(capacity_chunks + 1, sizeof(vw_audio_chunk_t));
    if (!q->ring_buffer) {
      free(q);
      return NULL;
    }
    atomic_init(&q->head, 0);
    atomic_init(&q->tail, 0);
    atomic_init(&q->audio_dropped_us, 0);
  }
  return q;
}

void vw_spsc_queue_destroy(vw_spsc_queue_t* queue) {
  if (queue) {
    free(queue->ring_buffer);
    free(queue);
  }
}

bool vw_spsc_queue_push(vw_spsc_queue_t* queue, const vw_audio_chunk_t* chunk) {
  if (!queue || !chunk) {
    return false;
  }

  size_t curr_head = atomic_load_explicit(&queue->head, memory_order_relaxed);
  size_t next_head = (curr_head + 1) % (queue->capacity_chunks + 1);

  if (next_head == atomic_load_explicit(&queue->tail, memory_order_acquire)) {
    // Queue is full. Drop the newest chunk (skip writing).
    atomic_fetch_add_explicit(&queue->audio_dropped_us, chunk->duration_us, memory_order_relaxed);
    return false;
  }

  // Write chunk into the ring buffer
  queue->ring_buffer[curr_head] = *chunk;

  // Publish the written chunk
  atomic_store_explicit(&queue->head, next_head, memory_order_release);
  return true;
}

vw_audio_chunk_t* vw_spsc_queue_pop(vw_spsc_queue_t* queue, vw_audio_chunk_t* chunk) {
  if (!queue || !chunk) {
    return NULL;
  }

  size_t curr_tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);

  if (curr_tail == atomic_load_explicit(&queue->head, memory_order_acquire)) {
    // Queue is empty
    return NULL;
  }

  // Read chunk from the ring buffer
  *chunk = queue->ring_buffer[curr_tail];

  // Advance consumer index
  size_t next_tail = (curr_tail + 1) % (queue->capacity_chunks + 1);
  atomic_store_explicit(&queue->tail, next_tail, memory_order_release);

  return chunk;
}

uint64_t vw_spsc_queue_get_dropped_microseconds(const vw_spsc_queue_t* queue) {
  return queue ? atomic_load_explicit(&queue->audio_dropped_us, memory_order_relaxed) : 0;
}
