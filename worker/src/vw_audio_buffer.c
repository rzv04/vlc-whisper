#include "vw_audio_buffer.h"

#include <stdlib.h>
#include <string.h>

vw_audio_buffer_t* vw_audio_buffer_create(size_t max_samples) {
  if (max_samples == 0) return NULL;
  vw_audio_buffer_t* buf = (vw_audio_buffer_t*)calloc(1, sizeof(vw_audio_buffer_t));
  if (!buf) return NULL;
  buf->samples = (float*)calloc(max_samples, sizeof(float));
  if (!buf->samples) {
    free(buf);
    return NULL;
  }
  buf->max_samples = max_samples;
  buf->start_pts_us = -1;
  return buf;
}

void vw_audio_buffer_free(vw_audio_buffer_t* buf) {
  if (buf) {
    if (buf->samples) free(buf->samples);
    free(buf);
  }
}

bool vw_audio_buffer_append_s16le(vw_audio_buffer_t* buf, const int16_t* pcm16, size_t sample_count, int64_t pts_us) {
  if (!buf || !buf->samples || !pcm16 || sample_count == 0) return false;

  // Set initial start PTS if buffer is currently empty
  if (buf->count == 0 || buf->start_pts_us < 0) {
    buf->start_pts_us = pts_us;
  }

  for (size_t i = 0; i < sample_count; i++) {
    float norm = pcm16[i] / 32768.0f;
    buf->samples[buf->head] = norm;
    buf->head = (buf->head + 1) % buf->max_samples;
    if (buf->count < buf->max_samples) {
      buf->count++;
    } else {
      // Ring buffer overflow: drop oldest sample and advance start_pts_us by 1 sample duration (62.5 us at 16kHz)
      buf->dropped_samples++;
      buf->start_pts_us += 63;  // 1000000 us / 16000 Hz = 62.5 us
    }
  }
  return true;
}

size_t vw_audio_buffer_get_count(const vw_audio_buffer_t* buf) { return buf ? buf->count : 0; }

size_t vw_audio_buffer_get_samples(const vw_audio_buffer_t* buf, float* out_samples, size_t max_out,
                                   int64_t* out_pts_us) {
  if (!buf || !buf->samples || !out_samples || max_out == 0 || buf->count == 0) return 0;

  size_t to_copy = (buf->count < max_out) ? buf->count : max_out;
  size_t tail = (buf->head + buf->max_samples - buf->count) % buf->max_samples;

  for (size_t i = 0; i < to_copy; i++) {
    out_samples[i] = buf->samples[(tail + i) % buf->max_samples];
  }

  if (out_pts_us) {
    *out_pts_us = buf->start_pts_us;
  }
  return to_copy;
}

void vw_audio_buffer_drain(vw_audio_buffer_t* buf, size_t sample_count) {
  if (!buf || sample_count == 0 || buf->count == 0) return;

  size_t drained = (sample_count < buf->count) ? sample_count : buf->count;
  buf->count -= drained;
  buf->start_pts_us += (int64_t)(drained * 63);  // 16kHz sample duration

  if (buf->count == 0) {
    buf->head = 0;
    buf->start_pts_us = -1;
  }
}

void vw_audio_buffer_clear(vw_audio_buffer_t* buf) {
  if (!buf) return;
  buf->head = 0;
  buf->count = 0;
  buf->start_pts_us = -1;
}
