#include "vw_asr_engine.h"

#include <stdlib.h>
#include <string.h>

#include "vw_whisper_engine.h"

struct vw_asr_engine {
  vw_asr_engine_kind_t kind;
  vw_whisper_engine_t* whisper;
};

static const vw_asr_engine_descriptor_t k_whisper_descriptor = {
    .kind = VW_ASR_ENGINE_WHISPER,
    .id = "whisper",
    .capabilities = VW_ASR_CAP_LOCAL | VW_ASR_CAP_FINAL_ONLY | VW_ASR_CAP_SOURCE_LOOKAHEAD | VW_ASR_CAP_CPU |
                    VW_ASR_CAP_VULKAN,
    .available = true,
};

static const vw_asr_engine_descriptor_t k_nemotron_descriptor = {
    .kind = VW_ASR_ENGINE_NEMOTRON,
    .id = "nemotron",
    .capabilities = VW_ASR_CAP_LOCAL | VW_ASR_CAP_NATIVE_STREAMING | VW_ASR_CAP_PARTIAL_RESULTS | VW_ASR_CAP_CPU |
                    VW_ASR_CAP_VULKAN,
    .available = false,
};

bool vw_asr_engine_kind_from_id(const char* id, vw_asr_engine_kind_t* out_kind) {
  if (!id || !id[0] || !out_kind) return false;
  if (strcmp(id, "whisper") == 0) {
    *out_kind = VW_ASR_ENGINE_WHISPER;
    return true;
  }
  if (strcmp(id, "nemotron") == 0) {
    *out_kind = VW_ASR_ENGINE_NEMOTRON;
    return true;
  }
  return false;
}

const vw_asr_engine_descriptor_t* vw_asr_engine_get_descriptor(vw_asr_engine_kind_t kind) {
  switch (kind) {
    case VW_ASR_ENGINE_WHISPER:
      return &k_whisper_descriptor;
    case VW_ASR_ENGINE_NEMOTRON:
      return &k_nemotron_descriptor;
    default:
      return NULL;
  }
}

vw_asr_engine_t* vw_asr_engine_create(const vw_asr_engine_config_t* config) {
  if (!config || !config->model_path || !config->model_path[0]) return NULL;
  const vw_asr_engine_descriptor_t* descriptor = vw_asr_engine_get_descriptor(config->kind);
  if (!descriptor || !descriptor->available) return NULL;

  vw_asr_engine_t* engine = (vw_asr_engine_t*)calloc(1, sizeof(vw_asr_engine_t));
  if (!engine) return NULL;
  engine->kind = config->kind;

  switch (config->kind) {
    case VW_ASR_ENGINE_WHISPER:
      engine->whisper = vw_whisper_engine_init(config->model_path, config->backend, config->gpu_device);
      if (!engine->whisper ||
          !vw_whisper_engine_set_language(engine->whisper,
                                          (config->language && config->language[0]) ? config->language : "en") ||
          !vw_whisper_engine_set_n_threads(engine->whisper, config->n_threads)) {
        vw_asr_engine_free(engine);
        return NULL;
      }
      return engine;
    case VW_ASR_ENGINE_NEMOTRON:
    default:
      free(engine);
      return NULL;
  }
}

void vw_asr_engine_free(vw_asr_engine_t* engine) {
  if (!engine) return;
  if (engine->whisper) vw_whisper_engine_free(engine->whisper);
  free(engine);
}

bool vw_asr_engine_transcribe_pcm(vw_asr_engine_t* engine, const float* pcm32, size_t sample_count) {
  if (!engine) return false;
  switch (engine->kind) {
    case VW_ASR_ENGINE_WHISPER:
      return vw_whisper_engine_transcribe_pcm(engine->whisper, pcm32, sample_count);
    case VW_ASR_ENGINE_NEMOTRON:
    default:
      return false;
  }
}

int vw_asr_engine_get_result_count(const vw_asr_engine_t* engine) {
  if (!engine) return 0;
  switch (engine->kind) {
    case VW_ASR_ENGINE_WHISPER:
      return vw_whisper_engine_get_segment_count(engine->whisper);
    case VW_ASR_ENGINE_NEMOTRON:
    default:
      return 0;
  }
}

bool vw_asr_engine_get_result(const vw_asr_engine_t* engine, int index, vw_asr_result_t* out_result) {
  if (!engine || !out_result) return false;
  switch (engine->kind) {
    case VW_ASR_ENGINE_WHISPER: {
      vw_whisper_segment_t whisper_result;
      if (!vw_whisper_engine_get_segment(engine->whisper, index, &whisper_result)) return false;
      out_result->start_offset_us = whisper_result.t0_us;
      out_result->end_offset_us = whisper_result.t1_us;
      out_result->is_final = true;
      out_result->no_speech_prob = whisper_result.no_speech_prob;
      out_result->text_utf8 = whisper_result.text_utf8;
      return true;
    }
    case VW_ASR_ENGINE_NEMOTRON:
    default:
      return false;
  }
}

uint64_t vw_asr_engine_get_total_inference_us(const vw_asr_engine_t* engine) {
  if (!engine) return 0;
  switch (engine->kind) {
    case VW_ASR_ENGINE_WHISPER:
      return vw_whisper_engine_get_total_inference_us(engine->whisper);
    case VW_ASR_ENGINE_NEMOTRON:
    default:
      return 0;
  }
}

bool vw_asr_engine_is_gpu_active(const vw_asr_engine_t* engine) {
  if (!engine) return false;
  switch (engine->kind) {
    case VW_ASR_ENGINE_WHISPER:
      return vw_whisper_engine_is_gpu_active(engine->whisper);
    case VW_ASR_ENGINE_NEMOTRON:
    default:
      return false;
  }
}
