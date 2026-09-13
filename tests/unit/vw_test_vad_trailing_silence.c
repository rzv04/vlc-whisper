#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include "vw_vad.h"
#include "whisper.h"

struct whisper_vad_context {
  int unused;
};

struct whisper_vad_segments {
  int count;
};

static struct whisper_vad_context g_context;
static struct whisper_vad_segments g_segments = {.count = 1};

struct whisper_vad_params whisper_vad_default_params(void) {
  struct whisper_vad_params params = {0};
  return params;
}

struct whisper_vad_context_params whisper_vad_default_context_params(void) {
  struct whisper_vad_context_params params = {0};
  return params;
}

struct whisper_vad_context* whisper_vad_init_from_file_with_params(const char* path,
                                                                   struct whisper_vad_context_params params) {
  (void)path;
  (void)params;
  return &g_context;
}

struct whisper_vad_context* whisper_vad_init_with_params(struct whisper_model_loader* loader,
                                                         struct whisper_vad_context_params params) {
  (void)loader;
  (void)params;
  return &g_context;
}

bool whisper_vad_detect_speech(struct whisper_vad_context* context, const float* samples, int sample_count) {
  return context != NULL && samples != NULL && sample_count > 0;
}

bool whisper_vad_detect_speech_no_reset(struct whisper_vad_context* context, const float* samples, int sample_count) {
  return whisper_vad_detect_speech(context, samples, sample_count);
}

void whisper_vad_reset_state(struct whisper_vad_context* context) { (void)context; }
int whisper_vad_n_probs(struct whisper_vad_context* context) {
  (void)context;
  return 0;
}
float* whisper_vad_probs(struct whisper_vad_context* context) {
  (void)context;
  return NULL;
}

struct whisper_vad_segments* whisper_vad_segments_from_probs(struct whisper_vad_context* context,
                                                             struct whisper_vad_params params) {
  (void)params;
  return context ? &g_segments : NULL;
}

struct whisper_vad_segments* whisper_vad_segments_from_samples(struct whisper_vad_context* context,
                                                               struct whisper_vad_params params, const float* samples,
                                                               int sample_count) {
  (void)params;
  return whisper_vad_detect_speech(context, samples, sample_count) ? &g_segments : NULL;
}

int whisper_vad_segments_n_segments(struct whisper_vad_segments* segments) { return segments ? segments->count : 0; }
float whisper_vad_segments_get_segment_t0(struct whisper_vad_segments* segments, int index) {
  (void)segments;
  (void)index;
  return 0.0f;
}
float whisper_vad_segments_get_segment_t1(struct whisper_vad_segments* segments, int index) {
  (void)segments;
  (void)index;
  return 600.0f;  // Six seconds of speech, followed by eighteen seconds of silence.
}
void whisper_vad_free_segments(struct whisper_vad_segments* segments) { (void)segments; }
void whisper_vad_free(struct whisper_vad_context* context) { (void)context; }

int main(void) {
  float* pcm = (float*)calloc(384000U, sizeof(float));
  assert(pcm != NULL);
  size_t cut_samples = 0;
  size_t silence_drain = 0;
  assert(vw_vad_find_chunk_boundary(pcm, 384000U, &g_context, true, &cut_samples, &silence_drain));
  // Segment end is 6s plus 150ms padding; trailing silence must be capped at 300ms.
  assert(cut_samples <= 100800U);
  assert(cut_samples > 96000U);
  free(pcm);
  return 0;
}
