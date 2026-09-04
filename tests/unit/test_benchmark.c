#include <stdio.h>
#include <string.h>

#include "vw_benchmark.h"
#include "vw_log.h"
#include "vw_test.h"

typedef struct vw_test_log_capture {
  vw_log_level_t level;
  char event_id[64];
  char message[512];
  unsigned count;
} vw_test_log_capture_t;

static bool report_contains(const char* path, const char* needle) {
  FILE* file = fopen(path, "r");
  if (!file) return false;
  char line[256];
  bool found = false;
  while (fgets(line, sizeof(line), file)) {
    if (strstr(line, needle)) {
      found = true;
      break;
    }
  }
  fclose(file);
  return found;
}

static bool path_ends_with(const char* path, const char* suffix) {
  if (!path || !suffix) return false;
  size_t path_len = strlen(path);
  size_t suffix_len = strlen(suffix);
  return path_len >= suffix_len && strcmp(path + path_len - suffix_len, suffix) == 0;
}

static void test_log_sink(vw_log_level_t level, const char* event_id, const char* formatted_msg, void* user_data) {
  vw_test_log_capture_t* capture = (vw_test_log_capture_t*)user_data;
  if (!capture) return;
  capture->level = level;
  snprintf(capture->event_id, sizeof(capture->event_id), "%s", event_id ? event_id : "");
  snprintf(capture->message, sizeof(capture->message), "%s", formatted_msg ? formatted_msg : "");
  capture->count++;
}

static void test_translation_failure_logging(void) {
  vw_benchmark_t benchmark = {0};
  benchmark.last_segment_id = 42;
  benchmark.last_segment_start_pts_us = 12000000;
  benchmark.last_segment_end_pts_us = 13500000;

  vw_test_log_capture_t capture = {0};
  vw_log_set_sink(test_log_sink, &capture);
  vw_log_set_enabled(true);

  vw_benchmark_record_translation(&benchmark, 0, 0, false);
  EXPECT(capture.level == VW_LOG_LEVEL_ERROR);
  EXPECT_EQ_STR(capture.event_id, "PLUGIN_TRANSLATION_FAILURE");
  EXPECT(strstr(capture.message, "segment=42") != NULL);
  EXPECT(strstr(capture.message, "start_pts_us=12000000") != NULL);
  EXPECT(strstr(capture.message, "end_pts_us=13500000") != NULL);
  EXPECT(strstr(capture.message, "reason=pipeline_saturated_or_unavailable") != NULL);
  EXPECT(strstr(capture.message, "before a network request could run") != NULL);

  memset(&capture, 0, sizeof(capture));
  vw_benchmark_record_translation(&benchmark, 0, 100000, false);
  EXPECT(strstr(capture.message, "latency_us=100000") != NULL);
  EXPECT(strstr(capture.message, "reason=provider_fallbacks_failed") != NULL);
  EXPECT(strstr(capture.message, "Web RPC, GTX, and Mobile produced no valid translation") != NULL);
  EXPECT(strstr(capture.message, "request or response parse failure") != NULL);

  memset(&capture, 0, sizeof(capture));
  vw_benchmark_record_translation(&benchmark, 0, VW_BENCHMARK_TRANSLATION_TIMEOUT_US, false);
  EXPECT(strstr(capture.message, "latency_us=800000") != NULL);
  EXPECT(strstr(capture.message, "reason=deadline_exhausted") != NULL);
  EXPECT(strstr(capture.message, "global 800ms cue deadline exhausted") != NULL);
  EXPECT(strstr(capture.message, "source") == NULL);
  EXPECT(strstr(capture.message, "translated") == NULL);

  vw_log_set_enabled(false);
  vw_log_set_sink(NULL, NULL);
}

int main(void) {
  test_translation_failure_logging();

  vw_benchmark_t benchmark;
  EXPECT(vw_benchmark_begin(&benchmark, "tiny", "gpu", 1000000));
  EXPECT(benchmark.report_path[0] != '\0');
  EXPECT(path_ends_with(benchmark.report_path, "vlc-whisper-benchmark.txt"));
  EXPECT(report_contains(benchmark.report_path, "state=active"));
  char report_path[VW_PATH_MAX_BYTES];
  snprintf(report_path, sizeof(report_path), "%s", benchmark.report_path);

  vw_benchmark_record_audio(&benchmark, 10000000, 1000000, 2000000);
  vw_benchmark_record_audio(&benchmark, 11000000, 1000000, 3000000);
  vw_benchmark_record_frame(&benchmark);

  vw_caption_segment_t segment = {.segment_id = 42,
                                  .start_pts_us = 10000000,
                                  .end_pts_us = 10500000,
                                  .text_bytes = 4,
                                  .text_utf8 = (char*)"test"};
  vw_benchmark_record_caption_received(&benchmark, &segment, 2100000, false);
  EXPECT(benchmark.captions_received == 1);
  EXPECT(benchmark.last_segment_id == 42);
  EXPECT(benchmark.last_segment_start_pts_us == 10000000);
  EXPECT(benchmark.last_segment_end_pts_us == 10500000);
  EXPECT(benchmark.latency_sample_count == 1);
  EXPECT(benchmark.latency_samples[0] < 0);  // Look-ahead arrival is retained, not clamped.
  vw_benchmark_record_caption_sent(&benchmark, 2200000);
  vw_benchmark_record_caption_filtered(&benchmark, true, false, false);

  vw_benchmark_record_translation(&benchmark, 1, 150000, true);
  vw_benchmark_record_translation(&benchmark, 2, 200000, true);
  vw_benchmark_record_translation(&benchmark, 0, 100000, false);  // transport/parser failure before deadline
  vw_benchmark_record_translation(&benchmark, 0, 800000, false);  // global cue deadline exhausted
  EXPECT(benchmark.translation_requests_sent == 4);
  EXPECT(benchmark.translation_success_count == 2);
  EXPECT(benchmark.translation_tier1_count == 1);
  EXPECT(benchmark.translation_tier2_count == 1);
  EXPECT(benchmark.translation_failure_count == 1);
  EXPECT(benchmark.translation_timeout_count == 1);
  EXPECT(benchmark.translation_duration_us == 1250000);
  EXPECT(benchmark.translation_latency_sample_count == 4);

  vw_msg_status_t status = {.inference_us = 500000, .dropped_audio_us = 123};
  snprintf(status.resolved_backend, sizeof(status.resolved_backend), "cpu");
  vw_benchmark_update_status(&benchmark, &status);
  EXPECT(benchmark.inference_us == 500000);
  EXPECT(benchmark.dropped_audio_us == 123);
  EXPECT_EQ_STR(benchmark.backend, "cpu");

  vw_benchmark_finalize(&benchmark, 5000000);
  EXPECT(benchmark.finalized);
  EXPECT(report_contains(benchmark.report_path, "state=finalized"));
  EXPECT(report_contains(benchmark.report_path, "captions_received=1"));
  EXPECT(report_contains(benchmark.report_path, "captions_sent=1"));
  EXPECT(report_contains(benchmark.report_path, "captions_filtered=1"));
  EXPECT(report_contains(benchmark.report_path, "real_time_factor=0.250000"));
  EXPECT(report_contains(benchmark.report_path, "translation_requests_sent=4"));
  EXPECT(report_contains(benchmark.report_path, "translation_success_count=2"));
  EXPECT(report_contains(benchmark.report_path, "translation_tier1_count=1"));
  EXPECT(report_contains(benchmark.report_path, "translation_tier2_count=1"));
  EXPECT(report_contains(benchmark.report_path, "translation_failure_count=1"));
  EXPECT(report_contains(benchmark.report_path, "translation_timeout_count=1"));
  EXPECT(report_contains(benchmark.report_path, "translation_duration_us=1250000"));
  EXPECT(report_contains(benchmark.report_path, "translation_latency_samples=4"));

  vw_benchmark_t replacement;
  EXPECT(vw_benchmark_begin(&replacement, "base", "cpu", 6000000));
  EXPECT_EQ_STR(replacement.report_path, report_path);
  EXPECT(report_contains(replacement.report_path, "state=active"));
  EXPECT(report_contains(replacement.report_path, "model=base"));
  EXPECT(!report_contains(replacement.report_path, "translation_requests_sent=4"));
  vw_benchmark_finalize(&replacement, 7000000);
  remove(replacement.report_path);
  return 0;
}
