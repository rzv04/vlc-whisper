#include "vw_audio_buffer.h"

#include <stdlib.h>

struct vw_audio_buffer {
  size_t max_samples;
};

vw_audio_buffer_t *vw_audio_buffer_create(size_t max_samples) {
  vw_audio_buffer_t *buf = (vw_audio_buffer_t *)calloc(1, sizeof(vw_audio_buffer_t));
  if (buf) {
    buf->max_samples = max_samples;
  }
  return buf;
}

void vw_audio_buffer_free(vw_audio_buffer_t *buf) {
  if (buf) {
    free(buf);
  }
}

bool vw_audio_buffer_append_s16le(vw_audio_buffer_t *buf, const int16_t *pcm16, size_t sample_count, int64_t pts_us) {
  (void)buf;
  (void)pcm16;
  (void)sample_count;
  (void)pts_us;
  return true;
}
