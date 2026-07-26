#ifndef VW_AUDIO_CAPTURE_H_
#define VW_AUDIO_CAPTURE_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct vw_audio_capture vw_audio_capture_t;

// Non-blocking callback interface for VLC audio pipeline
bool vw_audio_capture_on_pcm_block(vw_audio_capture_t *cap, const int16_t *pcm_samples, size_t sample_count, int64_t pts_us);

#endif // VW_AUDIO_CAPTURE_H_
