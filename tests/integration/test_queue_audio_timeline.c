#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_audio_buffer.h"
#include "vw_protocol_codec.h"
#include "vw_worker_queue.h"

static int g_failures = 0;

static void check_true(const char* name, bool condition) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", name);
    g_failures++;
  }
}

static uint8_t* make_audio_payload(int64_t start_pts_us, int64_t duration_us, uint32_t* out_len) {
  uint32_t pcm_bytes = (uint32_t)((duration_us * 32) / 1000);
  uint8_t* pcm = (uint8_t*)calloc(1, pcm_bytes);
  if (!pcm) return NULL;

  vw_msg_audio_t audio = {
      .start_pts_us = start_pts_us, .duration_us = duration_us, .pcm_bytes = pcm_bytes, .pcm_data = pcm};
  uint8_t* payload = (uint8_t*)malloc((size_t)pcm_bytes + 128U);
  if (!payload) {
    free(pcm);
    return NULL;
  }

  size_t written = 0;
  bool encoded = vw_protocol_encode_payload(VW_MSG_AUDIO_PCM, &audio, payload, (size_t)pcm_bytes + 128U, &written);
  free(pcm);
  if (!encoded || written > UINT32_MAX) {
    free(payload);
    return NULL;
  }

  *out_len = (uint32_t)written;
  return payload;
}

static int64_t sample_span_us(size_t sample_count) {
  return (int64_t)((sample_count * 1000000ULL) / 16000ULL);
}

int main(void) {
  const int64_t frame_duration_us = 100000;
  const size_t frame_samples = 1600;
  int16_t initial_pcm[1600] = {0};

  vw_audio_buffer_t* audio_buffer = vw_audio_buffer_create(16000);
  check_true("audio buffer creation", audio_buffer != NULL);
  if (!audio_buffer) return 1;

  check_true("initial contiguous audio append",
             vw_audio_buffer_append_s16le(audio_buffer, initial_pcm, frame_samples, 1000000));

  vw_worker_queue_t* queue = vw_worker_queue_create(3);
  check_true("worker queue creation", queue != NULL);
  if (!queue) {
    vw_audio_buffer_free(audio_buffer);
    return 1;
  }

  uint32_t len_b = 0;
  uint32_t len_c = 0;
  uint32_t len_d = 0;
  uint32_t len_e = 0;
  uint8_t* payload_b = make_audio_payload(1100000, frame_duration_us, &len_b);
  uint8_t* payload_c = make_audio_payload(1200000, frame_duration_us, &len_c);
  uint8_t* payload_d = make_audio_payload(1300000, frame_duration_us, &len_d);
  uint8_t* payload_e = make_audio_payload(1400000, frame_duration_us, &len_e);

  check_true("payload B allocation", payload_b != NULL);
  check_true("payload C allocation", payload_c != NULL);
  check_true("payload D allocation", payload_d != NULL);
  check_true("payload E allocation", payload_e != NULL);
  if (!payload_b || !payload_c || !payload_d || !payload_e) {
    free(payload_b);
    free(payload_c);
    free(payload_d);
    free(payload_e);
    vw_worker_queue_destroy(queue);
    vw_audio_buffer_free(audio_buffer);
    return 1;
  }

  check_true("queue B", vw_worker_queue_push(queue, VW_MSG_AUDIO_PCM, payload_b, len_b));
  check_true("queue C", vw_worker_queue_push(queue, VW_MSG_AUDIO_PCM, payload_c, len_c));
  check_true("queue D", vw_worker_queue_push(queue, VW_MSG_AUDIO_PCM, payload_d, len_d));
  check_true("queue E with eviction", vw_worker_queue_push(queue, VW_MSG_AUDIO_PCM, payload_e, len_e));
  check_true("queue records exactly one dropped frame",
             vw_worker_queue_get_dropped_audio_us(queue) == (uint64_t)frame_duration_us);

  vw_worker_frame_t frame = {0};
  check_true("pop first surviving frame", vw_worker_queue_pop(queue, &frame));
  check_true("first surviving frame is audio", frame.type == VW_MSG_AUDIO_PCM);

  vw_msg_audio_t decoded = {0};
  check_true("decode first surviving audio frame",
             vw_protocol_decode_payload(VW_MSG_AUDIO_PCM, frame.payload, frame.payload_len, &decoded));
  check_true("oldest queued frame was evicted", decoded.start_pts_us == 1200000);
  check_true("decoded frame size is whole S16LE samples", decoded.pcm_bytes % sizeof(int16_t) == 0);

  size_t before_count = vw_audio_buffer_get_count(audio_buffer);
  float before_sample = 0.0f;
  int64_t before_pts = -1;
  check_true("read pre-discontinuity buffer anchor",
             vw_audio_buffer_get_samples(audio_buffer, &before_sample, 1, &before_pts) == 1);

  size_t decoded_sample_count = decoded.pcm_bytes / sizeof(int16_t);
  int16_t* decoded_pcm = (int16_t*)malloc(decoded.pcm_bytes);
  check_true("allocate decoded PCM copy", decoded_pcm != NULL);
  if (decoded_pcm) memcpy(decoded_pcm, decoded.pcm_data, decoded.pcm_bytes);

  bool accepted = false;
  if (decoded_pcm) {
    accepted = vw_audio_buffer_append_s16le(audio_buffer, decoded_pcm, decoded_sample_count, decoded.start_pts_us);
  }

  if (!accepted) {
    float after_sample = 0.0f;
    int64_t after_pts = -1;
    check_true("rejected discontinuity leaves sample count unchanged",
               vw_audio_buffer_get_count(audio_buffer) == before_count);
    check_true("rejected discontinuity leaves anchor readable",
               vw_audio_buffer_get_samples(audio_buffer, &after_sample, 1, &after_pts) == 1);
    check_true("rejected discontinuity leaves anchor unchanged", after_pts == before_pts);
  } else {
    float after_sample = 0.0f;
    int64_t after_pts = -1;
    size_t after_count = vw_audio_buffer_get_count(audio_buffer);
    check_true("accepted discontinuity leaves readable buffered audio",
               vw_audio_buffer_get_samples(audio_buffer, &after_sample, 1, &after_pts) == 1);

    int64_t represented_end_pts_us = after_pts + sample_span_us(after_count);
    int64_t incoming_end_pts_us = decoded.start_pts_us + decoded.duration_us;
    check_true("queue eviction must not collapse missing media time",
               represented_end_pts_us >= incoming_end_pts_us);
  }

  free(decoded_pcm);
  free(frame.payload);

  while (vw_worker_queue_pop(queue, &frame)) {
    free(frame.payload);
  }
  vw_worker_queue_destroy(queue);
  vw_audio_buffer_free(audio_buffer);

  if (g_failures != 0) {
    fprintf(stderr, "test_queue_audio_timeline: %d contract failure(s)\n", g_failures);
    return 1;
  }

  printf("test_queue_audio_timeline PASSED\n");
  return 0;
}
