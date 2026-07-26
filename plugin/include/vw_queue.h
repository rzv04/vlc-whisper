#ifndef VW_QUEUE_H_
#define VW_QUEUE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct vw_audio_chunk {
  int64_t start_pts_us;
  int64_t duration_us;
  uint32_t sample_rate;
  uint16_t channels;
  uint32_t pcm_bytes;
  uint8_t *pcm_data;
} vw_audio_chunk_t;

typedef struct vw_spsc_queue vw_spsc_queue_t;

vw_spsc_queue_t *vw_spsc_queue_create(size_t capacity_bytes);
void vw_spsc_queue_destroy(vw_spsc_queue_t *queue);
bool vw_spsc_queue_push(vw_spsc_queue_t *queue, const vw_audio_chunk_t *chunk);
bool vw_spsc_queue_pop(vw_spsc_queue_t *queue, vw_audio_chunk_t *chunk);
uint64_t vw_spsc_queue_get_dropped_microseconds(const vw_spsc_queue_t *queue);

#endif // VW_QUEUE_H_
