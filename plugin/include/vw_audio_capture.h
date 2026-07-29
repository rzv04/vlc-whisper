#ifndef VW_AUDIO_CAPTURE_H_
#define VW_AUDIO_CAPTURE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct vw_audio_capture {
  uint32_t sample_rate;
} vw_audio_capture_t;

typedef struct vw_audio_chunk {
  int64_t start_pts_us;
  int64_t duration_us;
  uint32_t sample_rate;
  uint32_t channels;
  uint32_t bytes;
} vw_audio_chunk_t;

// Non-blocking callback interface for VLC audio pipeline
bool vw_audio_capture_on_pcm_block(vw_audio_capture_t* cap, const int16_t* pcm_samples, size_t sample_count,
                                   int64_t pts_us);

#endif  // VW_AUDIO_CAPTURE_H_
