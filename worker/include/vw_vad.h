#ifndef VW_VAD_H_
#define VW_VAD_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <whisper.h>

// Initializes a separate VAD context using the default parameters and the provided model path, separate from the
// whisper pipeline. Returns a pointer to the initialized context, or NULL on failure.

// Documented defaults are:
/*  threshold               =  0.5f,    Threshold for speech detection (0.0 to 1.0)
    min_speech_duration_ms  =  250,     Minimum duration of speech to consider it valid (in milliseconds)
    min_silence_duration_ms =  100,     Minimum duration of silence to consider speech ended (in milliseconds)
    max_speech_duration_s   =  FLT_MAX, Maximum duration of a speech segment before forcing a new segment (in seconds)
    speech_pad_ms           =  30,      Padding added before and after speech segments (in milliseconds)
    samples_overlap         =  0.1,     Overlap in seconds when copying audio samples from speech segment (in seconds)
*/

struct whisper_vad_context* vw_vad_init_default(const char* path_model);

// Detects speech in a given PCM audio buffer/window using a VAD model. The input audio is expected to be in
// 32-bit floating point format (normalized to [-1.0f, +1.0f]) and sampled at 16kHz. The function returns true if any
// speech is detected, and false otherwise.
bool vw_vad_detect_speech(const float* pcm32, size_t sample_count, struct whisper_vad_context* vctx);

#endif  // VW_VAD_H_
