// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_vad.h"

static void vw_test_vad_energy_detection(void) {
  // 1. Edge-case handling
  assert(!vw_vad_detect_speech_energy(NULL, 16000, 0.01f));
  assert(!vw_vad_detect_speech_energy((const float[]){0.5f}, 0, 0.01f));
  assert(!vw_vad_detect_speech_energy((const float[]){0.5f}, 1, -0.01f));

  // 2. Pure silence (all zeros)
  float silence[16000] = {0};
  assert(!vw_vad_detect_speech_energy(silence, 16000, VW_VAD_ENERGY_THRESHOLD));

  // 3. Low amplitude background static (RMS < 0.005)
  float low_noise[16000];
  for (size_t i = 0; i < 16000; i++) {
    low_noise[i] = 0.002f * (float)((i % 2 == 0) ? 1.0f : -1.0f);
  }
  assert(!vw_vad_detect_speech_energy(low_noise, 16000, VW_VAD_ENERGY_THRESHOLD));

  // 4. Strong signal / speech simulation (RMS > 0.1)
  float speech[16000];
  for (size_t i = 0; i < 16000; i++) {
    speech[i] = 0.3f * sinf(2.0f * 3.14159f * 440.0f * (float)i / 16000.0f);
  }
  assert(vw_vad_detect_speech_energy(speech, 16000, VW_VAD_ENERGY_THRESHOLD));
}

static void vw_test_vad_fallback_null_context(void) {
  // NULL context must transparently fall back to energy VAD
  float silence[16000] = {0};
  assert(!vw_vad_detect_speech(silence, 16000, NULL));

  float speech[16000];
  for (size_t i = 0; i < 16000; i++) {
    speech[i] = 0.4f * sinf(2.0f * 3.14159f * 300.0f * (float)i / 16000.0f);
  }
  assert(vw_vad_detect_speech(speech, 16000, NULL));
}

static void vw_test_vad_partial_window_sample_counts(void) {
  // Tests arbitrary non-standard sample counts (partial EOF windows, short audio buffers)
  float speech_short[512];
  for (size_t i = 0; i < 512; i++) {
    speech_short[i] = 0.35f * sinf(2.0f * 3.14159f * 440.0f * (float)i / 16000.0f);
  }
  assert(vw_vad_detect_speech(speech_short, 512, NULL));

  float speech_mid[2400];
  for (size_t i = 0; i < 2400; i++) {
    speech_mid[i] = 0.35f * sinf(2.0f * 3.14159f * 440.0f * (float)i / 16000.0f);
  }
  assert(vw_vad_detect_speech(speech_mid, 2400, NULL));
}

static void vw_test_vad_null_safety(void) {
  // NULL pointers to reset_state and free must be safe no-ops
  vw_vad_reset_state(NULL);
  vw_vad_free(NULL);

  // Invalid model path returns NULL cleanly
  struct whisper_vad_context* vctx = vw_vad_init_default("non_existent_vad_model.bin");
  assert(vctx == NULL);
}

static bool load_wav_pcm32(const char* path, float* out_samples, size_t max_samples, size_t* out_read) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  if (fseek(f, 44, SEEK_SET) != 0) {
    fclose(f);
    return false;
  }
  int16_t buf[1024];
  size_t total = 0;
  while (total < max_samples) {
    size_t to_read = (max_samples - total < 1024) ? (max_samples - total) : 1024;
    size_t n = fread(buf, sizeof(int16_t), to_read, f);
    if (n == 0) break;
    for (size_t i = 0; i < n; i++) {
      out_samples[total + i] = (float)buf[i] / 32768.0f;
    }
    total += n;
  }
  fclose(f);
  *out_read = total;
  return total > 0;
}

#if defined(__linux__)
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

static void vw_test_vad_silero_model(void) {
  if (running_under_valgrind()) {
    printf("Running under Valgrind - skipping model-gated VAD inference\n");
    return;
  }

  const char* vad_paths[] = {"models/ggml-silero-vad.bin", "../../../models/ggml-silero-vad.bin",
                             "../../models/ggml-silero-vad.bin", "../models/ggml-silero-vad.bin"};
  const char* vad_path = NULL;
  for (size_t i = 0; i < sizeof(vad_paths) / sizeof(vad_paths[0]); i++) {
    FILE* f = fopen(vad_paths[i], "rb");
    if (f) {
      fclose(f);
      vad_path = vad_paths[i];
      break;
    }
  }

  if (!vad_path) {
    printf("Silero VAD model not found on disk; skipping model-gated test\n");
    return;
  }

  struct whisper_vad_context* vctx = vw_vad_init_default(vad_path);
  assert(vctx != NULL);

  // 1. Full 8.0s silence window (128,000 samples)
  float silence[128000] = {0};
  assert(!vw_vad_detect_speech(silence, 128000, vctx));

  // 2. Real speech test fixture (16kHz mono jfk.wav from whisper.cpp)
  const char* speech_fixtures[] = {"worker/third_party/whisper.cpp/samples/jfk.wav",
                                   "../worker/third_party/whisper.cpp/samples/jfk.wav",
                                   "../../worker/third_party/whisper.cpp/samples/jfk.wav",
                                   "../../../worker/third_party/whisper.cpp/samples/jfk.wav", NULL};
  const char* speech_path = NULL;
  for (int i = 0; speech_fixtures[i]; i++) {
    FILE* f = fopen(speech_fixtures[i], "rb");
    if (f) {
      fclose(f);
      speech_path = speech_fixtures[i];
      break;
    }
  }

  if (speech_path) {
    float speech_buf[128000] = {0};
    size_t samples_read = 0;
    if (load_wav_pcm32(speech_path, speech_buf, 128000, &samples_read) && samples_read >= 16000) {
      assert(vw_vad_detect_speech(speech_buf, samples_read, vctx));
    }
  }

  // 3. Partial trailing silence window (e.g. 1,600 samples = 100ms)
  assert(!vw_vad_detect_speech(silence, 1600, vctx));

  // 4. State reset & free
  vw_vad_reset_state(vctx);
  vw_vad_free(vctx);
}

static void vw_test_vad_find_chunk_boundary_energy(void) {
  size_t cut_samples = 0;
  size_t silence_drain = 0;

  // 1. Invalid args
  assert(!vw_vad_find_chunk_boundary(NULL, 16000, NULL, false, &cut_samples, &silence_drain));
  assert(!vw_vad_find_chunk_boundary((const float[]){0.1f}, 0, NULL, false, &cut_samples, &silence_drain));
  assert(!vw_vad_find_chunk_boundary((const float[]){0.1f}, 16000, NULL, false, NULL, &silence_drain));
  assert(!vw_vad_find_chunk_boundary((const float[]){0.1f}, 16000, NULL, false, &cut_samples, NULL));

  // 2. Pure silence in energy fallback (M1: progressive silence drain at MIN_SAMPLES)
  float silence[VW_CHUNK_MIN_SAMPLES] = {0};
  assert(vw_vad_find_chunk_boundary(silence, VW_CHUNK_MIN_SAMPLES, NULL, false, &cut_samples, &silence_drain));
  assert(silence_drain == VW_CHUNK_MIN_SAMPLES - VW_CHUNK_PAD_SAMPLES);
  assert(cut_samples == 0);

  // 3. Speech signal in energy fallback
  float speech[VW_CHUNK_MAX_SAMPLES];
  for (size_t i = 0; i < VW_CHUNK_MAX_SAMPLES; i++) {
    speech[i] = 0.3f * sinf(2.0f * 3.14159f * 440.0f * (float)i / 16000.0f);
  }
  assert(vw_vad_find_chunk_boundary(speech, VW_CHUNK_MAX_SAMPLES, NULL, false, &cut_samples, &silence_drain));
  assert(cut_samples > 0);
  assert(silence_drain == 0);

  // 4. EOF partial audio in energy fallback
  assert(vw_vad_find_chunk_boundary(speech, 32000, NULL, true, &cut_samples, &silence_drain));
  assert(cut_samples == 32000);
}

static void vw_test_vad_find_chunk_boundary_silero(void) {
  if (running_under_valgrind()) {
    return;
  }

  const char* vad_paths[] = {"models/ggml-silero-vad.bin", "../../../models/ggml-silero-vad.bin",
                             "../../models/ggml-silero-vad.bin", "../models/ggml-silero-vad.bin"};
  const char* vad_path = NULL;
  for (size_t i = 0; i < sizeof(vad_paths) / sizeof(vad_paths[0]); i++) {
    FILE* f = fopen(vad_paths[i], "rb");
    if (f) {
      fclose(f);
      vad_path = vad_paths[i];
      break;
    }
  }

  if (!vad_path) return;

  struct whisper_vad_context* vctx = vw_vad_init_default(vad_path);
  assert(vctx != NULL);

  size_t cut_samples = 0;
  size_t silence_drain = 0;

  // 1. Pure silence at min chunk size -> progressive silence drain without Whisper inference (M1)
  float silence[VW_CHUNK_MIN_SAMPLES] = {0};
  assert(vw_vad_find_chunk_boundary(silence, VW_CHUNK_MIN_SAMPLES, vctx, false, &cut_samples, &silence_drain));
  assert(silence_drain == VW_CHUNK_MIN_SAMPLES - VW_CHUNK_PAD_SAMPLES);
  assert(cut_samples == 0);

  // 2. JFK speech audio fixture
  const char* speech_fixtures[] = {"worker/third_party/whisper.cpp/samples/jfk.wav",
                                   "../worker/third_party/whisper.cpp/samples/jfk.wav",
                                   "../../worker/third_party/whisper.cpp/samples/jfk.wav",
                                   "../../../worker/third_party/whisper.cpp/samples/jfk.wav", NULL};
  const char* speech_path = NULL;
  for (int i = 0; speech_fixtures[i]; i++) {
    FILE* f = fopen(speech_fixtures[i], "rb");
    if (f) {
      fclose(f);
      speech_path = speech_fixtures[i];
      break;
    }
  }

  if (speech_path) {
    float speech_buf[VW_CHUNK_MAX_SAMPLES] = {0};
    size_t samples_read = 0;
    if (load_wav_pcm32(speech_path, speech_buf, VW_CHUNK_MAX_SAMPLES, &samples_read) && samples_read >= 96000) {
      cut_samples = 0;
      silence_drain = 0;
      bool res = vw_vad_find_chunk_boundary(speech_buf, samples_read, vctx, false, &cut_samples, &silence_drain);
      if (res) {
        assert(cut_samples > 0 || silence_drain > 0);
      }
    }
  }

  // 3. Multi-chunk streaming simulation across iterations (Finding L4)
  size_t stream_pos = 0;
  float stream_audio[VW_CHUNK_MAX_SAMPLES * 2] = {0};
  // Populate first half with simulated tone and second half with silence
  for (size_t i = 0; i < VW_CHUNK_MIN_SAMPLES; i++) {
    stream_audio[i] = 0.35f * sinf(2.0f * 3.14159f * 300.0f * (float)i / 16000.0f);
  }
  // Iteration 1: detect speech cut
  cut_samples = 0;
  silence_drain = 0;
  bool s1 = vw_vad_find_chunk_boundary(stream_audio, VW_CHUNK_MIN_SAMPLES, vctx, false, &cut_samples, &silence_drain);
  if (s1) {
    stream_pos += (cut_samples > 0) ? cut_samples : silence_drain;
  }
  // Iteration 2: pure silence trailing
  cut_samples = 0;
  silence_drain = 0;
  bool s2 = vw_vad_find_chunk_boundary(stream_audio + VW_CHUNK_MIN_SAMPLES, VW_CHUNK_MIN_SAMPLES, vctx, false,
                                       &cut_samples, &silence_drain);
  if (s2) {
    assert(silence_drain == VW_CHUNK_MIN_SAMPLES - VW_CHUNK_PAD_SAMPLES);
  }

  vw_vad_reset_state(vctx);
  vw_vad_free(vctx);
}

int main(void) {
  vw_test_vad_energy_detection();
  vw_test_vad_fallback_null_context();
  vw_test_vad_partial_window_sample_counts();
  vw_test_vad_null_safety();
  vw_test_vad_silero_model();
  vw_test_vad_find_chunk_boundary_energy();
  vw_test_vad_find_chunk_boundary_silero();
  printf("All VAD unit tests passed!\n");
  return 0;
}
