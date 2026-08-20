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
    EXPECT(seg.no_speech_prob >= 0.0f && seg.no_speech_prob <= 1.0f);
    EXPECT(seg.text_utf8 != NULL);
  }

  vw_whisper_engine_free(eng);
  printf("test_whisper_engine PASSED\n");
  return 0;
}
