#include <stdio.h>
#include <string.h>

#include "vw_benchmark.h"
#include "vw_test.h"

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

int main(void) {
  vw_benchmark_t benchmark;
  EXPECT(vw_benchmark_begin(&benchmark, "tiny", "gpu", 1000000));
  EXPECT(benchmark.report_path[0] != '\0');
  EXPECT(report_contains(benchmark.report_path, "state=active"));

  vw_benchmark_record_audio(&benchmark, 10000000, 1000000, 2000000);
  vw_benchmark_record_audio(&benchmark, 11000000, 1000000, 3000000);
  vw_benchmark_record_frame(&benchmark);

  vw_caption_segment_t segment = {
      .start_pts_us = 10000000, .end_pts_us = 10500000, .text_bytes = 4, .text_utf8 = (char*)"test"};
  vw_benchmark_record_caption_received(&benchmark, &segment, 2100000, false);
  EXPECT(benchmark.captions_received == 1);
  EXPECT(benchmark.latency_sample_count == 1);
  EXPECT(benchmark.latency_samples[0] < 0);  // Look-ahead arrival is retained, not clamped.
  vw_benchmark_record_caption_sent(&benchmark, 2200000);
  vw_benchmark_record_caption_filtered(&benchmark, true, false, false);
  vw_benchmark_record_translation(&benchmark, 1, 150000, true);
  vw_benchmark_record_translation(&benchmark, 2, 200000, true);
  vw_benchmark_record_translation(&benchmark, 0, 0, false);
  EXPECT(benchmark.translation_requests_sent == 3);
  EXPECT(benchmark.translation_success_count == 2);
  EXPECT(benchmark.translation_tier1_count == 1);
  EXPECT(benchmark.translation_tier2_count == 1);
  EXPECT(benchmark.translation_timeout_count == 1);
  EXPECT(benchmark.translation_duration_us == 350000);

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
  EXPECT(report_contains(benchmark.report_path, "translation_requests_sent=3"));
  EXPECT(report_contains(benchmark.report_path, "translation_success_count=2"));
  EXPECT(report_contains(benchmark.report_path, "translation_tier1_count=1"));
  EXPECT(report_contains(benchmark.report_path, "translation_tier2_count=1"));
  EXPECT(report_contains(benchmark.report_path, "translation_timeout_count=1"));
  remove(benchmark.report_path);
  return 0;
}
