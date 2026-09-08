#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_audio_buffer.h"
#include "vw_local_agreement.h"
#include "vw_log.h"
#include "vw_platform.h"
#include "vw_protocol_codec.h"
#include "vw_protocol_types.h"
#include "vw_protocol_util.h"
#include "vw_segment_builder.h"
#include "vw_vad.h"
#include "vw_whisper_engine.h"
#include "vw_worker_queue.h"
#include "whisper.h"

typedef enum vw_local_agreement_pending_control {
  VW_LOCAL_AGREEMENT_PENDING_NONE = 0,
  VW_LOCAL_AGREEMENT_PENDING_START,
  VW_LOCAL_AGREEMENT_PENDING_STOP,
} vw_local_agreement_pending_control_t;

typedef struct vw_local_agreement_runtime {
  bool live_session;
  bool window_pts_valid;
  bool collecting_hypothesis;
  bool hypothesis_invalid;
  bool pending_start_live_session;
  int expected_segments;
  int64_t window_pts_us;
  vw_segment_builder_t* builder;
  vw_local_agreement_pending_control_t pending_control;
  vw_local_agreement_t agreement;
  vw_local_agreement_word_t hypothesis[VW_LOCAL_AGREEMENT_MAX_WORDS];
  size_t hypothesis_count;
} vw_local_agreement_runtime_t;

static vw_local_agreement_runtime_t vw_la_runtime;
static bool vw_la_initialized = false;

static void vw_local_agreement_runtime_init_once(void) {
  if (vw_la_initialized) return;
  memset(&vw_la_runtime, 0, sizeof(vw_la_runtime));
  vw_local_agreement_init(&vw_la_runtime.agreement);
  vw_la_initialized = true;
}

static void vw_local_agreement_runtime_reset_hypothesis(void) {
  vw_local_agreement_runtime_init_once();
  vw_local_agreement_reset(&vw_la_runtime.agreement);
  vw_la_runtime.window_pts_valid = false;
  vw_la_runtime.collecting_hypothesis = false;
  vw_la_runtime.hypothesis_invalid = false;
  vw_la_runtime.expected_segments = 0;
  vw_la_runtime.hypothesis_count = 0;
}

static void vw_local_agreement_clear_previous_hypothesis(void) {
  (void)vw_local_agreement_update(&vw_la_runtime.agreement, NULL, 0, NULL, 0);
}

static bool vw_local_agreement_rebuild_last_text(vw_whisper_engine_t* engine) {
  if (!engine || !engine->ctx || !engine->last_text || engine->last_text_bytes == 0) return false;
  engine->last_text[0] = '\0';
  size_t written = 0;
  int n_segments = whisper_full_n_segments(engine->ctx);
  for (int i = 0; i < n_segments; i++) {
    const char* txt = whisper_full_get_segment_text(engine->ctx, i);
    if (!txt || txt[0] == '\0') continue;
    size_t len = strlen(txt);
    bool needs_space = (written > 0 && engine->last_text[written - 1] != ' ' && txt[0] != ' ');
    size_t extra = needs_space ? 1U : 0U;
    if (written + len + extra + 2U >= engine->last_text_bytes) {
      size_t new_cap = engine->last_text_bytes * 2U + len + extra + 2U;
      char* new_buf = (char*)realloc(engine->last_text, new_cap);
      if (!new_buf) return false;
      engine->last_text = new_buf;
      engine->last_text_bytes = new_cap;
    }
    if (needs_space) engine->last_text[written++] = ' ';
    memcpy(engine->last_text + written, txt, len);
    written += len;
    engine->last_text[written] = '\0';
  }
  return true;
}

bool vw_local_agreement_vad_detect_speech(const float* pcm, size_t sample_count, struct whisper_vad_context* ctx) {
  bool speech = vw_vad_detect_speech(pcm, sample_count, ctx);
  vw_local_agreement_runtime_init_once();
  if (vw_la_runtime.live_session && !speech) {
    vw_local_agreement_clear_previous_hypothesis();
    vw_log_event(VW_LOG_LEVEL_DEBUG, "WORKER_LOCAL_AGREEMENT", "VAD-skipped cadence; previous hypothesis cleared");
  }
  return speech;
}

// Worker-only remap for the experiment. Non-live sessions delegate to the production engine unchanged; live
// sessions use the same deterministic decode parameters but enable whisper.cpp token timestamps so LocalAgreement
// can measure and commit authentic tokenizer pieces rather than interpolated pseudo-word boundaries.
bool vw_local_agreement_transcribe_pcm(vw_whisper_engine_t* engine, const float* pcm32, size_t sample_count) {
  vw_local_agreement_runtime_init_once();
  if (!vw_la_runtime.live_session) return vw_whisper_engine_transcribe_pcm(engine, pcm32, sample_count);
  if (!engine || !engine->ctx || !pcm32 || sample_count == 0) {
    vw_local_agreement_clear_previous_hypothesis();
    return false;
  }

  struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  wparams.strategy = WHISPER_SAMPLING_GREEDY;
  wparams.temperature = 0.0f;
  wparams.temperature_inc = 0.2f;
  wparams.entropy_thold = 2.40f;
  wparams.logprob_thold = -1.00f;
  wparams.no_speech_thold = 0.60f;
  wparams.no_context = true;
  wparams.single_segment = false;
  wparams.suppress_blank = true;
  wparams.suppress_nst = true;
  wparams.print_special = false;
  wparams.max_len = 0;
  wparams.token_timestamps = true;
  wparams.translate = false;
  wparams.language = engine->language[0] != '\0' ? engine->language : "en";
  int thr = engine->n_threads;
  if (thr < 1) thr = 1;
  wparams.n_threads = thr;
  wparams.print_progress = false;
  wparams.print_realtime = false;
  wparams.print_timestamps = false;

  int64_t inference_started_us = vw_platform_get_monotonic_time_us();
  int inference_result = whisper_full(engine->ctx, wparams, pcm32, (int)sample_count);
  int64_t inference_elapsed_us = vw_platform_get_monotonic_time_us() - inference_started_us;
  if (inference_elapsed_us < 0) inference_elapsed_us = 0;
  engine->last_inference_us = (uint64_t)inference_elapsed_us;
  if (UINT64_MAX - engine->total_inference_us < (uint64_t)inference_elapsed_us) {
    engine->total_inference_us = UINT64_MAX;
  } else {
    engine->total_inference_us += (uint64_t)inference_elapsed_us;
  }
  if (inference_result != 0) {
    vw_local_agreement_clear_previous_hypothesis();
    vw_log_event(VW_LOG_LEVEL_DEBUG, "WORKER_LOCAL_AGREEMENT", "failed inference cadence; previous hypothesis cleared");
    return false;
  }
  if (!vw_local_agreement_rebuild_last_text(engine)) {
    vw_local_agreement_clear_previous_hypothesis();
    return false;
  }
  return true;
}

static bool vw_local_agreement_append_segment(const vw_whisper_engine_t* engine, int segment_index,
                                              const vw_whisper_segment_t* segment) {
  if (!engine || !engine->ctx || !segment || !vw_la_runtime.window_pts_valid || segment_index < 0) return false;

  int token_count = whisper_full_n_tokens(engine->ctx, segment_index);
  if (token_count < 0) return false;
  whisper_token eot = whisper_token_eot(engine->ctx);
  for (int token_index = 0; token_index < token_count; token_index++) {
    whisper_token token_id = whisper_full_get_token_id(engine->ctx, segment_index, token_index);
    if (token_id >= eot) continue;

    const char* token_text = whisper_full_get_token_text(engine->ctx, segment_index, token_index);
    if (!token_text || token_text[0] == '\0') continue;
    size_t bytes = strlen(token_text);
    if (bytes >= VW_LOCAL_AGREEMENT_WORD_BYTES || vw_la_runtime.hypothesis_count >= VW_LOCAL_AGREEMENT_MAX_WORDS) {
      return false;
    }

    int64_t token_t0 = whisper_full_get_token_t0(engine->ctx, segment_index, token_index);
    int64_t token_t1 = whisper_full_get_token_t1(engine->ctx, segment_index, token_index);
    if (token_t0 < 0 || token_t1 < token_t0) return false;

    vw_local_agreement_word_t* out = &vw_la_runtime.hypothesis[vw_la_runtime.hypothesis_count++];
    memset(out, 0, sizeof(*out));
    memcpy(out->text_utf8, token_text, bytes);
    out->text_utf8[bytes] = '\0';
    out->start_pts_us = vw_saturating_add_i64(vw_la_runtime.window_pts_us, token_t0 * 10000LL);
    out->end_pts_us = vw_saturating_add_i64(vw_la_runtime.window_pts_us, token_t1 * 10000LL);
    if (out->start_pts_us < 0 || out->end_pts_us < out->start_pts_us) return false;
  }
  return true;
}

static size_t vw_local_agreement_commit_chunk_words(const vw_local_agreement_word_t* words, size_t count) {
  size_t bytes = 0;
  size_t fit = 0;
  for (size_t i = 0; i < count; i++) {
    size_t token_bytes = strlen(words[i].text_utf8);
    if (bytes + token_bytes + 1U > VW_SEGMENT_BUILDER_MAX_TEXT_BYTES) break;
    bytes += token_bytes;
    fit++;
  }
  return fit;
}

static void vw_local_agreement_finalize_hypothesis(void) {
  if (!vw_la_runtime.live_session || !vw_la_runtime.collecting_hypothesis) return;
  vw_la_runtime.collecting_hypothesis = false;

  if (vw_la_runtime.hypothesis_invalid) {
    vw_local_agreement_clear_previous_hypothesis();
    vw_la_runtime.hypothesis_invalid = false;
    vw_la_runtime.hypothesis_count = 0;
    return;
  }

  vw_local_agreement_word_t committed[VW_LOCAL_AGREEMENT_MAX_WORDS];
  size_t committed_count =
      vw_local_agreement_update(&vw_la_runtime.agreement, vw_la_runtime.hypothesis, vw_la_runtime.hypothesis_count,
                                committed, VW_LOCAL_AGREEMENT_MAX_WORDS);
  vw_log_event(VW_LOG_LEVEL_DEBUG, "WORKER_LOCAL_AGREEMENT", "hypothesis_tokens=%zu committed_tokens=%zu",
               vw_la_runtime.hypothesis_count, committed_count);
  vw_la_runtime.hypothesis_count = 0;

  if (!vw_la_runtime.builder || committed_count == 0) return;
  size_t offset = 0;
  while (offset < committed_count) {
    size_t chunk_words = vw_local_agreement_commit_chunk_words(committed + offset, committed_count - offset);
    if (chunk_words == 0) break;
    char text[VW_SEGMENT_BUILDER_MAX_TEXT_BYTES];
    int64_t start_pts_us = 0;
    int64_t end_pts_us = 0;
    if (!vw_local_agreement_format_commit(committed + offset, chunk_words, text, sizeof(text), &start_pts_us,
                                          &end_pts_us)) {
      break;
    }
    if (end_pts_us > start_pts_us) {
      (void)vw_segment_builder_push_hypothesis(vw_la_runtime.builder, text, start_pts_us, end_pts_us);
    }
    offset += chunk_words;
  }
}

bool vw_local_agreement_worker_queue_pop(vw_worker_queue_t* queue, vw_worker_frame_t* out) {
  bool popped = vw_worker_queue_pop_prioritized(queue, out);
  if (!popped || !out) return popped;
  vw_local_agreement_runtime_init_once();

  // Stage session-mode transitions only. The worker validates session IDs and payload semantics after
  // dequeue; the staged transition is applied by vw_local_agreement_builder_clear() only on an accepted
  // START/STOP path. The next dequeue discards any staged intent left by a rejected or duplicate control.
  vw_la_runtime.pending_control = VW_LOCAL_AGREEMENT_PENDING_NONE;
  vw_la_runtime.pending_start_live_session = false;
  if (out->type == VW_MSG_START_SESSION && out->payload && out->payload_len > 0) {
    vw_msg_start_t start;
    memset(&start, 0, sizeof(start));
    if (vw_protocol_decode_payload(VW_MSG_START_SESSION, out->payload, out->payload_len, &start)) {
      vw_la_runtime.pending_control = VW_LOCAL_AGREEMENT_PENDING_START;
      vw_la_runtime.pending_start_live_session = (start.source_kind == VW_SOURCE_LIVE_AUDIO);
    }
  } else if (out->type == VW_MSG_STOP_SESSION) {
    vw_la_runtime.pending_control = VW_LOCAL_AGREEMENT_PENDING_STOP;
  }
  return popped;
}

void vw_local_agreement_builder_clear(vw_segment_builder_t* builder) {
  vw_local_agreement_runtime_init_once();
  vw_la_runtime.builder = builder;

  vw_local_agreement_pending_control_t accepted_control = vw_la_runtime.pending_control;
  bool accepted_start_live_session = vw_la_runtime.pending_start_live_session;
  vw_la_runtime.pending_control = VW_LOCAL_AGREEMENT_PENDING_NONE;
  vw_la_runtime.pending_start_live_session = false;

  vw_local_agreement_runtime_reset_hypothesis();
  if (accepted_control == VW_LOCAL_AGREEMENT_PENDING_START) {
    vw_la_runtime.live_session = accepted_start_live_session;
    vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_LOCAL_AGREEMENT", "session gate active=%d",
                 vw_la_runtime.live_session ? 1 : 0);
  } else if (accepted_control == VW_LOCAL_AGREEMENT_PENDING_STOP) {
    vw_la_runtime.live_session = false;
    vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_LOCAL_AGREEMENT", "session gate stopped");
  }
  vw_segment_builder_clear(builder);
}

bool vw_local_agreement_builder_push(vw_segment_builder_t* builder, const char* text, int64_t start_pts_us,
                                     int64_t end_pts_us) {
  vw_local_agreement_runtime_init_once();
  vw_la_runtime.builder = builder;
  if (vw_la_runtime.live_session) return true;
  return vw_segment_builder_push_hypothesis(builder, text, start_pts_us, end_pts_us);
}

size_t vw_local_agreement_audio_get_samples(const vw_audio_buffer_t* buffer, float* out_samples, size_t max_out,
                                            int64_t* out_pts_us) {
  size_t copied = vw_audio_buffer_get_samples(buffer, out_samples, max_out, out_pts_us);
  vw_local_agreement_runtime_init_once();
  if (vw_la_runtime.live_session && copied > 0 && out_pts_us) {
    vw_la_runtime.window_pts_us = *out_pts_us;
    vw_la_runtime.window_pts_valid = true;
  }
  return copied;
}

int vw_local_agreement_segment_count(const vw_whisper_engine_t* engine) {
  int count = vw_whisper_engine_get_segment_count(engine);
  vw_local_agreement_runtime_init_once();
  if (vw_la_runtime.live_session) {
    vw_la_runtime.hypothesis_count = 0;
    vw_la_runtime.hypothesis_invalid = false;
    vw_la_runtime.expected_segments = count;
    vw_la_runtime.collecting_hypothesis = count > 0;
    if (count == 0) {
      vw_local_agreement_clear_previous_hypothesis();
      vw_log_event(VW_LOG_LEVEL_DEBUG, "WORKER_LOCAL_AGREEMENT", "empty inference pass; previous hypothesis cleared");
    }
  }
  return count;
}

bool vw_local_agreement_get_segment(const vw_whisper_engine_t* engine, int index, vw_whisper_segment_t* out_segment) {
  bool ok = vw_whisper_engine_get_segment(engine, index, out_segment);
  vw_local_agreement_runtime_init_once();
  if (!vw_la_runtime.live_session || !vw_la_runtime.collecting_hypothesis) return ok;

  if (ok && out_segment && out_segment->no_speech_prob < 0.60f && !vw_la_runtime.hypothesis_invalid) {
    if (!vw_local_agreement_append_segment(engine, index, out_segment)) {
      vw_log_event(VW_LOG_LEVEL_WARN, "WORKER_LOCAL_AGREEMENT",
                   "token hypothesis invalid or capacity exceeded; withholding pass");
      vw_la_runtime.hypothesis_invalid = true;
      vw_la_runtime.hypothesis_count = 0;
    }
  }

  if (index + 1 >= vw_la_runtime.expected_segments) vw_local_agreement_finalize_hypothesis();
  return ok;
}
