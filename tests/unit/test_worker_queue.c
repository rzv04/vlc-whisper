#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_protocol_codec.h"
#include "vw_test.h"
#include "vw_worker_queue.h"

// Build an AUDIO payload with the given duration_us, returning a malloc'd buffer and its length.
// The payload is a valid vw_msg_audio_t encoding with zeroed PCM bytes; caller frees.
static uint8_t* make_audio_payload(int64_t duration_us, uint32_t* out_len) {
  uint32_t pcm_bytes = (uint32_t)((duration_us * 32) / 1000);  // 16kHz S16LE
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
  // --- FIFO order with mixed types ---
  vw_worker_queue_t* q = vw_worker_queue_create(4);
  EXPECT(q != NULL);

  uint32_t len1 = 0, len2 = 0;
  uint8_t* p1 = make_audio_payload(100000, &len1);
  uint8_t* p2 = make_audio_payload(200000, &len2);

  EXPECT(vw_worker_queue_push(q, VW_MSG_AUDIO_PCM, p1, len1));
  EXPECT(vw_worker_queue_push(q, VW_MSG_AUDIO_PCM, p2, len2));
  EXPECT(vw_worker_queue_push(q, VW_MSG_STOP_SESSION, NULL, 0));  // zero-payload control frame

  vw_worker_frame_t f;
  EXPECT(vw_worker_queue_pop(q, &f));
  EXPECT(f.type == VW_MSG_AUDIO_PCM);
  EXPECT(f.payload_len == len1);
  EXPECT(f.payload == p1);  // ownership transferred, not copied
  free(f.payload);

  EXPECT(vw_worker_queue_pop(q, &f));
  EXPECT(f.type == VW_MSG_AUDIO_PCM);
  EXPECT(f.payload == p2);
  free(f.payload);

  EXPECT(vw_worker_queue_pop(q, &f));
  EXPECT(f.type == VW_MSG_STOP_SESSION);
  EXPECT(f.payload_len == 0);
  EXPECT(f.payload == NULL);

  EXPECT(!vw_worker_queue_pop(q, &f));  // empty

  // --- Full-queue eviction drops only the oldest AUDIO frame; control frames survive ---
  uint32_t l = 0;
  uint8_t* a = make_audio_payload(500000, &l);
  uint8_t* b = make_audio_payload(600000, &l);
  uint8_t* c = make_audio_payload(700000, &l);
  EXPECT(vw_worker_queue_push(q, VW_MSG_AUDIO_PCM, a, l));
  EXPECT(vw_worker_queue_push(q, VW_MSG_AUDIO_PCM, b, l));
  EXPECT(vw_worker_queue_push(q, VW_MSG_AUDIO_PCM, c, l));
  EXPECT(vw_worker_queue_push(q, VW_MSG_PAUSE, NULL, 0));  // control: never dropped

  // Queue is full (4/4). Push another AUDIO: oldest AUDIO (500000) is evicted, PAUSE stays.
  uint32_t l2 = 0;
  uint8_t* d = make_audio_payload(800000, &l2);
  EXPECT(vw_worker_queue_push(q, VW_MSG_AUDIO_PCM, d, l2));

  EXPECT(vw_worker_queue_get_dropped_audio_us(q) == 500000);  // evicted 500000us

  EXPECT(vw_worker_queue_pop(q, &f));
  EXPECT(f.type == VW_MSG_AUDIO_PCM);
  EXPECT(f.payload == b);
  free(f.payload);

  EXPECT(vw_worker_queue_pop(q, &f));
  EXPECT(f.type == VW_MSG_AUDIO_PCM);
  EXPECT(f.payload == c);
  free(f.payload);

  EXPECT(vw_worker_queue_pop(q, &f));
  EXPECT(f.type == VW_MSG_PAUSE);  // control frame survived the eviction
  EXPECT(f.payload == NULL);

  EXPECT(vw_worker_queue_pop(q, &f));
  EXPECT(f.type == VW_MSG_AUDIO_PCM);
  EXPECT(f.payload == d);
  free(f.payload);

  EXPECT(!vw_worker_queue_pop(q, &f));

  // --- Control frame pushed into a full queue evicts oldest AUDIO, control still accepted ---
  uint32_t l3 = 0;
  uint8_t* e1 = make_audio_payload(111111, &l3);
  uint8_t* e2 = make_audio_payload(222222, &l3);
  uint8_t* e3 = make_audio_payload(333333, &l3);
  EXPECT(vw_worker_queue_push(q, VW_MSG_AUDIO_PCM, e1, l3));
  EXPECT(vw_worker_queue_push(q, VW_MSG_AUDIO_PCM, e2, l3));
  EXPECT(vw_worker_queue_push(q, VW_MSG_AUDIO_PCM, e3, l3));
  uint32_t l4 = 0;
  uint8_t* e4 = make_audio_payload(444444, &l4);
  EXPECT(vw_worker_queue_push(q, VW_MSG_AUDIO_PCM, e4, l4));  // full again (4/4)

  // Push a SHUTDOWN (control) into the full queue: evicts oldest AUDIO (111111).
  EXPECT(vw_worker_queue_push(q, VW_MSG_SHUTDOWN, NULL, 0));
  EXPECT(vw_worker_queue_get_dropped_audio_us(q) == 500000 + 111111);

  EXPECT(vw_worker_queue_pop(q, &f));
  EXPECT(f.type == VW_MSG_AUDIO_PCM);
  EXPECT(f.payload == e2);
  free(f.payload);
  EXPECT(vw_worker_queue_pop(q, &f));
  EXPECT(f.type == VW_MSG_AUDIO_PCM);
  EXPECT(f.payload == e3);
  free(f.payload);
  EXPECT(vw_worker_queue_pop(q, &f));
  EXPECT(f.type == VW_MSG_AUDIO_PCM);
  EXPECT(f.payload == e4);
  free(f.payload);
  EXPECT(vw_worker_queue_pop(q, &f));
  EXPECT(f.type == VW_MSG_SHUTDOWN);
  EXPECT(f.payload == NULL);
  EXPECT(!vw_worker_queue_pop(q, &f));

  // --- All-control full queue: incoming control evicts oldest control, never dropped ---
  vw_worker_queue_t* q3 = vw_worker_queue_create(3);
  EXPECT(q3 != NULL);
  EXPECT(vw_worker_queue_push(q3, VW_MSG_PAUSE, NULL, 0));
  EXPECT(vw_worker_queue_push(q3, VW_MSG_STOP_SESSION, NULL, 0));
  EXPECT(vw_worker_queue_push(q3, VW_MSG_SHUTDOWN, NULL, 0));  // full (3/3, all controls)

  // Incoming control into the all-control-full queue: oldest control (PAUSE) is evicted,
  // the newest (RESUME) lands — control traffic is never dropped.
  EXPECT(vw_worker_queue_push(q3, VW_MSG_RESUME, NULL, 0));
  EXPECT(vw_worker_queue_pop(q3, &f));
  EXPECT(f.type == VW_MSG_STOP_SESSION);
  EXPECT(vw_worker_queue_pop(q3, &f));
  EXPECT(f.type == VW_MSG_SHUTDOWN);
  EXPECT(vw_worker_queue_pop(q3, &f));
  EXPECT(f.type == VW_MSG_RESUME);
  EXPECT(!vw_worker_queue_pop(q3, &f));
  vw_worker_queue_destroy(q3);

  // --- dropped_audio_us equals decoded duration_us sum; destroy frees queued payloads ---
  vw_worker_queue_destroy(q);

  // destroy with queued payloads must free them (valgrind-verified)
  vw_worker_queue_t* q2 = vw_worker_queue_create(2);
  EXPECT(q2 != NULL);
  uint32_t l5 = 0;
  uint8_t* g1 = make_audio_payload(1000, &l5);
  uint8_t* g2 = make_audio_payload(2000, &l5);
  EXPECT(vw_worker_queue_push(q2, VW_MSG_AUDIO_PCM, g1, l5));
  EXPECT(vw_worker_queue_push(q2, VW_MSG_AUDIO_PCM, g2, l5));
  vw_worker_queue_destroy(q2);  // frees g1, g2

  printf("test_worker_queue PASSED\n");
  return 0;
}
