#include "vw_bergamot_spike_core.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* name) {
  if (condition) {
    std::cout << "PASS: " << name << '\n';
  } else {
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
  }
}

void test_parse_multiline_srt() {
  const std::string input =
      "\xEF\xBB\xBF1\r\n"
      "00:00:01,000 --> 00:00:03,250\r\n"
      "<i>Hello</i> there.\r\n"
      "Second line.\r\n\r\n"
      "2\r\n"
      "00:00:04,000 --> 00:00:05,000\r\n"
      "Bye.\r\n";

  std::vector<vw::spike::SubtitleCue> cues;
  std::string error;
  check(vw::spike::parse_srt(input, cues, error), "parse accepts UTF-8 BOM and CRLF SRT");
  check(cues.size() == 2, "parse returns two cues");
  if (cues.size() == 2) {
    check(cues[0].id == "1", "first cue id preserved");
    check(cues[0].timing == "00:00:01,000 --> 00:00:03,250", "first cue timing preserved");
    check(cues[0].text == "<i>Hello</i> there.\nSecond line.", "multiline cue text preserved");
    check(cues[1].id == "2", "second cue id preserved");
  }
}

void test_malformed_srt_rejected() {
  const std::string input = "1\nnot-a-timestamp\nHello\n";
  std::vector<vw::spike::SubtitleCue> cues;
  std::string error;
  check(!vw::spike::parse_srt(input, cues, error), "malformed SRT timing is rejected");
  check(!error.empty(), "malformed SRT reports an error");
}

void test_render_preserves_identity_and_timing() {
  std::vector<vw::spike::SubtitleCue> cues = {
      {.id = "7", .timing = "00:00:10,100 --> 00:00:11,900", .text = "Salut!"},
      {.id = "8", .timing = "00:00:12,000 --> 00:00:13,000", .text = "La revedere."},
  };
  const std::string rendered = vw::spike::render_srt(cues);
  check(rendered.find("7\n00:00:10,100 --> 00:00:11,900\nSalut!") != std::string::npos,
        "render keeps first cue metadata");
  check(rendered.find("8\n00:00:12,000 --> 00:00:13,000\nLa revedere.") != std::string::npos,
        "render keeps second cue metadata");
}

void test_percentiles_and_deadline_rate() {
  const std::vector<double> samples = {10.0, 20.0, 30.0, 40.0, 50.0};
  const auto summary = vw::spike::summarize_benchmark(samples, 25.0, 123.0, 100, 250.0);
  check(std::fabs(summary.p50_ms - 30.0) < 0.001, "p50 uses deterministic nearest-rank percentile");
  check(std::fabs(summary.p95_ms - 50.0) < 0.001, "p95 uses deterministic nearest-rank percentile");
  check(std::fabs(summary.p99_ms - 50.0) < 0.001, "p99 uses deterministic nearest-rank percentile");
  check(std::fabs(summary.max_ms - 50.0) < 0.001, "max latency reported");
  check(std::fabs(summary.deadline_hit_rate_percent - 40.0) < 0.001, "deadline hit rate counts samples at or below budget");
  check(std::fabs(summary.model_load_ms - 123.0) < 0.001, "model load time is kept separate from cue latency");
  check(std::fabs(summary.cues_per_second - 20.0) < 0.001, "throughput derives from benchmark wall time");
  check(std::fabs(summary.characters_per_second - 400.0) < 0.001, "character throughput derives from benchmark wall time");
}

void test_empty_benchmark_samples() {
  const auto summary = vw::spike::summarize_benchmark({}, 800.0, 10.0, 0, 0.0);
  check(summary.sample_count == 0, "empty benchmark has zero samples");
  check(summary.p50_ms == 0.0 && summary.p95_ms == 0.0 && summary.p99_ms == 0.0,
        "empty benchmark percentiles are zero");
  check(summary.deadline_hit_rate_percent == 0.0, "empty benchmark deadline rate is zero");
}

}  // namespace

int main() {
  test_parse_multiline_srt();
  test_malformed_srt_rejected();
  test_render_preserves_identity_and_timing();
  test_percentiles_and_deadline_rate();
  test_empty_benchmark_samples();

  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "All Bergamot spike core tests passed\n";
  return 0;
}
