#include <stdlib.h>
#include <string.h>

#include "vw_asr_engine.h"
#include "vw_test.h"
#include "vw_whisper_engine.h"

static int init_calls, transcribe_calls, language_calls, thread_calls, free_calls;
static char last_language[16];

vw_whisper_engine_t* vw_whisper_engine_init(const char* path, vw_worker_backend_t backend, int device) {
  vw_test_check_true("adapter forwards model and backend",
                     strcmp(path, "model.bin") == 0 && backend == VW_WORKER_BACKEND_CPU && device == 2);
  init_calls++;
  return calloc(1, sizeof(vw_whisper_engine_t));
}
bool vw_whisper_engine_set_language(vw_whisper_engine_t* engine, const char* language) {
  if (!engine || strcmp(language, "invalid") == 0) return false;
  language_calls++;
  snprintf(last_language, sizeof(last_language), "%s", language);
  return true;
}
bool vw_whisper_engine_set_n_threads(vw_whisper_engine_t* engine, int threads) {
  if (!engine) return false;
  thread_calls++;
  engine->n_threads = threads;
  return true;
}
void vw_whisper_engine_free(vw_whisper_engine_t* engine) {
  free_calls++;
  free(engine);
}
bool vw_whisper_engine_transcribe_pcm(vw_whisper_engine_t* engine, const float* pcm, size_t n) {
  transcribe_calls++;
  return engine && pcm && n == 2;
}
int vw_whisper_engine_get_segment_count(const vw_whisper_engine_t* engine) { return engine ? 1 : 0; }
bool vw_whisper_engine_get_segment(const vw_whisper_engine_t* engine, int index, vw_whisper_segment_t* result) {
  if (!engine || index != 0 || !result) return false;
  *result =
      (vw_whisper_segment_t){.t0_us = 120000, .t1_us = 760000, .no_speech_prob = 0.1f, .text_utf8 = "spoken words"};
  return true;
}
uint64_t vw_whisper_engine_get_total_inference_us(const vw_whisper_engine_t* engine) { return engine ? 42 : 0; }
bool vw_whisper_engine_is_gpu_active(const vw_whisper_engine_t* engine) { return engine && engine->gpu_active; }

int main(void) {
  vw_asr_engine_config_t config = {.kind = VW_ASR_ENGINE_WHISPER,
                                   .model_path = "model.bin",
                                   .backend = VW_WORKER_BACKEND_CPU,
                                   .gpu_device = 2,
                                   .language = "en",
                                   .n_threads = 5};
  vw_asr_engine_t* engine = vw_asr_engine_create(&config);
  vw_test_check_true("Whisper construction crosses facade once",
                     engine && init_calls == 1 && language_calls == 1 && thread_calls == 1);
  vw_test_check_true("session language crosses facade", vw_asr_engine_set_language(engine, "tr") &&
                                                            language_calls == 2 && strcmp(last_language, "tr") == 0);
  vw_test_check_false("invalid session language fails closed", vw_asr_engine_set_language(engine, "invalid"));
  const float pcm[2] = {0.0f, 0.2f};
  vw_test_check_true("PCM inference crosses facade", vw_asr_engine_transcribe_pcm(engine, pcm, 2) &&
                                                         transcribe_calls == 1 &&
                                                         vw_asr_engine_get_result_count(engine) == 1);
  vw_asr_result_t result = {0};
  vw_test_check_true("result preserves microsecond offsets and finality",
                     vw_asr_engine_get_result(engine, 0, &result) && result.start_offset_us == 120000 &&
                         result.end_offset_us == 760000 && result.is_final && result.utterance_id == 0 &&
                         result.revision == 0 && strcmp(result.text_utf8, "spoken words") == 0);
  vw_test_check_true("status uses adapter metrics",
                     vw_asr_engine_get_total_inference_us(engine) == 42 && !vw_asr_engine_is_gpu_active(engine));
  vw_asr_engine_free(engine);
  vw_test_check_true("facade owns adapter destruction", free_calls == 1);
  config.kind = VW_ASR_ENGINE_NEMOTRON;
  vw_test_check_true("unavailable engine never falls back to Whisper",
                     vw_asr_engine_create(&config) == NULL && init_calls == 1);
  return vw_test_finish("test_asr_engine_adapter");
}
