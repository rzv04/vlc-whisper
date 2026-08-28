#ifndef VW_BENCHMARK_H_
#define VW_BENCHMARK_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vw_protocol_types.h"

#define VW_BENCHMARK_MAX_LATENCY_SAMPLES 1024U
#define VW_BENCHMARK_TRANSLATION_TIMEOUT_US 800000U

typedef struct vw_benchmark {
  bool active;
  bool finalized;
  int64_t started_us;
  int64_t ended_us;
  int64_t last_flush_us;
  char report_path[VW_PATH_MAX_BYTES];
  char model_id[VW_MAX_MODEL_ID_BYTES];
  char backend[16];
  uint64_t audio_chunks_sent;
  uint64_t audio_duration_us;
  uint64_t worker_frames_received;
  uint64_t captions_received;
  uint64_t captions_sent;
  uint64_t captions_filtered;
  uint64_t captions_paused;
  uint64_t captions_stale;
  uint64_t captions_presenter_rejected;
  uint64_t segment_audio_duration_us;
  uint64_t segment_text_bytes;
  uint64_t inference_us;
  uint64_t dropped_audio_us;
  uint64_t last_worker_inference_us;
  uint64_t last_worker_dropped_audio_us;
  uint64_t latency_samples_dropped;
  int64_t first_caption_elapsed_us;
  int64_t live_pts_to_monotonic_us;
  int64_t latency_samples[VW_BENCHMARK_MAX_LATENCY_SAMPLES];
  size_t latency_sample_count;
  uint64_t translation_requests_sent;
  uint64_t translation_success_count;
  uint64_t translation_tier1_count;
  uint64_t translation_tier2_count;
  uint64_t translation_tier3_count;
  uint64_t translation_failure_count;
  uint64_t translation_timeout_count;
  uint64_t translation_duration_us;
  int64_t translation_latency_samples[VW_BENCHMARK_MAX_LATENCY_SAMPLES];
  size_t translation_latency_sample_count;
  bool first_caption_recorded;
  bool live_clock_valid;
} vw_benchmark_t;

// Records translation telemetry for every attempted caption, including failed latency. Failures at/above the global
// translation deadline are classified as timeouts; earlier transport/parser failures are counted separately.
void vw_benchmark_record_translation(vw_benchmark_t* benchmark, uint8_t tier, uint32_t latency_us, bool success);

// Starts a bounded benchmark session, creates its private temporary report, and writes the initial active snapshot
// without recording transcript or PCM data.
bool vw_benchmark_begin(vw_benchmark_t* benchmark, const char* model_id, const char* backend, int64_t now_us);

// Records one successfully transmitted PCM chunk and establishes the live PTS-to-monotonic clock mapping if needed
// for latency calculations outside the realtime callback.
void vw_benchmark_record_audio(vw_benchmark_t* benchmark, int64_t start_pts_us, int64_t duration_us, int64_t now_us);

// Records one worker frame received by the plugin for the active benchmark session, including status, segment,
// progress, and error frames without retaining payload data.
void vw_benchmark_record_frame(vw_benchmark_t* benchmark);

// Records a valid caption and samples live utterance latency without comparing incompatible source clock domains,
// while accumulating bounded segment duration and UTF-8 size measurements.
void vw_benchmark_record_caption_received(vw_benchmark_t* benchmark, const vw_caption_segment_t* segment,
                                          int64_t now_us, bool source_mode);

// Records a caption rejected by the plugin and classifies whether pause, stale session, or presentation caused it,
// distinguishing accepted transport from visible submission.
void vw_benchmark_record_caption_filtered(vw_benchmark_t* benchmark, bool paused, bool stale, bool presenter_rejected);

// Records a caption submitted to the presenter and captures elapsed time to the first submitted caption using the
// monotonic session clock.
void vw_benchmark_record_caption_sent(vw_benchmark_t* benchmark, int64_t now_us);

// Copies the worker's cumulative inference and queue-drop measurements into the session report state, handling
// worker respawn counter resets safely.
void vw_benchmark_update_status(vw_benchmark_t* benchmark, const vw_msg_status_t* status);

// Writes a final benchmark snapshot with the supplied end time and marks the report complete after all sender-thread
// observations finish.
void vw_benchmark_finalize(vw_benchmark_t* benchmark, int64_t now_us);

// Flushes an active report at most once per second so an abrupt VLC exit leaves a useful bounded snapshot without
// unbounded disk activity.
void vw_benchmark_flush_if_due(vw_benchmark_t* benchmark, int64_t now_us);

#endif  // VW_BENCHMARK_H_
