#define _POSIX_C_SOURCE 200809L

#include "vw_benchmark.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_log.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#define VW_BENCHMARK_REPORT_FILENAME "vlc-whisper-benchmark.txt"

#ifndef _WIN32
static bool vw_benchmark_directory_exists(const char* path) {
  struct stat info;
  return path && path[0] && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}
#endif

static bool vw_benchmark_resolve_report_path(char* path, size_t path_size) {
  if (!path || path_size == 0) return false;
#ifdef _WIN32
  char temp_dir[VW_PATH_MAX_BYTES];
  DWORD length = GetTempPathA((DWORD)sizeof(temp_dir), temp_dir);
  if (length == 0 || length >= sizeof(temp_dir)) return false;
  int written = snprintf(path, path_size, "%s%s", temp_dir, VW_BENCHMARK_REPORT_FILENAME);
#else
  const char* temp_dir = getenv("XDG_RUNTIME_DIR");
  if (!vw_benchmark_directory_exists(temp_dir)) temp_dir = getenv("TMPDIR");
  if (!vw_benchmark_directory_exists(temp_dir)) temp_dir = "/tmp";
  int written = snprintf(path, path_size, "%s/%s", temp_dir, VW_BENCHMARK_REPORT_FILENAME);
#endif
  return written >= 0 && (size_t)written < path_size;
}

static double vw_benchmark_ratio(uint64_t numerator, uint64_t denominator) {
  return denominator == 0 ? 0.0 : (double)numerator / (double)denominator;
}

static double vw_benchmark_us_to_ms(int64_t duration_us) { return (double)duration_us / 1000.0; }

static double vw_benchmark_us_to_s(int64_t duration_us) { return (double)duration_us / 1000000.0; }

static int vw_benchmark_compare_i64(const void* lhs, const void* rhs) {
  const int64_t left = *(const int64_t*)lhs;
  const int64_t right = *(const int64_t*)rhs;
  return left < right ? -1 : (left > right ? 1 : 0);
}

static int64_t vw_benchmark_percentile(const vw_benchmark_t* benchmark, unsigned percentile) {
  if (!benchmark || benchmark->latency_sample_count == 0) return 0;
  int64_t sorted[VW_BENCHMARK_MAX_LATENCY_SAMPLES];
  memcpy(sorted, benchmark->latency_samples, benchmark->latency_sample_count * sizeof(sorted[0]));
  qsort(sorted, benchmark->latency_sample_count, sizeof(sorted[0]), vw_benchmark_compare_i64);
  size_t index = ((size_t)percentile * (benchmark->latency_sample_count - 1U) + 99U) / 100U;
  return sorted[index];
}

static int64_t vw_benchmark_trans_percentile(const vw_benchmark_t* benchmark, unsigned percentile) {
  if (!benchmark || benchmark->translation_latency_sample_count == 0) return 0;
  int64_t sorted[VW_BENCHMARK_MAX_LATENCY_SAMPLES];
  memcpy(sorted, benchmark->translation_latency_samples,
         benchmark->translation_latency_sample_count * sizeof(sorted[0]));
  qsort(sorted, benchmark->translation_latency_sample_count, sizeof(sorted[0]), vw_benchmark_compare_i64);
  size_t index = ((size_t)percentile * (benchmark->translation_latency_sample_count - 1U) + 99U) / 100U;
  return sorted[index];
}

static const char* vw_benchmark_translation_failure_reason(uint32_t latency_us) {
  if (latency_us == 0) return "pipeline_saturated_or_unavailable";
  if (latency_us >= VW_BENCHMARK_TRANSLATION_TIMEOUT_US) return "deadline_exhausted";
  return "provider_fallbacks_failed";
}

static const char* vw_benchmark_translation_failure_detail(uint32_t latency_us) {
  if (latency_us == 0) return "translation pipeline rejected the cue before a network request could run";
  if (latency_us >= VW_BENCHMARK_TRANSLATION_TIMEOUT_US)
    return "global 800ms cue deadline exhausted while running the Web RPC, GTX, and Mobile fallback chain";
  return "Web RPC, GTX, and Mobile produced no valid translation before the deadline (request or response parse "
         "failure)";
}

static bool vw_benchmark_write(const vw_benchmark_t* benchmark, bool finalized, int64_t end_us) {
  if (!benchmark || !benchmark->active || benchmark->report_path[0] == '\0') return false;
  char temp_path[VW_PATH_MAX_BYTES];
  int path_length = snprintf(temp_path, sizeof(temp_path), "%s.next", benchmark->report_path);
  if (path_length < 0 || (size_t)path_length >= sizeof(temp_path)) return false;
  FILE* report = fopen(temp_path, "w");
  if (!report) return false;
#ifndef _WIN32
  chmod(temp_path, S_IRUSR | S_IWUSR);
#endif

  uint64_t processing_audio_us = benchmark->audio_duration_us;
  if (processing_audio_us == 0) processing_audio_us = benchmark->segment_audio_duration_us;
  int64_t duration_us = end_us > benchmark->started_us ? end_us - benchmark->started_us : 0;
  int64_t translation_latency_min_us =
      benchmark->translation_latency_sample_count ? vw_benchmark_trans_percentile(benchmark, 0) : 0;
  int64_t translation_latency_max_us =
      benchmark->translation_latency_sample_count ? vw_benchmark_trans_percentile(benchmark, 100) : 0;
  fprintf(report, "report_version=2\n");
  fprintf(report, "state=%s\n", finalized ? "finalized" : "active");
  fprintf(report, "model=%s\n", benchmark->model_id[0] ? benchmark->model_id : "unknown");
  fprintf(report, "backend=%s\n", benchmark->backend[0] ? benchmark->backend : "unknown");
  fprintf(report, "session_duration_s=%.3f\n", vw_benchmark_us_to_s(duration_us));
  fprintf(report, "audio_chunks_sent=%llu\n", (unsigned long long)benchmark->audio_chunks_sent);
  fprintf(report, "audio_duration_s=%.3f\n", (double)benchmark->audio_duration_us / 1000000.0);
  fprintf(report, "worker_frames_received=%llu\n", (unsigned long long)benchmark->worker_frames_received);
  fprintf(report, "captions_received=%llu\n", (unsigned long long)benchmark->captions_received);
  fprintf(report, "captions_sent=%llu\n", (unsigned long long)benchmark->captions_sent);
  fprintf(report, "captions_filtered=%llu\n", (unsigned long long)benchmark->captions_filtered);
  fprintf(report, "captions_paused=%llu\n", (unsigned long long)benchmark->captions_paused);
  fprintf(report, "captions_stale=%llu\n", (unsigned long long)benchmark->captions_stale);
  fprintf(report, "captions_presenter_rejected=%llu\n", (unsigned long long)benchmark->captions_presenter_rejected);
  fprintf(report, "segment_count=%llu\n", (unsigned long long)benchmark->captions_received);
  fprintf(report, "segment_audio_duration_s=%.3f\n", (double)benchmark->segment_audio_duration_us / 1000000.0);
  fprintf(report, "segment_text_bytes=%llu\n", (unsigned long long)benchmark->segment_text_bytes);
  fprintf(report, "segment_transcription_duration_s=%.3f\n", (double)benchmark->inference_us / 1000000.0);
  fprintf(report, "inference_processing_duration_s=%.3f\n", (double)benchmark->inference_us / 1000000.0);
  fprintf(report, "processing_audio_duration_s=%.3f\n", (double)processing_audio_us / 1000000.0);
  fprintf(report, "real_time_factor=%.6f\n", vw_benchmark_ratio(benchmark->inference_us, processing_audio_us));
  fprintf(report, "processing_speed_ratio=%.6f\n", vw_benchmark_ratio(processing_audio_us, benchmark->inference_us));
  fprintf(report, "first_sent_caption_elapsed_ms=%.3f\n",
          vw_benchmark_us_to_ms(benchmark->first_caption_recorded ? benchmark->first_caption_elapsed_us : 0));
  fprintf(report, "utterance_latency_samples=%zu\n", benchmark->latency_sample_count);
  fprintf(report, "utterance_latency_samples_dropped=%llu\n", (unsigned long long)benchmark->latency_samples_dropped);
  fprintf(report, "utterance_latency_min_ms=%.3f\n",
          vw_benchmark_us_to_ms(benchmark->latency_sample_count ? vw_benchmark_percentile(benchmark, 0) : 0));
  fprintf(report, "utterance_latency_p50_ms=%.3f\n", vw_benchmark_us_to_ms(vw_benchmark_percentile(benchmark, 50)));
  fprintf(report, "utterance_latency_p95_ms=%.3f\n", vw_benchmark_us_to_ms(vw_benchmark_percentile(benchmark, 95)));
  fprintf(report, "utterance_latency_max_ms=%.3f\n",
          vw_benchmark_us_to_ms(benchmark->latency_sample_count ? vw_benchmark_percentile(benchmark, 100) : 0));
  fprintf(report, "queue_audio_dropped_ms=%.3f\n", (double)benchmark->dropped_audio_us / 1000.0);
  fprintf(report, "translation_requests_sent=%llu\n", (unsigned long long)benchmark->translation_requests_sent);
  fprintf(report, "translation_success_count=%llu\n", (unsigned long long)benchmark->translation_success_count);
  fprintf(report, "translation_tier1_count=%llu\n", (unsigned long long)benchmark->translation_tier1_count);
  fprintf(report, "translation_tier2_count=%llu\n", (unsigned long long)benchmark->translation_tier2_count);
  fprintf(report, "translation_tier3_count=%llu\n", (unsigned long long)benchmark->translation_tier3_count);
  fprintf(report, "translation_failure_count=%llu\n", (unsigned long long)benchmark->translation_failure_count);
  fprintf(report, "translation_timeout_count=%llu\n", (unsigned long long)benchmark->translation_timeout_count);
  fprintf(report, "translation_duration_s=%.3f\n", (double)benchmark->translation_duration_us / 1000000.0);
  fprintf(report, "translation_latency_samples=%zu\n", benchmark->translation_latency_sample_count);
  fprintf(report, "translation_latency_min_ms=%.3f\n", vw_benchmark_us_to_ms(translation_latency_min_us));
  fprintf(report, "translation_latency_p50_ms=%.3f\n",
          vw_benchmark_us_to_ms(vw_benchmark_trans_percentile(benchmark, 50)));
  fprintf(report, "translation_latency_p95_ms=%.3f\n",
          vw_benchmark_us_to_ms(vw_benchmark_trans_percentile(benchmark, 95)));
  fprintf(report, "translation_latency_max_ms=%.3f\n", vw_benchmark_us_to_ms(translation_latency_max_us));
  fprintf(report, "latency_clock=live_pts_to_monotonic_only\n");
  fprintf(report, "post_filtering_included_in_speed=false\n");
  bool success = fflush(report) == 0;
  if (fclose(report) != 0) success = false;
  if (success) {
#ifdef _WIN32
    success = MoveFileExA(temp_path, benchmark->report_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    success = rename(temp_path, benchmark->report_path) == 0;
#endif
  }
  if (!success) remove(temp_path);
  return success;
}

bool vw_benchmark_begin(vw_benchmark_t* benchmark, const char* model_id, const char* backend, int64_t now_us) {
  if (!benchmark || now_us < 0) return false;
  memset(benchmark, 0, sizeof(*benchmark));
  if (!vw_benchmark_resolve_report_path(benchmark->report_path, sizeof(benchmark->report_path))) return false;
  benchmark->active = true;
  benchmark->started_us = now_us;
  if (model_id) snprintf(benchmark->model_id, sizeof(benchmark->model_id), "%s", model_id);
  if (backend) snprintf(benchmark->backend, sizeof(benchmark->backend), "%s", backend);
  if (!vw_benchmark_write(benchmark, false, now_us)) {
    memset(benchmark, 0, sizeof(*benchmark));
    return false;
  }
  benchmark->last_flush_us = now_us;
  return true;
}

void vw_benchmark_record_audio(vw_benchmark_t* benchmark, int64_t start_pts_us, int64_t duration_us, int64_t now_us) {
  if (!benchmark || !benchmark->active || duration_us <= 0) return;
  benchmark->audio_chunks_sent++;
  benchmark->audio_duration_us += (uint64_t)duration_us;
  if (!benchmark->live_clock_valid) {
    benchmark->live_pts_to_monotonic_us = now_us - start_pts_us;
    benchmark->live_clock_valid = true;
  }
}

void vw_benchmark_record_frame(vw_benchmark_t* benchmark) {
  if (benchmark && benchmark->active) benchmark->worker_frames_received++;
}

void vw_benchmark_record_caption_received(vw_benchmark_t* benchmark, const vw_caption_segment_t* segment,
                                          int64_t now_us, bool source_mode) {
  if (!benchmark || !segment) return;
  benchmark->last_segment_id = segment->segment_id;
  benchmark->last_segment_start_pts_us = segment->start_pts_us;
  benchmark->last_segment_end_pts_us = segment->end_pts_us;
  if (!benchmark->active) return;
  benchmark->captions_received++;
  if (segment->end_pts_us > segment->start_pts_us) {
    benchmark->segment_audio_duration_us += (uint64_t)(segment->end_pts_us - segment->start_pts_us);
  }
  benchmark->segment_text_bytes += segment->text_bytes;
  if (!source_mode && benchmark->live_clock_valid) {
    int64_t segment_end_monotonic_us = segment->end_pts_us + benchmark->live_pts_to_monotonic_us;
    if (benchmark->latency_sample_count < VW_BENCHMARK_MAX_LATENCY_SAMPLES) {
      benchmark->latency_samples[benchmark->latency_sample_count++] = now_us - segment_end_monotonic_us;
    } else {
      benchmark->latency_samples_dropped++;
    }
  }
}

void vw_benchmark_record_caption_filtered(vw_benchmark_t* benchmark, bool paused, bool stale, bool presenter_rejected) {
  if (!benchmark || !benchmark->active) return;
  benchmark->captions_filtered++;
  if (paused) benchmark->captions_paused++;
  if (stale) benchmark->captions_stale++;
  if (presenter_rejected) benchmark->captions_presenter_rejected++;
}

void vw_benchmark_record_translation(vw_benchmark_t* benchmark, uint8_t tier, uint32_t latency_us, bool success) {
  if (!benchmark) return;
  if (!success) {
    const char* reason = vw_benchmark_translation_failure_reason(latency_us);
    const char* detail = vw_benchmark_translation_failure_detail(latency_us);
    double start_pts_s = vw_benchmark_us_to_s(benchmark->last_segment_start_pts_us);
    double end_pts_s = vw_benchmark_us_to_s(benchmark->last_segment_end_pts_us);
    double latency_ms = vw_benchmark_us_to_ms((int64_t)latency_us);
    vw_log_event(VW_LOG_LEVEL_ERROR, "PLUGIN_TRANSLATION_FAILURE",
                 "segment=%llu start_pts_s=%.3f end_pts_s=%.3f latency_ms=%.3f reason=%s detail=%s",
                 (unsigned long long)benchmark->last_segment_id, start_pts_s, end_pts_s, latency_ms, reason, detail);
  }
  if (!benchmark->active) return;
  benchmark->translation_requests_sent++;
  benchmark->translation_duration_us += latency_us;
  if (latency_us > 0 && benchmark->translation_latency_sample_count < VW_BENCHMARK_MAX_LATENCY_SAMPLES) {
    benchmark->translation_latency_samples[benchmark->translation_latency_sample_count++] = (int64_t)latency_us;
  }
  if (success) {
    benchmark->translation_success_count++;
    if (tier == 1) {
      benchmark->translation_tier1_count++;
    } else if (tier == 2) {
      benchmark->translation_tier2_count++;
    } else if (tier == 3) {
      benchmark->translation_tier3_count++;
    }
  } else if (latency_us >= VW_BENCHMARK_TRANSLATION_TIMEOUT_US) {
    benchmark->translation_timeout_count++;
  } else {
    benchmark->translation_failure_count++;
  }
}

void vw_benchmark_record_caption_sent(vw_benchmark_t* benchmark, int64_t now_us) {
  if (!benchmark || !benchmark->active) return;
  benchmark->captions_sent++;
  if (!benchmark->first_caption_recorded) {
    benchmark->first_caption_recorded = true;
    benchmark->first_caption_elapsed_us = now_us - benchmark->started_us;
  }
}

void vw_benchmark_update_status(vw_benchmark_t* benchmark, const vw_msg_status_t* status) {
  if (!benchmark || !benchmark->active || !status) return;
  if (status->inference_us >= 0) {
    uint64_t current = (uint64_t)status->inference_us;
    benchmark->inference_us +=
        current >= benchmark->last_worker_inference_us ? current - benchmark->last_worker_inference_us : current;
    benchmark->last_worker_inference_us = current;
  }
  if (status->dropped_audio_us >= 0) {
    uint64_t current = (uint64_t)status->dropped_audio_us;
    benchmark->dropped_audio_us += current >= benchmark->last_worker_dropped_audio_us
                                       ? current - benchmark->last_worker_dropped_audio_us
                                       : current;
    benchmark->last_worker_dropped_audio_us = current;
  }
  if (status->resolved_backend[0] != '\0')
    snprintf(benchmark->backend, sizeof(benchmark->backend), "%s", status->resolved_backend);
}

void vw_benchmark_finalize(vw_benchmark_t* benchmark, int64_t now_us) {
  if (!benchmark || !benchmark->active || benchmark->finalized) return;
  benchmark->ended_us = now_us;
  if (vw_benchmark_write(benchmark, true, now_us)) benchmark->finalized = true;
}

void vw_benchmark_flush_if_due(vw_benchmark_t* benchmark, int64_t now_us) {
  if (!benchmark || !benchmark->active || benchmark->finalized) return;
  if (now_us - benchmark->last_flush_us < 1000000LL) return;
  if (vw_benchmark_write(benchmark, false, now_us)) benchmark->last_flush_us = now_us;
}
