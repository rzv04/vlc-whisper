#ifndef VW_BERGAMOT_SPIKE_CORE_H_
#define VW_BERGAMOT_SPIKE_CORE_H_

#include <cstddef>
#include <string>
#include <vector>

namespace vw::spike {

struct SubtitleCue {
  std::string id;
  std::string timing;
  std::string text;
};

struct BenchmarkSummary {
  std::size_t sample_count = 0;
  double model_load_ms = 0.0;
  double budget_ms = 0.0;
  double min_ms = 0.0;
  double mean_ms = 0.0;
  double p50_ms = 0.0;
  double p95_ms = 0.0;
  double p99_ms = 0.0;
  double max_ms = 0.0;
  double deadline_hit_rate_percent = 0.0;
  double cues_per_second = 0.0;
  double characters_per_second = 0.0;
};

// Parses a UTF-8 SRT document into ordered cues while preserving each cue identifier and timing line verbatim for later
// rendering.
bool parse_srt(const std::string& input, std::vector<SubtitleCue>& cues, std::string& error);

// Renders ordered cues back to normalized UTF-8 SRT text without modifying their identifiers, timing lines, or supplied
// translated cue text.
std::string render_srt(const std::vector<SubtitleCue>& cues);

// Computes deterministic nearest-rank latency percentiles, deadline hit rate, and throughput while keeping cold model
// loading outside measured cue latency.
BenchmarkSummary summarize_benchmark(const std::vector<double>& cue_latency_ms, double budget_ms, double model_load_ms,
                                     std::size_t total_characters, double benchmark_wall_ms);

}  // namespace vw::spike

#endif  // VW_BERGAMOT_SPIKE_CORE_H_
