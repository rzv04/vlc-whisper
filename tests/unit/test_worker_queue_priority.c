#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_protocol_codec.h"
#include "vw_test.h"
#include "vw_worker_queue.h"

static void fill_session(vw_session_id_t* session_id, uint8_t marker) {
  memset(session_id->bytes, marker, VW_SESSION_ID_BYTES);
}

static uint8_t* make_audio_payload(int64_t duration_us, uint8_t session_marker, uint32_t* out_len) {
  uint32_t pcm_bytes = (uint32_t)((duration_us * 32) / 1000);
  uint8_t* pcm = (uint8_t*)calloc(1, pcm_bytes);
  EXPECT(pcm != NULL);
  vw_msg_audio_t audio = {.start_pts_us = 0, .duration_us = duration_us, .pcm_bytes = pcm_bytes, .pcm_data = pcm};
  fill_session(&audio.session_id, session_marker);
  uint8_t* buf = (uint8_t*)malloc(pcm_bytes + 64);
  EXPECT(buf != NULL);
  size_t written = 0;
  EXPECT(vw_protocol_encode_payload(VW_MSG_AUDIO_PCM, &audio, buf, pcm_bytes + 64, &written));
  free(pcm);
  *out_len = (uint32_t)written;
  return buf;
}

static uint8_t* make_control_payload(uint16_t type, uint8_t session_marker, uint32_t* out_len) {
  vw_msg_control_t control = {.reason = VW_CTRL_REASON_USER_STOP};
  fill_session(&control.session_id, session_marker);
  uint8_t* buf = (uint8_t*)malloc(128);
  EXPECT(buf != NULL);
  size_t written = 0;
  EXPECT(vw_protocol_encode_payload(type, &control, buf, 128, &written));
  *out_len = (uint32_t)written;
  return buf;
}

static uint8_t* make_start_payload(uint8_t session_marker, uint32_t* out_len) {
  vw_msg_start_t start = {.timeline_origin_pts_us = 0,
                          .sample_rate = 16000,
                          .channels = 1,
                          .sample_format = 1,
                          .model_id = "tiny",
                          .language = "en",
                          .source_kind = VW_SOURCE_LIVE_AUDIO};
  fill_session(&start.session_id, session_marker);
  uint8_t* buf = (uint8_t*)malloc(512);
  EXPECT(buf != NULL);
  size_t written = 0;
  EXPECT(vw_protocol_encode_payload(VW_MSG_START_SESSION, &start, buf, 512, &written));
  *out_len = (uint32_t)written;
  return buf;
}

static uint8_t* make_position_payload(uint8_t session_marker, uint32_t flags, uint32_t* out_len) {
  vw_msg_position_t position = {
      .current_pts_us = 1000000, .input_time_us = 1000000, .playback_rate = 1.0f, .flags = flags};
  fill_session(&position.session_id, session_marker);
  uint8_t* buf = (uint8_t*)malloc(128);
  EXPECT(buf != NULL);
  size_t written = 0;
  EXPECT(vw_protocol_encode_payload(VW_MSG_POSITION, &position, buf, 128, &written));
  *out_len = (uint32_t)written;
  return buf;
}

int main(void) {
  vw_worker_queue_t* q = vw_worker_queue_create(VW_WORKER_FRAME_QUEUE_CAPACITY);
  EXPECT(q != NULL);

  uint32_t l1 = 0, l2 = 0, l3 = 0, pause_len = 0;
  uint8_t* old1 = make_audio_payload(100000, 1, &l1);
  uint8_t* old2 = make_audio_payload(200000, 1, &l2);
  uint8_t* fresh = make_audio_payload(300000, 1, &l3);
  uint8_t* pause = make_control_payload(VW_MSG_PAUSE, 1, &pause_len);
  EXPECT(vw_worker_queue_push(q, VW_MSG_AUDIO_PCM, old1, l1));
  EXPECT(vw_worker_queue_push(q, VW_MSG_AUDIO_PCM, old2, l2));
  EXPECT(vw_worker_queue_push(q, VW_MSG_TRANSLATE_CTRL, NULL, 0));
  EXPECT(vw_worker_queue_push(q, VW_MSG_PAUSE, pause, pause_len));
  EXPECT(vw_worker_queue_push(q, VW_MSG_AUDIO_PCM, fresh, l3));

  vw_worker_frame_t frame;
  EXPECT(vw_worker_queue_pop_prioritized(q, &frame));
  EXPECT(frame.type == VW_MSG_PAUSE);
  free(frame.payload);
  EXPECT(vw_worker_queue_get_dropped_audio_us(q) == 300000);

  EXPECT(vw_worker_queue_pop_prioritized(q, &frame));
  EXPECT(frame.type == VW_MSG_TRANSLATE_CTRL);
  EXPECT(frame.payload == NULL);

  EXPECT(vw_worker_queue_pop_prioritized(q, &frame));
  EXPECT(frame.type == VW_MSG_AUDIO_PCM);
  EXPECT(frame.payload == fresh);
  free(frame.payload);
  EXPECT(!vw_worker_queue_pop_prioritized(q, &frame));
  vw_worker_queue_destroy(q);

  // Wrong-session PAUSE must stay FIFO and preserve valid queued PCM.
  vw_worker_queue_t* wrong = vw_worker_queue_create(4);
  EXPECT(wrong != NULL);
  uint32_t wrong_audio_len = 0, wrong_pause_len = 0;
  uint8_t* wrong_audio = make_audio_payload(125000, 2, &wrong_audio_len);
  uint8_t* wrong_pause = make_control_payload(VW_MSG_PAUSE, 3, &wrong_pause_len);
  EXPECT(vw_worker_queue_push(wrong, VW_MSG_AUDIO_PCM, wrong_audio, wrong_audio_len));
  EXPECT(vw_worker_queue_push(wrong, VW_MSG_PAUSE, wrong_pause, wrong_pause_len));
  EXPECT(vw_worker_queue_pop_prioritized(wrong, &frame));
  EXPECT(frame.type == VW_MSG_AUDIO_PCM);
  EXPECT(frame.payload == wrong_audio);
  free(frame.payload);
  EXPECT(vw_worker_queue_get_dropped_audio_us(wrong) == 0);
  EXPECT(vw_worker_queue_pop_prioritized(wrong, &frame));
  EXPECT(frame.type == VW_MSG_PAUSE);
  free(frame.payload);
  vw_worker_queue_destroy(wrong);

  // Duplicate START for the same audio epoch must not preempt or invalidate queued PCM.
  vw_worker_queue_t* duplicate = vw_worker_queue_create(4);
  EXPECT(duplicate != NULL);
  uint32_t duplicate_audio_len = 0, duplicate_start_len = 0;
  uint8_t* duplicate_audio = make_audio_payload(150000, 4, &duplicate_audio_len);
  uint8_t* duplicate_start = make_start_payload(4, &duplicate_start_len);
  EXPECT(vw_worker_queue_push(duplicate, VW_MSG_AUDIO_PCM, duplicate_audio, duplicate_audio_len));
  EXPECT(vw_worker_queue_push(duplicate, VW_MSG_START_SESSION, duplicate_start, duplicate_start_len));
  EXPECT(vw_worker_queue_pop_prioritized(duplicate, &frame));
  EXPECT(frame.type == VW_MSG_AUDIO_PCM);
  EXPECT(frame.payload == duplicate_audio);
  free(frame.payload);
  EXPECT(vw_worker_queue_get_dropped_audio_us(duplicate) == 0);
  EXPECT(vw_worker_queue_pop_prioritized(duplicate, &frame));
  EXPECT(frame.type == VW_MSG_START_SESSION);
  free(frame.payload);
  vw_worker_queue_destroy(duplicate);

  // Replacement START for a different epoch preempts and invalidates old-session audio.
  vw_worker_queue_t* replacement = vw_worker_queue_create(4);
  EXPECT(replacement != NULL);
  uint32_t replacement_audio_len = 0, replacement_start_len = 0;
  uint8_t* replacement_audio = make_audio_payload(175000, 5, &replacement_audio_len);
  uint8_t* replacement_start = make_start_payload(6, &replacement_start_len);
  EXPECT(vw_worker_queue_push(replacement, VW_MSG_AUDIO_PCM, replacement_audio, replacement_audio_len));
  EXPECT(vw_worker_queue_push(replacement, VW_MSG_START_SESSION, replacement_start, replacement_start_len));
  EXPECT(vw_worker_queue_pop_prioritized(replacement, &frame));
  EXPECT(frame.type == VW_MSG_START_SESSION);
  free(frame.payload);
  EXPECT(vw_worker_queue_get_dropped_audio_us(replacement) == 175000);
  EXPECT(!vw_worker_queue_pop_prioritized(replacement, &frame));
  vw_worker_queue_destroy(replacement);

  // A same-session PAUSE supersedes older unpaused POSITION state; the stale position must not replay afterward.
  vw_worker_queue_t* pause_position = vw_worker_queue_create(4);
  EXPECT(pause_position != NULL);
  uint32_t position_len = 0, position_pause_len = 0;
  uint8_t* position = make_position_payload(8, 0, &position_len);
  uint8_t* position_pause = make_control_payload(VW_MSG_PAUSE, 8, &position_pause_len);
  EXPECT(vw_worker_queue_push(pause_position, VW_MSG_POSITION, position, position_len));
  EXPECT(vw_worker_queue_push(pause_position, VW_MSG_PAUSE, position_pause, position_pause_len));
  EXPECT(vw_worker_queue_pop_prioritized(pause_position, &frame));
  EXPECT(frame.type == VW_MSG_PAUSE);
  free(frame.payload);
  EXPECT(vw_worker_queue_get_dropped_audio_us(pause_position) == 0);
  EXPECT(!vw_worker_queue_pop_prioritized(pause_position, &frame));
  vw_worker_queue_destroy(pause_position);

  // A same-session RESUME likewise supersedes an older paused POSITION snapshot.
  vw_worker_queue_t* resume_position = vw_worker_queue_create(4);
  EXPECT(resume_position != NULL);
  uint32_t paused_position_len = 0, resume_len = 0;
  uint8_t* paused_position = make_position_payload(9, VW_POSITION_FLAG_PAUSED, &paused_position_len);
  uint8_t* resume = make_control_payload(VW_MSG_RESUME, 9, &resume_len);
  EXPECT(vw_worker_queue_push(resume_position, VW_MSG_POSITION, paused_position, paused_position_len));
  EXPECT(vw_worker_queue_push(resume_position, VW_MSG_RESUME, resume, resume_len));
  EXPECT(vw_worker_queue_pop_prioritized(resume_position, &frame));
  EXPECT(frame.type == VW_MSG_RESUME);
  free(frame.payload);
  EXPECT(!vw_worker_queue_pop_prioritized(resume_position, &frame));
  vw_worker_queue_destroy(resume_position);

  // Wrong-session lifecycle controls must not jump ahead of a POSITION belonging to another active epoch.
  vw_worker_queue_t* wrong_position = vw_worker_queue_create(4);
  EXPECT(wrong_position != NULL);
  uint32_t wrong_position_len = 0, wrong_position_pause_len = 0;
  uint8_t* active_position = make_position_payload(10, 0, &wrong_position_len);
  uint8_t* stale_pause = make_control_payload(VW_MSG_PAUSE, 11, &wrong_position_pause_len);
  EXPECT(vw_worker_queue_push(wrong_position, VW_MSG_POSITION, active_position, wrong_position_len));
  EXPECT(vw_worker_queue_push(wrong_position, VW_MSG_PAUSE, stale_pause, wrong_position_pause_len));
  EXPECT(vw_worker_queue_pop_prioritized(wrong_position, &frame));
  EXPECT(frame.type == VW_MSG_POSITION);
  EXPECT(frame.payload == active_position);
  free(frame.payload);
  EXPECT(vw_worker_queue_pop_prioritized(wrong_position, &frame));
  EXPECT(frame.type == VW_MSG_PAUSE);
  free(frame.payload);
  vw_worker_queue_destroy(wrong_position);

  // Authentication ordering is never bypassed even when START is already queued behind HELLO.
  vw_worker_queue_t* auth = vw_worker_queue_create(4);
  EXPECT(auth != NULL);
  uint32_t auth_start_len = 0;
  uint8_t* auth_start = make_start_payload(7, &auth_start_len);
  EXPECT(vw_worker_queue_push(auth, VW_MSG_HELLO, NULL, 0));
  EXPECT(vw_worker_queue_push(auth, VW_MSG_START_SESSION, auth_start, auth_start_len));
  EXPECT(vw_worker_queue_pop_prioritized(auth, &frame));
  EXPECT(frame.type == VW_MSG_HELLO);
  EXPECT(vw_worker_queue_pop_prioritized(auth, &frame));
  EXPECT(frame.type == VW_MSG_START_SESSION);
  free(frame.payload);
  EXPECT(!vw_worker_queue_pop_prioritized(auth, &frame));
  vw_worker_queue_destroy(auth);

  printf("test_worker_queue_priority PASSED\n");
  return 0;
}
