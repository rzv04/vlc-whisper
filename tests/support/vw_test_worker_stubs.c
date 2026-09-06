#include "vw_test_worker_stubs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_source_decoder.h"
#include "vw_vad.h"
#include "vw_whisper_engine.h"

struct vw_source_decoder {
  int64_t pts_us;
  bool emitted_data;
};

static vw_test_decoder_mode_t g_decoder_mode = VW_TEST_DECODER_DATA;
static int g_decoder_read_calls = 0;
static int g_transcribe_calls = 0;

void vw_test_worker_stubs_reset(void) {
  g_decoder_mode = VW_TEST_DECODER_DATA;
  g_decoder_read_calls = 0;
  g_transcribe_calls = 0;
}

void vw_test_decoder_set_mode(vw_test_decoder_mode_t mode) { g_decoder_mode = mode; }

int vw_test_decoder_read_calls(void) { return g_decoder_read_calls; }

int vw_test_whisper_transcribe_calls(void) { return g_transcribe_calls; }

vw_whisper_engine_t* vw_whisper_engine_init(const char* model_path, vw_worker_backend_t backend, int gpu_device) {
  (void)model_path;
  (void)backend;
  (void)gpu_device;
  vw_whisper_engine_t* engine = (vw_whisper_engine_t*)calloc(1, sizeof(*engine));
  if (engine) {
    snprintf(engine->language, sizeof(engine->language), "%s", "en");
    engine->n_threads = 1;
  }
  return engine;
}

bool vw_whisper_engine_set_language(vw_whisper_engine_t* engine, const char* language) {
  if (!engine || !language || !language[0] || strlen(language) >= sizeof(engine->language)) return false;
  snprintf(engine->language, sizeof(engine->language), "%s", language);
  return true;
}

bool vw_whisper_engine_set_n_threads(vw_whisper_engine_t* engine, int n_threads) {
  if (!engine) return false;
  if (n_threads < 1) n_threads = 1;
  if (n_threads > 16) n_threads = 16;
  engine->n_threads = n_threads;
  return true;
}

bool vw_whisper_engine_is_gpu_active(const vw_whisper_engine_t* engine) {
  (void)engine;
  return false;
}

void vw_whisper_engine_free(vw_whisper_engine_t* engine) { free(engine); }

bool vw_whisper_engine_transcribe_pcm(vw_whisper_engine_t* engine, const float* pcm32, size_t sample_count) {
  if (!engine || !pcm32 || sample_count == 0) return false;
  g_transcribe_calls++;
  engine->last_inference_us = 1000;
  engine->total_inference_us += 1000;
  return true;
}

const char* vw_whisper_engine_get_text(const vw_whisper_engine_t* engine) { return engine ? "stub speech" : ""; }

int vw_whisper_engine_get_segment_count(const vw_whisper_engine_t* engine) {
  return (engine && g_transcribe_calls > 0) ? 1 : 0;
}

uint64_t vw_whisper_engine_get_total_inference_us(const vw_whisper_engine_t* engine) {
  return engine ? engine->total_inference_us : 0;
}

bool vw_whisper_engine_get_segment(const vw_whisper_engine_t* engine, int index, vw_whisper_segment_t* out_seg) {
  if (!engine || !out_seg || index != 0 || g_transcribe_calls == 0) return false;
  out_seg->t0_us = 0;
  out_seg->t1_us = 500000;
  out_seg->no_speech_prob = 0.0f;
  out_seg->text_utf8 = "stub speech";
  return true;
}

struct whisper_vad_context* vw_vad_init_default(const char* path_model) {
  (void)path_model;
  return NULL;
}

bool vw_vad_detect_speech(const float* pcm32, size_t sample_count, struct whisper_vad_context* vctx) {
  (void)pcm32;
  (void)vctx;
  return sample_count > 0;
}

bool vw_vad_detect_speech_energy(const float* pcm32, size_t sample_count, float threshold) {
  (void)pcm32;
  (void)threshold;
  return sample_count > 0;
}

bool vw_vad_find_chunk_boundary(const float* pcm32, size_t sample_count, struct whisper_vad_context* vctx, bool is_eof,
                                size_t* out_cut_samples, size_t* out_silence_drain) {
  (void)pcm32;
  (void)vctx;
  if (!out_cut_samples || !out_silence_drain) return false;
  *out_cut_samples = 0;
  *out_silence_drain = 0;
  if (sample_count == 0) return false;
  if (is_eof || sample_count >= VW_CHUNK_MIN_SAMPLES) {
    *out_cut_samples = sample_count > VW_CHUNK_MAX_SAMPLES ? VW_CHUNK_MAX_SAMPLES : sample_count;
    return true;
  }
  return false;
}

void vw_vad_reset_state(struct whisper_vad_context* vctx) { (void)vctx; }

void vw_vad_free(struct whisper_vad_context* vctx) { (void)vctx; }

vw_source_decoder_t* vw_source_decoder_open(const char* url, vw_source_decoder_info_t* info) {
  if (!url || !url[0]) return NULL;
  vw_source_decoder_t* decoder = (vw_source_decoder_t*)calloc(1, sizeof(*decoder));
  if (!decoder) return NULL;
  if (info) {
    info->duration_us = 60000000;
    info->sample_rate = 16000;
    info->channels = 1;
    snprintf(info->container_format, sizeof(info->container_format), "%s", "test-stub");
  }
  return decoder;
}

bool vw_source_decoder_seek(vw_source_decoder_t* decoder, int64_t target_pts_us) {
  if (!decoder || target_pts_us < 0) return false;
  decoder->pts_us = target_pts_us;
  decoder->emitted_data = false;
  return true;
}

vw_source_decoder_read_status_t vw_source_decoder_read_s16le(vw_source_decoder_t* decoder, int16_t* out_pcm,
                                                             size_t max_samples, size_t* out_sample_count,
                                                             int64_t* out_pts_us) {
  if (out_sample_count) *out_sample_count = 0;
  if (!decoder || !out_pcm || max_samples == 0 || !out_sample_count) return VW_SOURCE_DECODER_READ_ERROR;
  g_decoder_read_calls++;
  if (g_decoder_mode == VW_TEST_DECODER_ERROR) return VW_SOURCE_DECODER_READ_ERROR;
  if (g_decoder_mode == VW_TEST_DECODER_AGAIN_THEN_DATA && g_decoder_read_calls <= 5) {
    return VW_SOURCE_DECODER_READ_AGAIN;
  }
  if (decoder->emitted_data) return VW_SOURCE_DECODER_READ_EOF;

  size_t count = max_samples < 1600 ? max_samples : 1600;
  memset(out_pcm, 0, count * sizeof(*out_pcm));
  if (out_pts_us) *out_pts_us = decoder->pts_us;
  decoder->pts_us += (int64_t)((count * 1000000ULL) / 16000ULL);
  decoder->emitted_data = true;
  *out_sample_count = count;
  return VW_SOURCE_DECODER_READ_OK;
}

int64_t vw_source_decoder_get_duration_us(const vw_source_decoder_t* decoder) { return decoder ? 60000000 : -1; }

void vw_source_decoder_close(vw_source_decoder_t* decoder) { free(decoder); }
