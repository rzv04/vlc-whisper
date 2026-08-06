#include "vw_whisper_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "whisper.h"

vw_whisper_engine_t* vw_whisper_engine_init(const char* model_path) {
  if (!model_path || model_path[0] == '\0') return NULL;

  struct whisper_context_params cparams = whisper_context_default_params();
  struct whisper_context* ctx = whisper_init_from_file_with_params(model_path, cparams);
  if (!ctx) {
    return NULL;
  }

  vw_whisper_engine_t* eng = (vw_whisper_engine_t*)calloc(1, sizeof(vw_whisper_engine_t));
  if (!eng) {
    whisper_free(ctx);
    return NULL;
  }
  eng->ctx = ctx;
  eng->last_text_bytes = 2048;
  eng->last_text = (char*)calloc(1, eng->last_text_bytes);
  if (!eng->last_text) {
    whisper_free(ctx);
    free(eng);
    return NULL;
  }

  // Perform one silent warmup pass on 100ms of zeros
  float silent[1600] = {0};
  struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  wparams.print_progress = false;
  wparams.print_special = false;
  wparams.print_realtime = false;
  wparams.print_timestamps = false;
  wparams.translate = false;
  wparams.language = "en";
  wparams.n_threads = 2;
  whisper_full(eng->ctx, wparams, silent, 1600);

  return eng;
}

void vw_whisper_engine_free(vw_whisper_engine_t* engine) {
  if (engine) {
    if (engine->ctx) {
      whisper_free(engine->ctx);
    }
    if (engine->last_text) {
      free(engine->last_text);
    }
    free(engine);
  }
}

bool vw_whisper_engine_transcribe_pcm(vw_whisper_engine_t* engine, const float* pcm32, size_t sample_count) {
  if (!engine || !engine->ctx || !pcm32 || sample_count == 0) return false;

  struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  wparams.print_progress = false;
  wparams.print_special = false;
  wparams.print_realtime = false;
  wparams.print_timestamps = false;
  wparams.translate = false;
  wparams.language = "en";
  wparams.n_threads = 4;

  if (whisper_full(engine->ctx, wparams, pcm32, (int)sample_count) != 0) {
    return false;
  }

  engine->last_text[0] = '\0';
  size_t written = 0;
  int n_segments = whisper_full_n_segments(engine->ctx);
  for (int i = 0; i < n_segments; i++) {
    const char* txt = whisper_full_get_segment_text(engine->ctx, i);
    if (!txt) continue;
    size_t len = strlen(txt);
    if (written + len + 2 >= engine->last_text_bytes) {
      size_t new_cap = engine->last_text_bytes * 2 + len + 2;
      char* new_buf = (char*)realloc(engine->last_text, new_cap);
      if (!new_buf) return false;  // return false instead of shipping truncated text
      engine->last_text = new_buf;
      engine->last_text_bytes = new_cap;
    }
    memcpy(engine->last_text + written, txt, len);
    written += len;
    engine->last_text[written] = '\0';
  }

  return true;
}

const char* vw_whisper_engine_get_text(const vw_whisper_engine_t* engine) {
  if (!engine || !engine->last_text) return "";
  return engine->last_text;
}
