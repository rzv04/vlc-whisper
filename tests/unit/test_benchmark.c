#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

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
  vw_test_check_true("pipeline failure uses error level", capture.level == VW_LOG_LEVEL_ERROR);
  vw_test_check_true("pipeline failure uses translation event id",
                     strcmp(capture.event_id, "PLUGIN_TRANSLATION_FAILURE") == 0);
  vw_test_check_true("pipeline failure includes segment id", strstr(capture.message, "segment=42") != NULL);
  vw_test_check_true("pipeline failure includes start seconds", strstr(capture.message, "start_pts_s=12.000") != NULL);
  vw_test_check_true("pipeline failure includes end seconds", strstr(capture.message, "end_pts_s=13.500") != NULL);
  vw_test_check_true("pipeline failure includes zero latency", strstr(capture.message, "latency_ms=0.000") != NULL);
  vw_test_check_true("pipeline failure class is explicit",
                     strstr(capture.message, "reason=pipeline_saturated_or_unavailable") != NULL);
  vw_test_check_true("pipeline failure explains pre-request rejection",
                     strstr(capture.message, "before a network request could run") != NULL);
  vw_test_check_false("pipeline failure omits microsecond presentation fields",
                      strstr(capture.message, "_us=") != NULL);

  memset(&capture, 0, sizeof(capture));
  vw_benchmark_record_translation(&benchmark, 0, 100000, false);
  vw_test_check_true("provider failure includes latency", strstr(capture.message, "latency_ms=100.000") != NULL);
  vw_test_check_true("provider failure class is explicit",
                     strstr(capture.message, "reason=provider_fallbacks_failed") != NULL);
  vw_test_check_true("provider failure names fallback chain",
                     strstr(capture.message, "Web RPC, GTX, and Mobile produced no valid translation") != NULL);
  vw_test_check_true("provider failure explains request or parse cause",
                     strstr(capture.message, "request or response parse failure") != NULL);

  memset(&capture, 0, sizeof(capture));
  vw_benchmark_record_translation(&benchmark, 0, VW_BENCHMARK_TRANSLATION_TIMEOUT_US, false);
  vw_test_check_true("deadline failure includes latency", strstr(capture.message, "latency_ms=800.000") != NULL);
  vw_test_check_true("deadline failure class is explicit",
                     strstr(capture.message, "reason=deadline_exhausted") != NULL);
  vw_test_check_true("deadline failure explains global budget",
                     strstr(capture.message, "global 800ms cue deadline exhausted") != NULL);
  vw_test_check_false("deadline failure omits source subtitle text", strstr(capture.message, "source") != NULL);
  vw_test_check_false("deadline failure omits translated subtitle text", strstr(capture.message, "translated") != NULL);

  vw_log_set_enabled(false);
  vw_log_set_sink(NULL, NULL);
}

#ifndef _WIN32
static void test_posix_fallback_is_per_user(void) {
  const char* xdg = getenv("XDG_RUNTIME_DIR");
  const char* tmp = getenv("TMPDIR");
  char saved_xdg[VW_PATH_MAX_BYTES] = {0};
  char saved_tmp[VW_PATH_MAX_BYTES] = {0};
  bool had_xdg = xdg != NULL;
  bool had_tmp = tmp != NULL;
  if (had_xdg) snprintf(saved_xdg, sizeof(saved_xdg), "%s", xdg);
  if (had_tmp) snprintf(saved_tmp, sizeof(saved_tmp), "%s", tmp);

  char expected[VW_PATH_MAX_BYTES];
  snprintf(expected, sizeof(expected), "/tmp/vlc-whisper-%lu/vlc-whisper-benchmark.txt", (unsigned long)getuid());

  unsetenv("XDG_RUNTIME_DIR");
  unsetenv("TMPDIR");

  vw_benchmark_t missing_runtime = {0};
  bool missing_runtime_started = vw_benchmark_begin(&missing_runtime, "tiny", "cpu", 1000000);
  vw_test_check_true("missing runtime directory uses uid fallback", missing_runtime_started);
  if (missing_runtime_started) {
    vw_test_check_true("missing runtime fallback path is per user", strcmp(missing_runtime.report_path, expected) == 0);
    vw_benchmark_finalize(&missing_runtime, 2000000);
    remove(missing_runtime.report_path);
  }

  setenv("XDG_RUNTIME_DIR", "/tmp", 1);
  unsetenv("TMPDIR");

  vw_benchmark_t shared_runtime = {0};
  bool shared_runtime_started = vw_benchmark_begin(&shared_runtime, "tiny", "cpu", 3000000);
  vw_test_check_true("shared XDG runtime directory is rejected", shared_runtime_started);
  if (shared_runtime_started) {
    vw_test_check_true("shared XDG runtime falls back per user", strcmp(shared_runtime.report_path, expected) == 0);
    vw_benchmark_finalize(&shared_runtime, 4000000);
    remove(shared_runtime.report_path);
  }

  if (had_xdg) {
    setenv("XDG_RUNTIME_DIR", saved_xdg, 1);
  } else {
    unsetenv("XDG_RUNTIME_DIR");
  }
  if (had_tmp) {
    setenv("TMPDIR", saved_tmp, 1);
  } else {
    unsetenv("TMPDIR");
  }
}
#endif

int main(void) {
  test_translation_failure_logging();
#ifndef _WIN32
  test_posix_fallback_is_per_user();
#endif

  vw_benchmark_t benchmark;
  EXPECT(vw_benchmark_begin(&benchmark, "tiny", "gpu", 1000000));
  EXPECT(benchmark.report_path[0] != '\0');
  EXPECT(path_ends_with(benchmark.report_path, "vlc-whisper-benchmark.txt"));
  EXPECT(report_contains(benchmark.report_path, "report_version=2"));
  EXPECT(report_contains(benchmark.report_path, "state=active"));
  char report_path[VW_PATH_MAX_BYTES];
  snprintf(report_path, sizeof(report_path), "%s", benchmark.report_path);

  char legacy_staging_path[VW_PATH_MAX_BYTES];
  int legacy_len = snprintf(legacy_staging_path, sizeof(legacy_staging_path), "%s.next", benchmark.report_path);
  EXPECT(legacy_len > 0 && (size_t)legacy_len < sizeof(legacy_staging_path));
  FILE* legacy_staging = fopen(legacy_staging_path, "w");
  EXPECT(legacy_staging != NULL);
  EXPECT(fputs("sentinel\n", legacy_staging) >= 0);
  EXPECT(fclose(legacy_staging) == 0);

  vw_benchmark_record_audio(&benchmark, 10000000, 1000000, 2000000);
  vw_benchmark_record_audio(&benchmark, 11000000, 1000000, 3000000);
  vw_benchmark_record_frame(&benchmark);

  vw_caption_segment_t segment = {0};
  segment.segment_id = 42;
  segment.start_pts_us = 10000000;
  segment.end_pts_us = 10500000;
  segment.text_bytes = 4;
  segment.text_utf8 = (char*)"test";
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
  EXPECT(report_contains(benchmark.report_path, "session_duration_s=4.000"));
  EXPECT(report_contains(benchmark.report_path, "audio_duration_s=2.000"));
  EXPECT(report_contains(benchmark.report_path, "segment_audio_duration_s=0.500"));
  EXPECT(report_contains(benchmark.report_path, "segment_transcription_duration_s=0.500"));
  EXPECT(report_contains(benchmark.report_path, "inference_processing_duration_s=0.500"));
  EXPECT(report_contains(benchmark.report_path, "processing_audio_duration_s=2.000"));
  EXPECT(report_contains(benchmark.report_path, "first_sent_caption_elapsed_ms=1200.000"));
  EXPECT(report_contains(benchmark.report_path, "utterance_latency_min_ms=-400.000"));
  EXPECT(report_contains(benchmark.report_path, "utterance_latency_p50_ms=-400.000"));
  EXPECT(report_contains(benchmark.report_path, "utterance_latency_p95_ms=-400.000"));
  EXPECT(report_contains(benchmark.report_path, "utterance_latency_max_ms=-400.000"));
  EXPECT(report_contains(benchmark.report_path, "queue_audio_dropped_ms=0.123"));
  EXPECT(report_contains(benchmark.report_path, "real_time_factor=0.250000"));
  EXPECT(report_contains(benchmark.report_path, "translation_requests_sent=4"));
  EXPECT(report_contains(benchmark.report_path, "translation_success_count=2"));
  EXPECT(report_contains(benchmark.report_path, "translation_tier1_count=1"));
  EXPECT(report_contains(benchmark.report_path, "translation_tier2_count=1"));
  EXPECT(report_contains(benchmark.report_path, "translation_failure_count=1"));
  EXPECT(report_contains(benchmark.report_path, "translation_timeout_count=1"));
  EXPECT(report_contains(benchmark.report_path, "translation_duration_s=1.250"));
  EXPECT(report_contains(benchmark.report_path, "translation_latency_samples=4"));
  EXPECT(report_contains(benchmark.report_path, "translation_latency_min_ms=100.000"));
  EXPECT(report_contains(benchmark.report_path, "translation_latency_p50_ms=200.000"));
  EXPECT(report_contains(benchmark.report_path, "translation_latency_p95_ms=800.000"));
  EXPECT(report_contains(benchmark.report_path, "translation_latency_max_ms=800.000"));
  EXPECT(!report_contains(benchmark.report_path, "session_duration_us="));
  EXPECT(!report_contains(benchmark.report_path, "utterance_latency_p50_us="));
  EXPECT(!report_contains(benchmark.report_path, "translation_latency_p50_us="));
  EXPECT(!report_contains(benchmark.report_path, "translation_duration_us="));
  EXPECT(report_contains(legacy_staging_path, "sentinel"));
  remove(legacy_staging_path);

  vw_benchmark_t replacement;
  EXPECT(vw_benchmark_begin(&replacement, "base", "cpu", 6000000));
  EXPECT_EQ_STR(replacement.report_path, report_path);
  EXPECT(report_contains(replacement.report_path, "state=active"));
  EXPECT(report_contains(replacement.report_path, "model=base"));
  EXPECT(!report_contains(replacement.report_path, "translation_requests_sent=4"));
  vw_benchmark_finalize(&replacement, 7000000);
  remove(replacement.report_path);

  vw_benchmark_t epoch = {.active = true};
  vw_benchmark_record_audio(&epoch, 10000000, 1000000, 2000000);
  vw_benchmark_reset_live_clock(&epoch);
  EXPECT(!epoch.live_clock_valid);
  EXPECT(epoch.audio_chunks_sent == 1);
  vw_benchmark_record_audio(&epoch, 100000000, 1000000, 3000000);
  segment.end_pts_us = 101000000;
  segment.start_pts_us = 100000000;
  vw_benchmark_record_caption_received(&epoch, &segment, 4200000, false);
  EXPECT(epoch.latency_samples[0] == 200000);
  EXPECT(epoch.audio_chunks_sent == 2);
  return vw_test_finish("test_benchmark");
}
