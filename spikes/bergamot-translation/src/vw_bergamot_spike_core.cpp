#include "vw_bergamot_spike_core.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <sstream>
#include <utility>

namespace vw::spike {
namespace {

std::string normalize_newlines(std::string text) {
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
    text.erase(0, 3);
  }

  std::string normalized;
  normalized.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
      normalized.push_back('\n');
    } else {
      normalized.push_back(text[i]);
    }
  }

  std::string canonical;
  canonical.reserve(normalized.size());
  std::size_t line_start = 0;
  while (line_start < normalized.size()) {
    std::size_t line_end = normalized.find('\n', line_start);
    const bool has_newline = line_end != std::string::npos;
    if (!has_newline) line_end = normalized.size();

    bool whitespace_only = true;
    for (std::size_t i = line_start; i < line_end; ++i) {
      if (std::isspace(static_cast<unsigned char>(normalized[i])) == 0) {
        whitespace_only = false;
        break;
      }
    }
    if (!whitespace_only) canonical.append(normalized, line_start, line_end - line_start);
    if (!has_newline) break;
    canonical.push_back('\n');
    line_start = line_end + 1;
  }
  return canonical;
}

bool is_digits(const std::string& value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool parse_timestamp(const std::string& value, std::size_t offset, std::size_t* consumed,
                     std::uint64_t* milliseconds) {
  const std::size_t start = offset;
  std::uint64_t hours = 0;
  std::size_t digits = 0;
  while (offset < value.size() && std::isdigit(static_cast<unsigned char>(value[offset]))) {
    const unsigned digit = static_cast<unsigned>(value[offset] - '0');
    if (hours > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) return false;
    hours = hours * 10U + digit;
    ++offset;
    ++digits;
  }
  if (digits < 2 || offset >= value.size() || value[offset++] != ':') return false;

  auto take_two_digits = [&](char separator, unsigned* component) {
    if (offset + 2 >= value.size()) return false;
    if (!std::isdigit(static_cast<unsigned char>(value[offset])) ||
        !std::isdigit(static_cast<unsigned char>(value[offset + 1])) || value[offset + 2] != separator) {
      return false;
    }
    *component = static_cast<unsigned>(value[offset] - '0') * 10U +
                 static_cast<unsigned>(value[offset + 1] - '0');
    if (*component > 59U) return false;
    offset += 3;
    return true;
  };

  unsigned minutes = 0;
  unsigned seconds = 0;
  if (!take_two_digits(':', &minutes)) return false;
  if (!take_two_digits(',', &seconds)) return false;
  if (offset + 3 > value.size()) return false;

  unsigned millis = 0;
  for (std::size_t i = 0; i < 3; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(value[offset + i]))) return false;
    millis = millis * 10U + static_cast<unsigned>(value[offset + i] - '0');
  }
  offset += 3;

  std::uint64_t total = hours;
  const std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
  if (total > (max - minutes) / 60U) return false;
  total = total * 60U + minutes;
  if (total > (max - seconds) / 60U) return false;
  total = total * 60U + seconds;
  if (total > (max - millis) / 1000U) return false;
  total = total * 1000U + millis;

  if (consumed) *consumed = offset - start;
  if (milliseconds) *milliseconds = total;
  return true;
}

bool valid_timing_line(const std::string& timing) {
  std::size_t first_len = 0;
  std::uint64_t start_ms = 0;
  if (!parse_timestamp(timing, 0, &first_len, &start_ms)) return false;

  std::size_t pos = first_len;
  while (pos < timing.size() && timing[pos] == ' ') ++pos;
  if (pos + 3 > timing.size() || timing.compare(pos, 3, "-->") != 0) return false;
  pos += 3;
  while (pos < timing.size() && timing[pos] == ' ') ++pos;

  std::size_t second_len = 0;
  std::uint64_t end_ms = 0;
  if (!parse_timestamp(timing, pos, &second_len, &end_ms)) return false;
  pos += second_len;
  if (pos != timing.size() && std::isspace(static_cast<unsigned char>(timing[pos])) == 0) return false;
  return end_ms >= start_ms;
}

double nearest_rank_percentile(const std::vector<double>& sorted, double percentile) {
  if (sorted.empty()) return 0.0;
  const double rank = std::ceil((percentile / 100.0) * static_cast<double>(sorted.size()));
  const std::size_t index = static_cast<std::size_t>(std::max(1.0, rank)) - 1;
  return sorted[std::min(index, sorted.size() - 1)];
}

}  // namespace

bool parse_srt(const std::string& input, std::vector<SubtitleCue>& cues, std::string& error) {
  cues.clear();
  error.clear();

  const std::string normalized = normalize_newlines(input);
  std::size_t cursor = 0;
  std::size_t block_index = 0;

  while (cursor < normalized.size()) {
    while (cursor < normalized.size() && normalized[cursor] == '\n') ++cursor;
    if (cursor >= normalized.size()) break;

    std::size_t block_end = normalized.find("\n\n", cursor);
    if (block_end == std::string::npos) block_end = normalized.size();
    const std::string block = normalized.substr(cursor, block_end - cursor);
    cursor = block_end == normalized.size() ? normalized.size() : block_end + 2;
    ++block_index;

    std::vector<std::string> lines;
    std::istringstream stream(block);
    std::string line;
    while (std::getline(stream, line)) lines.push_back(line);

    if (lines.size() < 3) {
      error = "SRT block " + std::to_string(block_index) + " has fewer than three lines";
      return false;
    }
    if (!is_digits(lines[0])) {
      error = "SRT block " + std::to_string(block_index) + " has a non-numeric cue id";
      return false;
    }
    if (!valid_timing_line(lines[1])) {
      error = "SRT block " + std::to_string(block_index) + " has an invalid timing line";
      return false;
    }

    SubtitleCue cue;
    cue.id = lines[0];
    cue.timing = lines[1];
    for (std::size_t i = 2; i < lines.size(); ++i) {
      if (!cue.text.empty()) cue.text.push_back('\n');
      cue.text += lines[i];
    }
    cues.push_back(std::move(cue));
  }

  if (cues.empty()) {
    error = "SRT contains no cues";
    return false;
  }
  return true;
}

std::string render_srt(const std::vector<SubtitleCue>& cues) {
  std::string output;
  for (std::size_t i = 0; i < cues.size(); ++i) {
    const SubtitleCue& cue = cues[i];
    output += cue.id;
    output.push_back('\n');
    output += cue.timing;
    output.push_back('\n');
    output += cue.text;
    output.push_back('\n');
    if (i + 1 < cues.size()) output.push_back('\n');
  }
  return output;
}

BenchmarkSummary summarize_benchmark(const std::vector<double>& cue_latency_ms, double budget_ms, double model_load_ms,
                                     std::size_t total_characters, double benchmark_wall_ms) {
  BenchmarkSummary summary;
  summary.sample_count = cue_latency_ms.size();
  summary.model_load_ms = model_load_ms;
  summary.budget_ms = budget_ms;
  if (cue_latency_ms.empty()) return summary;

  std::vector<double> sorted = cue_latency_ms;
  std::sort(sorted.begin(), sorted.end());
  summary.min_ms = sorted.front();
  summary.max_ms = sorted.back();
  summary.mean_ms = std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(sorted.size());
  summary.p50_ms = nearest_rank_percentile(sorted, 50.0);
  summary.p95_ms = nearest_rank_percentile(sorted, 95.0);
  summary.p99_ms = nearest_rank_percentile(sorted, 99.0);

  const std::size_t hits = static_cast<std::size_t>(std::count_if(
      cue_latency_ms.begin(), cue_latency_ms.end(), [budget_ms](double latency) { return latency <= budget_ms; }));
  summary.deadline_hit_rate_percent =
      100.0 * static_cast<double>(hits) / static_cast<double>(cue_latency_ms.size());

  if (benchmark_wall_ms > 0.0) {
    const double wall_seconds = benchmark_wall_ms / 1000.0;
    summary.cues_per_second = static_cast<double>(cue_latency_ms.size()) / wall_seconds;
    summary.characters_per_second = static_cast<double>(total_characters) / wall_seconds;
  }
  return summary;
}

}  // namespace vw::spike
