#ifndef VW_VAD_H_
#define VW_VAD_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool vw_vad_detect_speech(const float *pcm32, size_t sample_count);

#endif // VW_VAD_H_
