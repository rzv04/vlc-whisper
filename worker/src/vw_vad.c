#include "vw_vad.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <stdio.h>
#include <windows.h>

typedef struct {
  FILE* file;
} vw_vad_file_loader_t;

static size_t vw_vad_file_read(void* context, void* output, size_t read_size) {
  vw_vad_file_loader_t* loader = (vw_vad_file_loader_t*)context;
  return loader && loader->file ? fread(output, 1, read_size, loader->file) : 0;
}

static bool vw_vad_file_eof(void* context) {
  vw_vad_file_loader_t* loader = (vw_vad_file_loader_t*)context;
  return !loader || !loader->file || feof(loader->file) != 0;
}

static void vw_vad_file_close(void* context) {
  vw_vad_file_loader_t* loader = (vw_vad_file_loader_t*)context;
  if (!loader) return;
  if (loader->file) fclose(loader->file);
  free(loader);
}

static struct whisper_vad_context* vw_vad_init_utf8_path(const char* path_model,
                                                         struct whisper_vad_context_params params) {
  int wide_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path_model, -1, NULL, 0);
  if (wide_length <= 0) return NULL;
  wchar_t* wide_path = (wchar_t*)calloc((size_t)wide_length, sizeof(wchar_t));
  if (!wide_path) return NULL;
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path_model, -1, wide_path, wide_length) <= 0) {
    free(wide_path);
    return NULL;
  }
  vw_vad_file_loader_t* context = (vw_vad_file_loader_t*)calloc(1, sizeof(*context));
  if (context) context->file = _wfopen(wide_path, L"rb");
  free(wide_path);
  if (!context || !context->file) {
    free(context);
    return NULL;
  }
  struct whisper_model_loader loader = {context, vw_vad_file_read, vw_vad_file_eof, vw_vad_file_close};
  struct whisper_vad_context* vad = whisper_vad_init_with_params(&loader, params);
  // The VAD loader API consumes synchronously but does not invoke close().
  vw_vad_file_close(context);
  return vad;
}
#endif

struct whisper_vad_context* vw_vad_init_default(const char* path_model) {
  if (path_model == NULL || path_model[0] == '\0') {
    return NULL;
  }

  struct whisper_vad_context_params vad_params = whisper_vad_default_context_params();
  const char* force_cpu = getenv("VW_FORCE_CPU");
  if (force_cpu && strcmp(force_cpu, "1") == 0) {
    vad_params.use_gpu = false;
  }
  struct whisper_vad_context* vctx;
#ifdef _WIN32
  vctx = vw_vad_init_utf8_path(path_model, vad_params);
#else
  vctx = whisper_vad_init_from_file_with_params(path_model, vad_params);
#endif
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
    if (!whisper_vad_detect_speech(vctx, pcm32, (int)sample_count)) {
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

bool vw_vad_find_chunk_boundary(const float* pcm32, size_t sample_count, struct whisper_vad_context* vctx, bool is_eof,
                                size_t* out_cut_samples, size_t* out_silence_drain) {
  if (out_cut_samples) *out_cut_samples = 0;
  if (out_silence_drain) *out_silence_drain = 0;
  if (pcm32 == NULL || sample_count == 0 || out_cut_samples == NULL || out_silence_drain == NULL) {
    return false;
  }

  // 1. Silero ML VAD Path (resets LSTM state at window start for deterministic evaluation)
  if (vctx != NULL) {
    if (!whisper_vad_detect_speech(vctx, pcm32, (int)sample_count)) {
      goto energy_fallback;
    }

    struct whisper_vad_params vad_params = whisper_vad_default_params();
    struct whisper_vad_segments* segments = whisper_vad_segments_from_probs(vctx, vad_params);
    if (segments == NULL) {
      goto energy_fallback;
    }

    int n_segs = whisper_vad_segments_n_segments(segments);

    // Case A: Pure silence / no speech detected (M1: progressive silence drain once min chunk size reached)
    if (n_segs == 0) {
      whisper_vad_free_segments(segments);
      if (sample_count >= VW_CHUNK_MIN_SAMPLES || is_eof) {
        // Drain confirmed silence run, retaining 150ms padding at the trailing edge unless at EOF
        *out_silence_drain =
            (is_eof || sample_count <= VW_CHUNK_PAD_SAMPLES) ? sample_count : (sample_count - VW_CHUNK_PAD_SAMPLES);
        return true;
      }
      return false;  // Wait for min chunk accumulation before evaluating silence
    }

    // Case B: Leading Silence Check before first speech segment (> 1.0s silence)
    float t0_cs = whisper_vad_segments_get_segment_t0(segments, 0);
    size_t first_speech_sample = (size_t)(t0_cs * 160.0f);
    if (first_speech_sample > 16000 + VW_CHUNK_PAD_SAMPLES) {
      size_t leading_silence = first_speech_sample - VW_CHUNK_PAD_SAMPLES;
      whisper_vad_free_segments(segments);
      *out_silence_drain = leading_silence;
      return true;
    }

    // Case C: Find optimal silence cut point between VW_CHUNK_MIN_SAMPLES and VW_CHUNK_MAX_SAMPLES
    size_t chosen_cut = 0;
    for (int i = 0; i < n_segs; i++) {
      float seg_t1_cs = whisper_vad_segments_get_segment_t1(segments, i);
      size_t raw_seg_end = (size_t)(seg_t1_cs * 160.0f);
      size_t seg_end_sample = raw_seg_end + VW_CHUNK_PAD_SAMPLES;

      if (seg_end_sample >= VW_CHUNK_MIN_SAMPLES) {
        if (seg_end_sample <= VW_CHUNK_MAX_SAMPLES) {
          if (i + 1 < n_segs) {
            float next_t0_cs = whisper_vad_segments_get_segment_t0(segments, i + 1);
            size_t next_start_sample = (size_t)(next_t0_cs * 160.0f);
            if (next_start_sample >= raw_seg_end + VW_CHUNK_MIN_SILENCE_GAP) {
              size_t next_padded_start = (next_start_sample > VW_CHUNK_PAD_SAMPLES)
                                             ? (next_start_sample - VW_CHUNK_PAD_SAMPLES)
                                             : next_start_sample;
              chosen_cut = (seg_end_sample + next_padded_start) / 2;
              if (chosen_cut < seg_end_sample) chosen_cut = seg_end_sample;
              break;
            }
          } else {
            if (sample_count >= raw_seg_end + VW_CHUNK_MIN_SILENCE_GAP || is_eof) {
              chosen_cut = (seg_end_sample + sample_count) / 2;
              if (chosen_cut > sample_count) chosen_cut = sample_count;
              if (chosen_cut < seg_end_sample) chosen_cut = seg_end_sample;
              break;
            }
          }
        }
      }
    }

    // If no clean mid-gap was found but buffer has reached or exceeded max chunk size:
    if (chosen_cut == 0 && sample_count >= VW_CHUNK_MAX_SAMPLES) {
      for (int i = n_segs - 1; i >= 0; i--) {
        float seg_t1_cs = whisper_vad_segments_get_segment_t1(segments, i);
        size_t seg_end = (size_t)(seg_t1_cs * 160.0f) + VW_CHUNK_PAD_SAMPLES;
        if (seg_end <= VW_CHUNK_MAX_SAMPLES && seg_end >= VW_CHUNK_MIN_SAMPLES) {
          chosen_cut = seg_end;
          break;
        }
      }
      if (chosen_cut == 0) {
        chosen_cut = VW_CHUNK_MAX_SAMPLES;
      }
    } else if (chosen_cut == 0 && is_eof) {
      chosen_cut = sample_count;
    }

    whisper_vad_free_segments(segments);

    if (chosen_cut > 0) {
      if (chosen_cut > sample_count) chosen_cut = sample_count;
      *out_cut_samples = chosen_cut;
      return true;
    }

    return false;
  }

energy_fallback:
  // 2. RMS Energy Fallback Path
  if (!vw_vad_detect_speech_energy(pcm32, sample_count, VW_VAD_ENERGY_THRESHOLD)) {
    if (sample_count >= VW_CHUNK_MIN_SAMPLES || is_eof) {
      *out_silence_drain =
          (is_eof || sample_count <= VW_CHUNK_PAD_SAMPLES) ? sample_count : (sample_count - VW_CHUNK_PAD_SAMPLES);
      return true;
    }
    return false;
  }

  // Energy detected speech: chunk at fixed contiguous intervals (e.g. 12s)
  size_t target_samples = (VW_CHUNK_MIN_SAMPLES + VW_CHUNK_MAX_SAMPLES) / 2;
  if (sample_count >= target_samples || is_eof) {
    *out_cut_samples = (sample_count > target_samples) ? target_samples : sample_count;
    return true;
  }

  return false;
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
