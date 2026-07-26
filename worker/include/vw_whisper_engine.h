#ifndef VW_WHISPER_ENGINE_H_
#define VW_WHISPER_ENGINE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct vw_whisper_engine vw_whisper_engine_t;

vw_whisper_engine_t *vw_whisper_engine_init(const char *model_path);
void vw_whisper_engine_free(vw_whisper_engine_t *engine);
bool vw_whisper_engine_transcribe_pcm(vw_whisper_engine_t *engine, const float *pcm32, size_t sample_count);

#endif // VW_WHISPER_ENGINE_H_
