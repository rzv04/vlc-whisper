#ifndef VW_VAD_H_
#define VW_VAD_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <whisper.h>

#define VW_VAD_ENERGY_THRESHOLD 0.01f  // RMS energy floor for speech detection (normalized float [-1,+1])

// Initializes a standalone Silero VAD context from a GGML model file using default parameters. Returns a valid context
// pointer on success, or NULL if the model file is missing or invalid.
struct whisper_vad_context* vw_vad_init_default(const char* path_model);

// Detects voice activity in a 16kHz float32 audio buffer. Uses Silero VAD when vctx is non-null, falling back
// transparently to RMS energy thresholding when vctx is NULL.
bool vw_vad_detect_speech(const float* pcm32, size_t sample_count, struct whisper_vad_context* vctx);

// Lightweight energy-based speech detector comparing root-mean-square amplitude against a float threshold. Returns
// true if audio energy exceeds the specified limit without requiring model weights.
bool vw_vad_detect_speech_energy(const float* pcm32, size_t sample_count, float threshold);

// Resets internal recurrent LSTM states in the Silero VAD context. Must be invoked during seeking, pause resume, or
// session epoch transitions to prevent past audio state leakage.
void vw_vad_reset_state(struct whisper_vad_context* vctx);

// Releases all resources and memory buffers allocated for the Silero VAD context. Safe to call with a NULL context
// pointer.
void vw_vad_free(struct whisper_vad_context* vctx);

#endif  // VW_VAD_H_
