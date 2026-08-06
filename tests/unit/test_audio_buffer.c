#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_audio_buffer.h"

#define EXPECT(cond)                                                                 \
  do {                                                                               \
    if (!(cond)) {                                                                   \
      fprintf(stderr, "Assertion failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
      exit(1);                                                                       \
    }                                                                                \
  } while (0)

int main(void) {
  // 1. Invalid / NULL parameter rejection
  EXPECT(!vw_audio_buffer_append_s16le(NULL, NULL, 100, 0));
  EXPECT(vw_audio_buffer_get_count(NULL) == 0);
  EXPECT(vw_audio_buffer_get_samples(NULL, NULL, 100, NULL) == 0);
  vw_audio_buffer_drain(NULL, 10);  // Safe no-op
  vw_audio_buffer_clear(NULL);      // Safe no-op
  vw_audio_buffer_free(NULL);       // Safe no-op

  // 2. Create and free
  vw_audio_buffer_t* buf = vw_audio_buffer_create(1000);
  EXPECT(buf != NULL);
  EXPECT(vw_audio_buffer_get_count(buf) == 0);
  EXPECT(!vw_audio_buffer_append_s16le(buf, NULL, 100, 1000000));

  // 3. Append S16LE samples
  int16_t pcm[100];
  for (int i = 0; i < 100; i++) {
    pcm[i] = (int16_t)(i * 100);
  }
  EXPECT(vw_audio_buffer_append_s16le(buf, pcm, 100, 1000000));
  EXPECT(vw_audio_buffer_get_count(buf) == 100);

  // 4. Retrieve float32 samples and verify PTS
  float out_samples[100];
  int64_t out_pts = 0;
  size_t read_cnt = vw_audio_buffer_get_samples(buf, out_samples, 100, &out_pts);
  EXPECT(read_cnt == 100);
  EXPECT(out_pts == 1000000);
  EXPECT(out_samples[0] == 0.0f);
  EXPECT(out_samples[10] > 0.0f);

  // 5. Overflow handling: push past max_samples (small 10-sample buffer)
  vw_audio_buffer_t* small_buf = vw_audio_buffer_create(10);
  int16_t small_pcm[15];
  for (int i = 0; i < 15; i++) small_pcm[i] = (int16_t)(i * 1000);
  EXPECT(vw_audio_buffer_append_s16le(small_buf, small_pcm, 15, 1000));
  EXPECT(vw_audio_buffer_get_count(small_buf) == 10);
  EXPECT(small_buf->dropped_samples == 5);
  // 5 dropped samples advance PTS by exactly 5 × 62.5 µs = 312.5 µs (0.5 µs carried, not lost or inflated)
  int64_t ovf_pts = 0;
  float ovf_s[1];
  EXPECT(vw_audio_buffer_get_samples(small_buf, ovf_s, 1, &ovf_pts) == 1);
  EXPECT(ovf_pts == 1312);
  vw_audio_buffer_free(small_buf);

  // 6. Drain and clear
  vw_audio_buffer_drain(buf, 50);
  EXPECT(vw_audio_buffer_get_count(buf) == 50);
  // 50 drained samples advance PTS by exactly 50 × 62.5 µs = 3125 µs (no per-hop drift)
  int64_t drain_pts = 0;
  float drain_s[1];
  EXPECT(vw_audio_buffer_get_samples(buf, drain_s, 1, &drain_pts) == 1);
  EXPECT(drain_pts == 1003125);

  vw_audio_buffer_clear(buf);
  EXPECT(vw_audio_buffer_get_count(buf) == 0);

  vw_audio_buffer_free(buf);
  printf("test_audio_buffer passed\n");
  return 0;
}
