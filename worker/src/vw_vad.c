#include "vw_vad.h"

#include <math.h>

struct whisper_vad_context* vw_vad_init_default(const char* path_model) {
  if (path_model == NULL || path_model[0] == '\0') {
    return NULL;
  }

  struct whisper_vad_context_params vad_params = whisper_vad_default_context_params();
  struct whisper_vad_context* vctx = whisper_vad_init_from_file_with_params(path_model, vad_params);
  if (vctx != NULL) {
    whisper_vad_reset_state(vctx);
  }

  return vctx;
}

bool vw_vad_detect_speech(const float* pcm32, size_t sample_count, struct whisper_vad_context* vctx) {
  if (pcm32 == NULL || sample_count == 0) {
    return false;
  }

  if (vctx != NULL) {
    if (!whisper_vad_detect_speech_no_reset(vctx, pcm32, (int)sample_count)) {
      return false;
    }
    struct whisper_vad_params vad_params = whisper_vad_default_params();
    struct whisper_vad_segments* segments = whisper_vad_segments_from_probs(vctx, vad_params);
    if (segments == NULL) {
      return false;
    }
    int n_segs = whisper_vad_segments_n_segments(segments);
    whisper_vad_free_segments(segments);
    return n_segs > 0;
  }

  // Fallback: no ML model loaded, use energy-based detection
  return vw_vad_detect_speech_energy(pcm32, sample_count, VW_VAD_ENERGY_THRESHOLD);
}

bool vw_vad_detect_speech_energy(const float* pcm32, size_t sample_count, float threshold) {
  if (pcm32 == NULL || sample_count == 0 || threshold < 0.0f) {
    return false;
  }

  double sum_sq = 0.0;
  for (size_t i = 0; i < sample_count; ++i) {
    sum_sq += (double)pcm32[i] * (double)pcm32[i];
  }

  // Calculate mean square energy directly: sqrt(mean_square) > threshold <=> mean_square > threshold^2
  float mean_square = (float)(sum_sq / (double)sample_count);
  float threshold_sq = threshold * threshold;

  return mean_square > threshold_sq;
}

void vw_vad_reset_state(struct whisper_vad_context* vctx) {
  if (vctx != NULL) {
    whisper_vad_reset_state(vctx);
  }
}

void vw_vad_free(struct whisper_vad_context* vctx) {
  if (vctx != NULL) {
    whisper_vad_free(vctx);
  }
}
