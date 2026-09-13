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
  vw_test_check_true("translation failure uses error level", capture.level == VW_LOG_LEVEL_ERROR);
  vw_test_check_true("translation failure uses translation event id",
                     strcmp(capture.event_id, "PLUGIN_TRANSLATION_FAILURE") == 0);
  vw_test_check_true("translation failure includes segment id", strstr(capture.message, "segment=42") != NULL);
  vw_test_check_true("translation failure includes start seconds",
                     strstr(capture.message, "start_pts_sec=12.000") != NULL);
  vw_test_check_true("translation failure includes end seconds", strstr(capture.message, "end_pts_sec=13.500") != NULL);
  vw_test_check_true("translation failure includes zero latency", strstr(capture.message, "latency_ms=0.000") != NULL);
  vw_test_check_true("translation failure uses generic reason",
                     strstr(capture.message, "reason=translation_failed") != NULL);
  vw_test_check_true("translation failure marks unavailable cause",
                     strstr(capture.message, "worker_did_not_provide_explicit_failure_cause") != NULL);
  vw_test_check_false("translation failure omits microsecond presentation fields",
                      strstr(capture.message, "_us=") != NULL);

  memset(&capture, 0, sizeof(capture));
  vw_benchmark_record_translation(&benchmark, 0, 100000, false);
  vw_test_check_true("timed failure includes latency", strstr(capture.message, "latency_ms=100.000") != NULL);
  vw_test_check_true("timed failure keeps generic reason",
                     strstr(capture.message, "reason=translation_failed") != NULL);
  vw_test_check_true("timed failure keeps explicit cause boundary",
                     strstr(capture.message, "worker_did_not_provide_explicit_failure_cause") != NULL);

  memset(&capture, 0, sizeof(capture));
  vw_benchmark_record_translation(&benchmark, 0, VW_BENCHMARK_TRANSLATION_TIMEOUT_US, false);
  vw_test_check_true("budget failure includes latency", strstr(capture.message, "latency_ms=800.000") != NULL);
  vw_test_check_true("budget failure keeps generic reason",
                     strstr(capture.message, "reason=translation_failed") != NULL);
  vw_test_check_true("budget failure keeps explicit cause boundary",
                     strstr(capture.message, "worker_did_not_provide_explicit_failure_cause") != NULL);
  vw_test_check_false("translation failure omits source subtitle text", strstr(capture.message, "source") != NULL);
  vw_test_check_false("translation failure omits translated subtitle text",
                      strstr(capture.message, "translated") != NULL);

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

  char temp_template[] = "/tmp/vlc-whisper-test-XXXXXX";
  char* temp_base = mkdtemp(temp_template);
  vw_test_check_true("fallback test creates isolated temp base", temp_base != NULL);
  if (!temp_base) return;

  char private_dir[VW_PATH_MAX_BYTES];
  char expected[VW_PATH_MAX_BYTES];
  snprintf(private_dir, sizeof(private_dir), "%s/vlc-whisper-%lu", temp_base, (unsigned long)getuid());
  snprintf(expected, sizeof(expected), "%s/vlc-whisper-benchmark.txt", private_dir);

  unsetenv("XDG_RUNTIME_DIR");
  setenv("TMPDIR", temp_base, 1);

  vw_benchmark_t missing_runtime = {0};
  bool missing_runtime_started = vw_benchmark_begin(&missing_runtime, "tiny", "cpu", 1000000);
  vw_test_check_true("missing runtime directory uses uid fallback", missing_runtime_started);
  if (missing_runtime_started) {
    vw_test_check_true("missing runtime fallback stays isolated", strcmp(missing_runtime.report_path, expected) == 0);
    vw_benchmark_finalize(&missing_runtime, 2000000);
    remove(missing_runtime.report_path);
  }

  setenv("XDG_RUNTIME_DIR", "/tmp", 1);
  setenv("TMPDIR", temp_base, 1);

  vw_benchmark_t shared_runtime = {0};
  bool shared_runtime_started = vw_benchmark_begin(&shared_runtime, "tiny", "cpu", 3000000);
  vw_test_check_true("shared XDG runtime directory is rejected", shared_runtime_started);
  if (shared_runtime_started) {
    vw_test_check_true("shared XDG fallback stays isolated", strcmp(shared_runtime.report_path, expected) == 0);
    vw_benchmark_finalize(&shared_runtime, 4000000);
    remove(shared_runtime.report_path);
  }

  rmdir(private_dir);
  rmdir(temp_base);

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
  vw_test_check_true("benchmark report path is populated", benchmark.report_path[0] != '\0');
  vw_test_check_true("benchmark report uses stable filename",
                     path_ends_with(benchmark.report_path, "vlc-whisper-benchmark.txt"));
  vw_test_check_true("benchmark report uses schema v2", report_contains(benchmark.report_path, "report_version=2"));
  vw_test_check_true("benchmark report begins active", report_contains(benchmark.report_path, "state=active"));
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
  vw_benchmark_record_translation(&benchmark, 0, 100000, false);
  vw_benchmark_record_translation(&benchmark, 0, 800000, false);
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
  vw_test_check_true("final report is finalized", report_contains(benchmark.report_path, "state=finalized"));
  vw_test_check_true("final report records received captions",
                     report_contains(benchmark.report_path, "captions_received=1"));
  vw_test_check_true("final report records sent captions", report_contains(benchmark.report_path, "captions_sent=1"));
  vw_test_check_true("final report records filtered captions",
                     report_contains(benchmark.report_path, "captions_filtered=1"));
  vw_test_check_true("session duration is seconds",
                     report_contains(benchmark.report_path, "session_duration_sec=4.000"));
  vw_test_check_true("audio duration is seconds", report_contains(benchmark.report_path, "audio_duration_sec=2.000"));
  vw_test_check_true("segment audio duration is seconds",
                     report_contains(benchmark.report_path, "segment_audio_duration_sec=0.500"));
  vw_test_check_true("transcription duration is seconds",
                     report_contains(benchmark.report_path, "segment_transcription_duration_sec=0.500"));
  vw_test_check_true("inference duration is seconds",
                     report_contains(benchmark.report_path, "inference_processing_duration_sec=0.500"));
  vw_test_check_true("processing audio duration is seconds",
                     report_contains(benchmark.report_path, "processing_audio_duration_sec=2.000"));
  vw_test_check_true("first caption timing is milliseconds",
                     report_contains(benchmark.report_path, "first_sent_caption_elapsed_ms=1200.000"));
  vw_test_check_true("minimum latency is milliseconds",
                     report_contains(benchmark.report_path, "utterance_latency_min_ms=-400.000"));
  vw_test_check_true("median latency is milliseconds",
                     report_contains(benchmark.report_path, "utterance_latency_p50_ms=-400.000"));
  vw_test_check_true("p95 latency is milliseconds",
                     report_contains(benchmark.report_path, "utterance_latency_p95_ms=-400.000"));
  vw_test_check_true("maximum latency is milliseconds",
                     report_contains(benchmark.report_path, "utterance_latency_max_ms=-400.000"));
  vw_test_check_true("dropped audio is milliseconds",
                     report_contains(benchmark.report_path, "queue_audio_dropped_ms=0.123"));
  vw_test_check_true("real time factor is preserved",
                     report_contains(benchmark.report_path, "real_time_factor=0.250000"));
  vw_test_check_true("translation request count is preserved",
                     report_contains(benchmark.report_path, "translation_requests_sent=4"));
  vw_test_check_true("translation success count is preserved",
                     report_contains(benchmark.report_path, "translation_success_count=2"));
  vw_test_check_true("translation tier1 count is preserved",
                     report_contains(benchmark.report_path, "translation_tier1_count=1"));
  vw_test_check_true("translation tier2 count is preserved",
                     report_contains(benchmark.report_path, "translation_tier2_count=1"));
  vw_test_check_true("translation failure count is preserved",
                     report_contains(benchmark.report_path, "translation_failure_count=1"));
  vw_test_check_true("translation timeout count is preserved",
                     report_contains(benchmark.report_path, "translation_timeout_count=1"));
  vw_test_check_true("translation duration is seconds",
                     report_contains(benchmark.report_path, "translation_duration_sec=1.250"));
  vw_test_check_true("translation latency sample count is preserved",
                     report_contains(benchmark.report_path, "translation_latency_samples=4"));
  vw_test_check_true("translation minimum latency is milliseconds",
                     report_contains(benchmark.report_path, "translation_latency_min_ms=100.000"));
  vw_test_check_true("translation median latency is milliseconds",
                     report_contains(benchmark.report_path, "translation_latency_p50_ms=200.000"));
  vw_test_check_true("translation p95 latency is milliseconds",
                     report_contains(benchmark.report_path, "translation_latency_p95_ms=800.000"));
  vw_test_check_true("translation maximum latency is milliseconds",
                     report_contains(benchmark.report_path, "translation_latency_max_ms=800.000"));
  vw_test_check_false("report omits session microseconds",
                      report_contains(benchmark.report_path, "session_duration_us="));
  vw_test_check_false("report omits utterance latency microseconds",
                      report_contains(benchmark.report_path, "utterance_latency_p50_us="));
  vw_test_check_false("report omits translation latency microseconds",
                      report_contains(benchmark.report_path, "translation_latency_p50_us="));
  vw_test_check_false("report omits translation duration microseconds",
                      report_contains(benchmark.report_path, "translation_duration_us="));
  vw_test_check_true("legacy fixed staging path remains untouched", report_contains(legacy_staging_path, "sentinel"));
  remove(legacy_staging_path);

  vw_benchmark_t replacement;
  EXPECT(vw_benchmark_begin(&replacement, "base", "cpu", 6000000));
  vw_test_check_true("replacement reuses stable report path", strcmp(replacement.report_path, report_path) == 0);
  vw_test_check_true("replacement report is active", report_contains(replacement.report_path, "state=active"));
  vw_test_check_true("replacement report contains new model", report_contains(replacement.report_path, "model=base"));
  vw_test_check_false("replacement report drops previous session metrics",
                      report_contains(replacement.report_path, "translation_requests_sent=4"));
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

  vw_benchmark_t samples_bench = {0};
  EXPECT(vw_benchmark_begin(&samples_bench, "base", "cpu", 8000000));
  vw_benchmark_record_processed_samples(&samples_bench, 32000);
  EXPECT(samples_bench.audio_duration_us == 2000000);
  vw_benchmark_record_processed_samples(&samples_bench, 16000);
  EXPECT(samples_bench.audio_duration_us == 2000000);
  vw_benchmark_record_processed_samples(&samples_bench, 48000);
  EXPECT(samples_bench.audio_duration_us == 3000000);
  vw_benchmark_finalize(&samples_bench, 9000000);
  remove(samples_bench.report_path);

  return vw_test_finish("test_benchmark");
}
