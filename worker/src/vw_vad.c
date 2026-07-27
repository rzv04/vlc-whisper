#include "vw_vad.h"

#include <math.h>

struct whisper_vad_context* vw_vad_init_default(const char* path_model) {
  if (path_model == NULL) {
    return NULL;  // Invalid parameter
  }

  struct whisper_vad_context_params vad_params = whisper_vad_default_context_params();
  struct whisper_vad_context* vctx = whisper_vad_init_from_file_with_params(path_model, vad_params);

  return vctx;  // Success
}

bool vw_vad_detect_speech(const float* pcm32, size_t sample_count, struct whisper_vad_context* vctx) {
  if (pcm32 == NULL || sample_count == 0) {
    return false;
  }

  if (vctx != NULL) {
    return whisper_vad_detect_speech(vctx, pcm32, (int)sample_count);
  }

  // Fallback: no ML model loaded, use energy-based detection
  return vw_vad_detect_speech_energy(pcm32, sample_count, VW_VAD_ENERGY_THRESHOLD);
}

bool vw_vad_detect_speech_energy(const float* pcm32, size_t sample_count, float threshold) {
  // Edge-case handling
  if (pcm32 == NULL || sample_count == 0 || threshold < 0.0f) {
    return false;
  }

  double sum_sq = 0.0;
  for (size_t i = 0; i < sample_count; ++i) {
    sum_sq += (double)pcm32[i] * (double)pcm32[i];
  }

  // Calculate mean square energy directly
  float mean_square = (float)(sum_sq / (double)sample_count);
  float threshold_sq = threshold * threshold;

  // Mathematically identical to: sqrt(mean_square) > threshold
  return mean_square > threshold_sq;
}

void vw_vad_free(struct whisper_vad_context* vctx) {
  if (vctx != NULL) {
    whisper_vad_free(vctx);
  }
}