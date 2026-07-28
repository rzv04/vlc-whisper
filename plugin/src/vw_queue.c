#include "vw_queue.h"

#include <stdint.h>
#include <stdlib.h>
#include "vw_audio_capture.h"


vw_spsc_queue_t* vw_spsc_queue_create(size_t capacity_bytes) {
  vw_spsc_queue_t* q = (vw_spsc_queue_t*)calloc(1, sizeof(vw_spsc_queue_t));
  if (q) {
    q->capacity_bytes = capacity_bytes;
  }
  return q;
}

void vw_spsc_queue_destroy(vw_spsc_queue_t* queue) {
  if (queue) {
    free(queue);
  }
}

bool vw_spsc_queue_push(vw_spsc_queue_t* queue, const vw_audio_chunk_t* chunk) {
  (void)queue;
  (void)chunk;
  return true;
}

bool vw_spsc_queue_pop(vw_spsc_queue_t* queue, vw_audio_chunk_t* chunk) {
  (void)queue;
  (void)chunk;
  return false;
}

uint64_t vw_spsc_queue_get_dropped_microseconds(const vw_spsc_queue_t* queue) { return queue ? queue->audio_dropped_us : 0; }
