#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vw_audio_buffer.h"
#include "vw_local_agreement.h"
#include "vw_log.h"
#include "vw_protocol_codec.h"
#include "vw_protocol_types.h"
#include "vw_segment_builder.h"
#include "vw_whisper_engine.h"
#include "vw_worker_queue.h"

typedef struct vw_local_agreement_runtime {
  bool live_session;
  bool window_pts_valid;
  bool collecting_hypothesis;
  int expected_segments;
  int64_t window_pts_us;
  vw_segment_builder_t* builder;
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
  vw_la_runtime.expected_segments = 0;
  vw_la_runtime.hypothesis_count = 0;
}

static size_t vw_local_agreement_count_words(const char* text) {
  if (!text) return 0;
  size_t count = 0;
  bool in_word = false;
  for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
    if (isspace(*p)) {
      in_word = false;
    } else if (!in_word) {
      in_word = true;
      count++;
    }
  }
  return count;
}

static bool vw_local_agreement_append_segment(const vw_whisper_segment_t* segment) {
  if (!segment || !segment->text_utf8 || segment->text_utf8[0] == '\0' || !vw_la_runtime.window_pts_valid) {
    return false;
  }

  size_t word_count = vw_local_agreement_count_words(segment->text_utf8);
  if (word_count == 0) return true;
  if (vw_la_runtime.hypothesis_count + word_count > VW_LOCAL_AGREEMENT_MAX_WORDS) return false;

  int64_t segment_start = vw_la_runtime.window_pts_us + segment->t0_us;
  int64_t segment_end = vw_la_runtime.window_pts_us + segment->t1_us;
  if (segment_start < 0 || segment_end < segment_start) return false;
  int64_t duration = segment_end - segment_start;

  const unsigned char* p = (const unsigned char*)segment->text_utf8;
  size_t word_index = 0;
  while (*p) {
    while (*p && isspace(*p)) p++;
    if (!*p) break;
    const unsigned char* begin = p;
    while (*p && !isspace(*p)) p++;
    size_t bytes = (size_t)(p - begin);
    if (bytes == 0 || bytes >= VW_LOCAL_AGREEMENT_WORD_BYTES) return false;

    vw_local_agreement_word_t* out = &vw_la_runtime.hypothesis[vw_la_runtime.hypothesis_count++];
    memset(out, 0, sizeof(*out));
    memcpy(out->text_utf8, begin, bytes);
    out->text_utf8[bytes] = '\0';
    out->start_pts_us = segment_start + (int64_t)((duration * (int64_t)word_index) / (int64_t)word_count);
    word_index++;
    out->end_pts_us = segment_start + (int64_t)((duration * (int64_t)word_index) / (int64_t)word_count);
  }
  return true;
}

static size_t vw_local_agreement_commit_chunk_words(const vw_local_agreement_word_t* words, size_t count) {
  size_t bytes = 0;
  size_t fit = 0;
  for (size_t i = 0; i < count; i++) {
    size_t word_bytes = strlen(words[i].text_utf8);
    size_t extra = word_bytes + (i > 0 ? 1U : 0U);
    if (bytes + extra + 1U > VW_SEGMENT_BUILDER_MAX_TEXT_BYTES) break;
    bytes += extra;
    fit++;
  }
  return fit;
}

static void vw_local_agreement_finalize_hypothesis(void) {
  if (!vw_la_runtime.live_session || !vw_la_runtime.collecting_hypothesis) return;
  vw_la_runtime.collecting_hypothesis = false;

  vw_local_agreement_word_t committed[VW_LOCAL_AGREEMENT_MAX_WORDS];
  size_t committed_count = vw_local_agreement_update(&vw_la_runtime.agreement, vw_la_runtime.hypothesis,
                                                      vw_la_runtime.hypothesis_count, committed,
                                                      VW_LOCAL_AGREEMENT_MAX_WORDS);
  vw_log_event(VW_LOG_LEVEL_DEBUG, "WORKER_LOCAL_AGREEMENT", "hypothesis_words=%zu committed_words=%zu",
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
  bool popped = vw_worker_queue_pop(queue, out);
  if (!popped || !out) return popped;
  vw_local_agreement_runtime_init_once();

  if (out->type == VW_MSG_START_SESSION && out->payload && out->payload_len > 0) {
    vw_msg_start_t start;
    memset(&start, 0, sizeof(start));
    if (vw_protocol_decode_payload(VW_MSG_START_SESSION, out->payload, out->payload_len, &start)) {
      vw_local_agreement_runtime_reset_hypothesis();
      vw_la_runtime.live_session = (start.source_kind == VW_SOURCE_LIVE_AUDIO);
      vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_LOCAL_AGREEMENT", "session gate active=%d",
                   vw_la_runtime.live_session ? 1 : 0);
    }
  } else if (out->type == VW_MSG_PAUSE || out->type == VW_MSG_RESUME) {
    vw_local_agreement_runtime_reset_hypothesis();
  } else if (out->type == VW_MSG_STOP_SESSION || out->type == VW_MSG_SHUTDOWN) {
    vw_local_agreement_runtime_reset_hypothesis();
    vw_la_runtime.live_session = false;
  }
  return popped;
}

void vw_local_agreement_builder_clear(vw_segment_builder_t* builder) {
  vw_local_agreement_runtime_init_once();
  vw_la_runtime.builder = builder;
  vw_local_agreement_runtime_reset_hypothesis();
  vw_segment_builder_clear(builder);
}

bool vw_local_agreement_builder_push(vw_segment_builder_t* builder, const char* text, int64_t start_pts_us,
                                     int64_t end_pts_us) {
  vw_local_agreement_runtime_init_once();
  vw_la_runtime.builder = builder;
  if (vw_la_runtime.live_session) {
    return true;
  }
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
    vw_la_runtime.expected_segments = count;
    vw_la_runtime.collecting_hypothesis = count > 0;
  }
  return count;
}

bool vw_local_agreement_get_segment(const vw_whisper_engine_t* engine, int index, vw_whisper_segment_t* out_segment) {
  bool ok = vw_whisper_engine_get_segment(engine, index, out_segment);
  vw_local_agreement_runtime_init_once();
  if (!vw_la_runtime.live_session || !vw_la_runtime.collecting_hypothesis) return ok;

  if (ok && out_segment && out_segment->no_speech_prob < 0.60f) {
    if (!vw_local_agreement_append_segment(out_segment)) {
      vw_log_event(VW_LOG_LEVEL_WARN, "WORKER_LOCAL_AGREEMENT", "word hypothesis capacity exceeded; withholding pass");
      vw_la_runtime.hypothesis_count = 0;
    }
  }

  if (index + 1 >= vw_la_runtime.expected_segments) {
    vw_local_agreement_finalize_hypothesis();
  }
  return ok;
}
