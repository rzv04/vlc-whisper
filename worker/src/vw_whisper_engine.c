#include "vw_whisper_engine.h"

#include <stdlib.h>



vw_whisper_engine_t *vw_whisper_engine_init(const char *model_path) {
  (void)model_path;
  vw_whisper_engine_t *eng = (vw_whisper_engine_t *)calloc(1, sizeof(vw_whisper_engine_t));
  return eng;
}

void vw_whisper_engine_free(vw_whisper_engine_t *engine) {
  if (engine) {
    free(engine);
  }
}

bool vw_whisper_engine_transcribe_pcm(vw_whisper_engine_t *engine, const float *pcm32, size_t sample_count) {
  (void)engine;
  (void)pcm32;
  (void)sample_count;
  return true;
}
