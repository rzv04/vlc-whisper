#include "vw_whisper_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ggml-backend.h"
#include "vw_log.h"
#include "vw_platform.h"
#include "whisper.h"

static void vw_whisper_log_callback(enum ggml_log_level level, const char* text, void* user_data) {
  (void)user_data;
  vw_log_level_t mapped;
  switch (level) {
    case GGML_LOG_LEVEL_ERROR:
      mapped = VW_LOG_LEVEL_ERROR;
      break;
    case GGML_LOG_LEVEL_WARN:
      mapped = VW_LOG_LEVEL_WARN;
      break;
    case GGML_LOG_LEVEL_INFO:
    case GGML_LOG_LEVEL_CONT:
      mapped = VW_LOG_LEVEL_INFO;
      break;
    case GGML_LOG_LEVEL_DEBUG:
      mapped = VW_LOG_LEVEL_DEBUG;
      break;
    default:
      return;
  }
  vw_log_event(mapped, "WHISPER", "%s", text ? text : "");
}

vw_whisper_engine_t* vw_whisper_engine_init(const char* model_path, vw_worker_backend_t backend, int gpu_device) {
  if (!model_path || model_path[0] == '\0') return NULL;

  whisper_log_set(vw_whisper_log_callback, NULL);
  struct whisper_context_params cparams = whisper_context_default_params();
  cparams.use_gpu = (backend != VW_WORKER_BACKEND_CPU);
  // Clamps negative gpu_device to 0 (CLI rejects <0, this guards direct API callers).
  cparams.gpu_device = (gpu_device >= 0) ? gpu_device : 0;
  struct whisper_context* ctx = whisper_init_from_file_with_params(model_path, cparams);
  if (!ctx) {
    return NULL;
  }
  // Runtime backend truth: mirror whisper's own GPU selection (whisper_backend_init_gpu walks
  // ggml's registered devices and picks the gpu_device-th GPU/IGPU; "no GPU found" degrades to
  // CPU while still returning a valid context). Re-derive that decision so STATUS can report the
  // backend actually used instead of the one requested. Must run AFTER whisper init: backend
  // registration happens during library init, so a pre-init probe would see no devices.
  // Additionally verify the device can actually be initialized (not just enumerated), so a
  // present but unloadable Vulkan driver correctly reports CPU fallback.
  bool gpu_active = false;
  if (cparams.use_gpu) {
    int remaining = cparams.gpu_device;
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
      ggml_backend_dev_t dev = ggml_backend_dev_get(i);
      enum ggml_backend_dev_type dev_type = ggml_backend_dev_type(dev);
      if (dev_type == GGML_BACKEND_DEVICE_TYPE_GPU || dev_type == GGML_BACKEND_DEVICE_TYPE_IGPU) {
        if (remaining == 0) {
          // Verify backend can actually be initialized; enumeration alone can be stale
          // (e.g., Vulkan present but loader/driver missing at runtime).
          ggml_backend_t be = ggml_backend_dev_init(dev, NULL);
          if (be) {
            ggml_backend_free(be);
            gpu_active = true;
          } else {
            gpu_active = false;
          }
          break;
        }
        remaining--;
      }
    }
  }
  vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_ENGINE",
               cparams.use_gpu ? (gpu_active ? "inference backend: gpu"
                                             : "inference backend: gpu REQUESTED but no usable device; running cpu")
                               : "inference backend: cpu");
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
  // Default language/threads (overridable via setters from config). Clamp threads 1..16.
  snprintf(eng->language, sizeof(eng->language), "en");
  eng->n_threads = 4;
  eng->gpu_active = gpu_active;

  // Perform one silent warmup pass on 100ms of zeros (uses configured language/threads)
  float silent[1600] = {0};
  struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  wparams.print_progress = false;
  wparams.print_special = false;
  wparams.print_realtime = false;
  wparams.print_timestamps = false;
  wparams.translate = false;
  wparams.language = eng->language;
  wparams.n_threads = eng->n_threads;
  whisper_full(eng->ctx, wparams, silent, 1600);

  return eng;
}

bool vw_whisper_engine_is_gpu_active(const vw_whisper_engine_t* engine) { return engine && engine->gpu_active; }

bool vw_whisper_engine_set_language(vw_whisper_engine_t* engine, const char* language) {
  if (!engine || !language || language[0] == '\0') return false;
  if (strcmp(language, "auto") == 0) return false;
  if (strlen(language) >= sizeof(engine->language)) return false;
  if (whisper_lang_id(language) < 0) return false;
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
  wparams.strategy = WHISPER_SAMPLING_GREEDY;
  wparams.temperature = 0.0f;
  wparams.temperature_inc =
      0.2f;  // Explicit bounded temperature fallback (initial pass + <= 5 retries, up to 6 total passes)
  wparams.entropy_thold = 2.40f;  // Shannon entropy gate over last 32 tokens
  wparams.logprob_thold = -1.00f;
  wparams.no_speech_thold = 0.60f;
  wparams.no_context = true;       // Disables within-window segment prompt conditioning
  wparams.single_segment = false;  // Emits discrete sub-segments for phrase-by-phrase timing
  wparams.suppress_blank = true;
  wparams.suppress_nst = true;  // Suppresses non-speech sound tokens at logit level
  wparams.print_special = false;
  wparams.max_len = 0;  // Natural transformer acoustic boundaries
  wparams.token_timestamps = false;
  wparams.translate = false;
  // Use configured language/threads (defaults "en"/4 if not set via setters).
  wparams.language = (engine->language[0] != '\0') ? engine->language : "en";
  int thr = engine->n_threads;
  if (thr < 1) thr = 1;
  wparams.n_threads = thr;
  wparams.print_progress = false;
  wparams.print_realtime = false;
  wparams.print_timestamps = false;
  int64_t inference_started_us = vw_platform_get_monotonic_time_us();
  int inference_result = whisper_full(engine->ctx, wparams, pcm32, (int)sample_count);
  int64_t inference_elapsed_us = vw_platform_get_monotonic_time_us() - inference_started_us;
  if (inference_elapsed_us < 0) inference_elapsed_us = 0;
  engine->last_inference_us = (uint64_t)inference_elapsed_us;
  if (UINT64_MAX - engine->total_inference_us < (uint64_t)inference_elapsed_us) {
    engine->total_inference_us = UINT64_MAX;
  } else {
    engine->total_inference_us += (uint64_t)inference_elapsed_us;
  }
  if (inference_result != 0) {
    return false;
  }

  engine->last_text[0] = '\0';
  size_t written = 0;
  int n_segments = whisper_full_n_segments(engine->ctx);
  for (int i = 0; i < n_segments; i++) {
    const char* txt = whisper_full_get_segment_text(engine->ctx, i);
    if (!txt) continue;
    size_t len = strlen(txt);
    if (len == 0) continue;
    bool needs_space = (written > 0 && engine->last_text[written - 1] != ' ' && txt[0] != ' ');
    size_t extra = needs_space ? 1 : 0;
    if (written + len + extra + 2 >= engine->last_text_bytes) {
      size_t new_cap = engine->last_text_bytes * 2 + len + extra + 2;
      char* new_buf = (char*)realloc(engine->last_text, new_cap);
      if (!new_buf) return false;  // return false instead of shipping truncated text
      engine->last_text = new_buf;
      engine->last_text_bytes = new_cap;
    }
    if (needs_space) {
      engine->last_text[written++] = ' ';
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

int vw_whisper_engine_get_segment_count(const vw_whisper_engine_t* engine) {
  if (!engine || !engine->ctx) {
    return 0;
  }
  return whisper_full_n_segments(engine->ctx);
}

uint64_t vw_whisper_engine_get_total_inference_us(const vw_whisper_engine_t* engine) {
  return engine ? engine->total_inference_us : 0;
}

bool vw_whisper_engine_get_segment(const vw_whisper_engine_t* engine, int index, vw_whisper_segment_t* out_seg) {
  if (!engine || !engine->ctx || !out_seg || index < 0) {
    return false;
  }
  int count = whisper_full_n_segments(engine->ctx);
  if (index >= count) {
    return false;
  }

  int64_t t0 = whisper_full_get_segment_t0(engine->ctx, index);
  int64_t t1 = whisper_full_get_segment_t1(engine->ctx, index);
  const char* txt = whisper_full_get_segment_text(engine->ctx, index);
  float nsp = whisper_full_get_segment_no_speech_prob(engine->ctx, index);

  out_seg->t0_us = t0 * 10000LL;
  out_seg->t1_us = t1 * 10000LL;
  out_seg->no_speech_prob = nsp;
  out_seg->text_utf8 = txt ? txt : "";
  return true;
}
