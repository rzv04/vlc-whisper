#include "vw_bergamot_engine.h"
#include "vw_bergamot_spike_core.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct CliOptions {
  std::string command;
  std::string model_config;
  std::string input;
  std::string output;
  double budget_ms = 800.0;
  std::size_t warmup = 5;
  std::size_t repeat = 1;
  std::size_t limit = 0;
  bool json = false;
};

void print_usage(std::ostream& out) {
  out << "VLC-Whisper Bergamot translation spike\n\n"
      << "Translate an SRT file:\n"
      << "  vw-bergamot-translate-spike translate --model-config MODEL.yml INPUT.srt [-o OUTPUT.srt]\n\n"
      << "Benchmark per-cue local translation latency:\n"
      << "  vw-bergamot-translate-spike benchmark --model-config MODEL.yml INPUT.srt [options]\n\n"
      << "Benchmark options:\n"
      << "  --budget-ms N   realtime deadline to evaluate (default: 800)\n"
      << "  --warmup N      warmup cues excluded from samples (default: 5)\n"
      << "  --repeat N      repeat measured cue set N times (default: 1)\n"
      << "  --limit N       benchmark only first N cues; 0 means all (default: 0)\n"
      << "  --json          emit machine-readable JSON summary\n";
}

std::size_t parse_size(const std::string& value, const char* option) {
  const std::string error = std::string(option) + " expects a non-negative integer";
  if (value.empty() || value.front() == '-' || value.front() == '+') throw std::runtime_error(error);

  std::size_t consumed = 0;
  unsigned long long parsed = 0;
  try {
    parsed = std::stoull(value, &consumed);
  } catch (...) {
    throw std::runtime_error(error);
  }
  if (consumed != value.size() ||
      parsed > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
    throw std::runtime_error(error);
  }
  return static_cast<std::size_t>(parsed);
}

double parse_double(const std::string& value, const char* option) {
  std::size_t consumed = 0;
  double parsed = 0.0;
  try {
    parsed = std::stod(value, &consumed);
  } catch (...) {
    throw std::runtime_error(std::string(option) + " expects a number");
  }
  if (consumed != value.size() || !std::isfinite(parsed) || parsed <= 0.0) {
    throw std::runtime_error(std::string(option) + " expects a positive finite number");
  }
  return parsed;
}

CliOptions parse_cli(int argc, char** argv) {
  if (argc < 2) throw std::runtime_error("missing command");
  CliOptions options;
  options.command = argv[1];
  if (options.command != "translate" && options.command != "benchmark") {
    throw std::runtime_error("command must be 'translate' or 'benchmark'");
  }

  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    auto need_value = [&](const char* name) -> std::string {
      if (i + 1 >= argc) throw std::runtime_error(std::string(name) + " requires a value");
      return argv[++i];
    };

    if (arg == "--model-config") {
      options.model_config = need_value("--model-config");
    } else if (arg == "-o" || arg == "--output") {
      options.output = need_value("--output");
    } else if (arg == "--budget-ms") {
      options.budget_ms = parse_double(need_value("--budget-ms"), "--budget-ms");
    } else if (arg == "--warmup") {
      options.warmup = parse_size(need_value("--warmup"), "--warmup");
    } else if (arg == "--repeat") {
      options.repeat = parse_size(need_value("--repeat"), "--repeat");
      if (options.repeat == 0) throw std::runtime_error("--repeat must be at least 1");
    } else if (arg == "--limit") {
      options.limit = parse_size(need_value("--limit"), "--limit");
    } else if (arg == "--json") {
      options.json = true;
    } else if (arg == "-h" || arg == "--help") {
      print_usage(std::cout);
      std::exit(0);
    } else if (!arg.empty() && arg[0] == '-') {
      throw std::runtime_error("unknown option: " + arg);
    } else if (options.input.empty()) {
      options.input = arg;
    } else {
      throw std::runtime_error("unexpected positional argument: " + arg);
    }
  }

  if (options.model_config.empty()) throw std::runtime_error("--model-config is required");
  if (options.input.empty()) throw std::runtime_error("an input .srt file is required");
  if (options.command == "benchmark" && !options.output.empty()) {
    throw std::runtime_error("--output is only valid for translate mode");
  }
  return options;
}

std::string read_file(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("could not open input file: " + path);
  std::ostringstream contents;
  contents << stream.rdbuf();
  if (!stream.good() && !stream.eof()) throw std::runtime_error("failed while reading input file: " + path);
  return contents.str();
}

void write_file(const std::string& path, const std::string& contents) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) throw std::runtime_error("could not open output file: " + path);
  stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!stream) throw std::runtime_error("failed while writing output file: " + path);
}

std::string default_output_path(const std::string& input) {
  const std::size_t dot = input.find_last_of('.');
  if (dot == std::string::npos) return input + ".translated.srt";
  return input.substr(0, dot) + ".translated" + input.substr(dot);
}

double elapsed_ms(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

std::size_t utf8_codepoints(const std::string& text) {
  std::size_t count = 0;
  for (unsigned char byte : text) {
    if ((byte & 0xC0U) != 0x80U) ++count;
  }
  return count;
}

std::vector<vw::spike::SubtitleCue> load_cues(const std::string& input_path) {
  std::vector<vw::spike::SubtitleCue> cues;
  std::string error;
  if (!vw::spike::parse_srt(read_file(input_path), cues, error)) {
    throw std::runtime_error("invalid SRT: " + error);
  }
  return cues;
}

int run_translate(const CliOptions& options) {
  auto cues = load_cues(options.input);
  const auto load_start = Clock::now();
  vw::spike::BergamotEngine engine(options.model_config);
  const double load_ms = elapsed_ms(load_start, Clock::now());

  for (std::size_t i = 0; i < cues.size(); ++i) {
    cues[i].text = engine.translate(cues[i].text);
    if ((i + 1) % 100 == 0) std::cerr << "Translated " << (i + 1) << "/" << cues.size() << " cues\n";
  }

  const std::string output = options.output.empty() ? default_output_path(options.input) : options.output;
  write_file(output, vw::spike::render_srt(cues));
  std::cout << "Translated " << cues.size() << " cues to " << output << "\n"
            << "Model load: " << std::fixed << std::setprecision(2) << load_ms << " ms\n";
  return 0;
}

void print_benchmark_text(const vw::spike::BenchmarkSummary& summary) {
  const bool realtime_candidate = summary.sample_count > 0 && summary.p95_ms <= summary.budget_ms;
  std::cout << std::fixed << std::setprecision(2)
            << "samples: " << summary.sample_count << '\n'
            << "model_load_ms: " << summary.model_load_ms << '\n'
            << "budget_ms: " << summary.budget_ms << '\n'
            << "min_ms: " << summary.min_ms << '\n'
            << "mean_ms: " << summary.mean_ms << '\n'
            << "p50_ms: " << summary.p50_ms << '\n'
            << "p95_ms: " << summary.p95_ms << '\n'
            << "p99_ms: " << summary.p99_ms << '\n'
            << "max_ms: " << summary.max_ms << '\n'
            << "deadline_hit_rate_percent: " << summary.deadline_hit_rate_percent << '\n'
            << "cues_per_second: " << summary.cues_per_second << '\n'
            << "characters_per_second: " << summary.characters_per_second << '\n'
            << "realtime_candidate_p95: " << (realtime_candidate ? "yes" : "no") << '\n';
}

void print_benchmark_json(const vw::spike::BenchmarkSummary& summary) {
  const bool realtime_candidate = summary.sample_count > 0 && summary.p95_ms <= summary.budget_ms;
  std::cout << std::fixed << std::setprecision(3)
            << "{\n"
            << "  \"samples\": " << summary.sample_count << ",\n"
            << "  \"model_load_ms\": " << summary.model_load_ms << ",\n"
            << "  \"budget_ms\": " << summary.budget_ms << ",\n"
            << "  \"min_ms\": " << summary.min_ms << ",\n"
            << "  \"mean_ms\": " << summary.mean_ms << ",\n"
            << "  \"p50_ms\": " << summary.p50_ms << ",\n"
            << "  \"p95_ms\": " << summary.p95_ms << ",\n"
            << "  \"p99_ms\": " << summary.p99_ms << ",\n"
            << "  \"max_ms\": " << summary.max_ms << ",\n"
            << "  \"deadline_hit_rate_percent\": " << summary.deadline_hit_rate_percent << ",\n"
            << "  \"cues_per_second\": " << summary.cues_per_second << ",\n"
            << "  \"characters_per_second\": " << summary.characters_per_second << ",\n"
            << "  \"realtime_candidate_p95\": " << (realtime_candidate ? "true" : "false") << "\n"
            << "}\n";
}

int run_benchmark(const CliOptions& options) {
  auto cues = load_cues(options.input);
  if (options.limit > 0 && cues.size() > options.limit) cues.resize(options.limit);

  const auto load_start = Clock::now();
  vw::spike::BergamotEngine engine(options.model_config);
  const double model_load_ms = elapsed_ms(load_start, Clock::now());

  const std::size_t warmup_count = std::min(options.warmup, cues.size());
  for (std::size_t i = 0; i < warmup_count; ++i) (void)engine.translate(cues[i].text);

  std::vector<double> samples;
  samples.reserve(cues.size() * options.repeat);
  std::size_t total_characters = 0;
  const auto wall_start = Clock::now();
  for (std::size_t repeat = 0; repeat < options.repeat; ++repeat) {
    for (const auto& cue : cues) {
      const auto start = Clock::now();
      (void)engine.translate(cue.text);
      samples.push_back(elapsed_ms(start, Clock::now()));
      total_characters += utf8_codepoints(cue.text);
    }
  }
  const double wall_ms = elapsed_ms(wall_start, Clock::now());

  const auto summary =
      vw::spike::summarize_benchmark(samples, options.budget_ms, model_load_ms, total_characters, wall_ms);
  if (options.json) {
    print_benchmark_json(summary);
  } else {
    print_benchmark_text(summary);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
      print_usage(std::cout);
      return 0;
    }
    const CliOptions options = parse_cli(argc, argv);
    if (options.command == "translate") return run_translate(options);
    return run_benchmark(options);
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n\n";
    print_usage(std::cerr);
    return 2;
  }
}
