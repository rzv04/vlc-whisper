#include "vw_vad.h"

struct whisper_vad_context* vw_vad_init_default(const char* path_model) {
  if (path_model == NULL) {
    return NULL;  // Invalid parameter
  }

  struct whisper_vad_context_params vad_params = whisper_vad_default_context_params();
  struct whisper_vad_context* vctx = whisper_vad_init_from_file_with_params(path_model, vad_params);

  return vctx;  // Success
}

bool vw_vad_detect_speech(const float* pcm32, size_t sample_count, struct whisper_vad_context* vctx) {
  if (pcm32 == NULL || sample_count == 0 || vctx == NULL) {
    return false;  // Invalid parameters
  }

  if (whisper_vad_detect_speech(vctx, pcm32, (int)sample_count) == 0) {
    return true;  // Speech detected (above defined threshold)
  }

  return false;  // No speech detected
}
