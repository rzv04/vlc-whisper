#include <stdio.h>
#include <stdlib.h>

#include "vw_protocol_codec.h"
#include "vw_test.h"
#include "vw_worker_queue.h"

static uint8_t* make_audio_payload(int64_t duration_us, uint32_t* out_len) {
  uint32_t pcm_bytes = (uint32_t)((duration_us * 32) / 1000);
  uint8_t* pcm = (uint8_t*)calloc(1, pcm_bytes);
  EXPECT(pcm != NULL);
  vw_msg_audio_t audio = {.start_pts_us = 0, .duration_us = duration_us, .pcm_bytes = pcm_bytes, .pcm_data = pcm};
  uint8_t* buf = (uint8_t*)malloc(pcm_bytes + 64);
  EXPECT(buf != NULL);
  size_t written = 0;
  EXPECT(vw_protocol_encode_payload(VW_MSG_AUDIO_PCM, &audio, buf, pcm_bytes + 64, &written));
  free(pcm);
  *out_len = (uint32_t)written;
  return buf;
}

int main(void) {
  vw_worker_queue_t* q = vw_worker_queue_create(VW_WORKER_FRAME_QUEUE_CAPACITY);
  EXPECT(q != NULL);

  uint32_t l1 = 0, l2 = 0, l3 = 0;
  uint8_t* old1 = make_audio_payload(100000, &l1);
  uint8_t* old2 = make_audio_payload(200000, &l2);
  uint8_t* fresh = make_audio_payload(300000, &l3);
  EXPECT(vw_worker_queue_push(q, VW_MSG_AUDIO_PCM, old1, l1));
  EXPECT(vw_worker_queue_push(q, VW_MSG_AUDIO_PCM, old2, l2));
  EXPECT(vw_worker_queue_push(q, VW_MSG_TRANSLATE_CTRL, NULL, 0));
  EXPECT(vw_worker_queue_push(q, VW_MSG_PAUSE, NULL, 0));
  EXPECT(vw_worker_queue_push(q, VW_MSG_AUDIO_PCM, fresh, l3));

  vw_worker_frame_t frame;
  EXPECT(vw_worker_queue_pop_prioritized(q, &frame));
  EXPECT(frame.type == VW_MSG_PAUSE);
  EXPECT(frame.payload == NULL);
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

  // Authentication ordering is never bypassed even when START is already queued behind HELLO.
  vw_worker_queue_t* auth = vw_worker_queue_create(4);
  EXPECT(auth != NULL);
  EXPECT(vw_worker_queue_push(auth, VW_MSG_HELLO, NULL, 0));
  EXPECT(vw_worker_queue_push(auth, VW_MSG_START_SESSION, NULL, 0));
  EXPECT(vw_worker_queue_pop_prioritized(auth, &frame));
  EXPECT(frame.type == VW_MSG_HELLO);
  EXPECT(vw_worker_queue_pop_prioritized(auth, &frame));
  EXPECT(frame.type == VW_MSG_START_SESSION);
  EXPECT(!vw_worker_queue_pop_prioritized(auth, &frame));
  vw_worker_queue_destroy(auth);

  printf("test_worker_queue_priority PASSED\n");
  return 0;
}
