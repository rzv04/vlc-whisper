#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_whisper_engine.h"

#define EXPECT(cond)                                                                 \
  do {                                                                               \
    if (!(cond)) {                                                                   \
      fprintf(stderr, "Assertion failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
      exit(1);                                                                       \
    }                                                                                \
  } while (0)

#if defined(__linux__)
// Detect Valgrind at runtime (Linux): Valgrind maps its preload libraries into the process.
// Under memcheck, loading the 77MB model + multi-threaded whisper inference takes minutes and
// whisper's GPU-less Vulkan fallback emits false-positive "invalid file descriptor -1" close()
// warnings, so the heavy model-gated section is skipped (exit 77). Native `ctest` still runs it.
static int running_under_valgrind(void) {
  FILE* f = fopen("/proc/self/maps", "r");
  if (!f) return 0;
  char line[512];
  int found = 0;
  while (fgets(line, sizeof(line), f)) {
    if (strstr(line, "vgpreload") != NULL) {
      found = 1;
      break;
    }
  }
  fclose(f);
  return found;
}
#else
static int running_under_valgrind(void) { return 0; }
#endif

// Minimal 16-bit PCM WAV loader (mono 16kHz expected); returns a float32 buffer or NULL when the
// file is absent/malformed (caller skips fixture-gated assertions).
static float* vw_test_load_wav_f32(const char* path, size_t* out_samples) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    return NULL;
  }
  uint8_t hdr[12];
  if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
    fclose(f);
    return NULL;
  }
  uint16_t channels = 0, bits = 0;
  uint32_t sample_rate = 0, data_off = 0, data_size = 0;
  uint8_t chunk_hdr[8];
  while (fread(chunk_hdr, 1, sizeof(chunk_hdr), f) == sizeof(chunk_hdr)) {
    uint32_t size = (uint32_t)chunk_hdr[4] | ((uint32_t)chunk_hdr[5] << 8) | ((uint32_t)chunk_hdr[6] << 16) |
                    ((uint32_t)chunk_hdr[7] << 24);
    if (memcmp(chunk_hdr, "fmt ", 4) == 0) {
      uint8_t fmt[16];
      if (size < 16 || fread(fmt, 1, 16, f) != 16) {
        fclose(f);
        return NULL;
      }
      channels = (uint16_t)(fmt[2] | (fmt[3] << 8));
      sample_rate = (uint32_t)fmt[4] | ((uint32_t)fmt[5] << 8) | ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
      bits = (uint16_t)(fmt[14] | (fmt[15] << 8));
      if (size > 16) {
        fseek(f, (long)(size - 16), SEEK_CUR);
      }
    } else if (memcmp(chunk_hdr, "data", 4) == 0) {
      data_off = (uint32_t)ftell(f);
      data_size = size;
      break;
    } else {
      fseek(f, (long)size, SEEK_CUR);
    }
  }
  if (channels != 1 || sample_rate != 16000 || bits != 16 || data_size == 0) {
    fclose(f);
    return NULL;
  }
  size_t frames = data_size / 2;
  int16_t* s16 = (int16_t*)malloc(frames * sizeof(int16_t));
  float* f32 = (float*)malloc(frames * sizeof(float));
  if (!s16 || !f32) {
    free(s16);
    free(f32);
    fclose(f);
    return NULL;
  }
  fseek(f, (long)data_off, SEEK_SET);
  if (fread(s16, 2, frames, f) != frames) {
    free(s16);
    free(f32);
    fclose(f);
    return NULL;
  }
  fclose(f);
  for (size_t i = 0; i < frames; i++) {
    f32[i] = (float)s16[i] / 32768.0f;
  }
  free(s16);
  *out_samples = frames;
  return f32;
}

int main(void) {
  // 1. Invalid or non-existent model path returns NULL cleanly
  vw_whisper_engine_t* null_eng = vw_whisper_engine_init("no_such_model_file.bin", VW_WORKER_BACKEND_CPU, 0);
  EXPECT(null_eng == NULL);

  // 1b. Segment accessors return safe defaults when engine is NULL
  EXPECT(vw_whisper_engine_get_segment_count(NULL) == 0);
  vw_whisper_segment_t dummy_seg = {0};
  EXPECT(vw_whisper_engine_get_segment(NULL, 0, &dummy_seg) == false);

  // 2. Check for model file in CWD or build parent directories
  const char* model_paths[] = {"models/ggml-tiny.en.bin", "../../../models/ggml-tiny.en.bin",
                               "../../models/ggml-tiny.en.bin", "../models/ggml-tiny.en.bin"};
  const char* model_path = NULL;
  for (size_t i = 0; i < sizeof(model_paths) / sizeof(model_paths[0]); i++) {
    FILE* f = fopen(model_paths[i], "rb");
    if (f) {
      fclose(f);
      model_path = model_paths[i];
      break;
    }
  }

  if (!model_path) {
    printf("Model file ggml-tiny.en.bin not present - skipping model-gated test (exit 77)\n");
    return 77;
  }

  // Running under memcheck/Valgrind: skip heavy model load + inference (exit 77) so that
  // `ctest -T memcheck` completes quickly; see running_under_valgrind() above.
  if (running_under_valgrind()) {
    printf("Running under Valgrind - skipping model-gated inference test (exit 77)\n");
    return 77;
  }

  // 3. Model present: test engine init, silent transcribe, get_text, and sub-segment getters
  vw_whisper_engine_t* eng = vw_whisper_engine_init(model_path, VW_WORKER_BACKEND_CPU, 0);
  EXPECT(eng != NULL);

  // Test bounds before transcription
  EXPECT(vw_whisper_engine_get_segment(eng, -1, &dummy_seg) == false);
  EXPECT(vw_whisper_engine_get_segment(eng, 0, NULL) == false);
  EXPECT(vw_whisper_engine_get_segment(eng, 999, &dummy_seg) == false);

  float pcm[16000] = {0};
  EXPECT(vw_whisper_engine_transcribe_pcm(eng, pcm, 16000));
  const char* text = vw_whisper_engine_get_text(eng);
  EXPECT(text != NULL);

  int seg_count = vw_whisper_engine_get_segment_count(eng);
  EXPECT(seg_count >= 0);
  for (int i = 0; i < seg_count; i++) {
    vw_whisper_segment_t seg = {0};
    EXPECT(vw_whisper_engine_get_segment(eng, i, &seg));
    EXPECT(seg.t0_us >= 0);
    EXPECT(seg.t1_us >= seg.t0_us);
    EXPECT(seg.text_utf8 != NULL);
  }

  // 4. Fixture-gated regression: token-level timing must be AUTHENTIC. whisper.cpp only computes
  // per-token t0/t1 when params.token_timestamps is enabled (default false); without it the token
  // count getter reports 0 and the builder would fall back to whole-phrase dedup, losing suffix
  // cues. Assert a speech segment exposes tokens with non-zero timing.
  size_t n_speech = 0;
  const char* fixture_paths[] = {"worker/third_party/whisper.cpp/samples/jfk.wav",
                                 "../worker/third_party/whisper.cpp/samples/jfk.wav",
                                 "../../worker/third_party/whisper.cpp/samples/jfk.wav",
                                 "../../../worker/third_party/whisper.cpp/samples/jfk.wav"};
  float* speech = NULL;
  for (size_t p = 0; p < sizeof(fixture_paths) / sizeof(fixture_paths[0]); p++) {
    speech = vw_test_load_wav_f32(fixture_paths[p], &n_speech);
    if (speech != NULL) {
      break;
    }
  }
  if (speech != NULL) {
    size_t take = n_speech < 80000 ? n_speech : 80000;  // first 5s
    EXPECT(vw_whisper_engine_transcribe_pcm(eng, speech, take));
    int n_segs = vw_whisper_engine_get_segment_count(eng);
    for (int i = 0; i < n_segs && i < 2; i++) {
      int n_tok = vw_whisper_engine_get_segment_token_count(eng, i);
      EXPECT(n_tok > 0);  // fails if token_timestamps is disabled (count guard returns 0)
      if (n_tok > 0) {
        vw_whisper_token_t tok = {0};
        EXPECT(vw_whisper_engine_get_segment_token(eng, i, n_tok - 1, &tok));
        EXPECT(tok.t1_us > 0);  // authentic spoken boundary, not zero
        EXPECT(tok.t1_us >= tok.t0_us);
      }
    }
    free(speech);
  } else {
    printf("WAV fixture jfk.wav not present - skipping token-timing regression\n");
  }

  vw_whisper_engine_free(eng);
  printf("test_whisper_engine PASSED\n");
  return 0;
}
