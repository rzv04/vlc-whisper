#ifndef VW_AUDIO_BUFFER_H_
#define VW_AUDIO_BUFFER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct vw_audio_buffer vw_audio_buffer_t;

vw_audio_buffer_t *vw_audio_buffer_create(size_t max_samples);
void vw_audio_buffer_free(vw_audio_buffer_t *buf);
bool vw_audio_buffer_append_s16le(vw_audio_buffer_t *buf, const int16_t *pcm16, size_t sample_count, int64_t pts_us);

#endif // VW_AUDIO_BUFFER_H_
