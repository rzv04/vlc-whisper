#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "vw_audio_capture.h"
#include "vw_queue.h"

int main(void) {
  vw_spsc_queue_t* q = vw_spsc_queue_create(10);
  assert(q != NULL);

  vw_audio_capture_t cap = {
      .target_sample_rate = 16000, .target_channels = 1, .last_pts_us = 0, .total_samples_processed = 0, .queue = q};

  // Create a block of 30,000 frames of 48kHz Stereo Float32
  // Downsampling to 16kHz should result in 10,000 frames exactly.
  // 10,000 frames should be chunked into 8192 and 1808.
  size_t input_frames = 30000;
  float* pcm = calloc(input_frames * 2, sizeof(float));

  // Set a few samples to test downmixing
  pcm[0] = 0.5f;  // Left
  pcm[1] = 0.5f;  // Right (avg should be 0.5)

  vw_audio_input_t input = {.pcm_data = pcm,
                            .frame_count = input_frames,
                            .pts_us = 1000000,
                            .format = VW_AUDIO_FORMAT_FL32,
                            .sample_rate = 48000,
                            .channels = 2};

  bool success = vw_audio_capture_process_block(&cap, &input);
  assert(success == true);
  assert(cap.total_samples_processed == 10000);

  vw_audio_chunk_t chunk1;
  assert(vw_spsc_queue_pop(q, &chunk1) != NULL);
  assert(chunk1.start_pts_us == 1000000);
  assert(chunk1.bytes == 16384);  // 8192 int16 samples
  assert(chunk1.duration_us == (8192 * 1000000LL) / 16000);

  // Check the downmixed value (0.5 float = ~16383 int16)
  int16_t* chunk1_pcm = (int16_t*)chunk1.pcm_data;
  assert(chunk1_pcm[0] >= 16382 && chunk1_pcm[0] <= 16384);

  vw_audio_chunk_t chunk2;
  assert(vw_spsc_queue_pop(q, &chunk2) != NULL);
  assert(chunk2.start_pts_us == chunk1.start_pts_us + chunk1.duration_us);
  assert(chunk2.bytes == 1808 * sizeof(int16_t));
  assert(chunk2.duration_us == (1808 * 1000000LL) / 16000);

  assert(vw_spsc_queue_pop(q, &chunk2) == NULL);

  // The rational phase must continue across short 44.1kHz blocks instead of restarting at
  // input sample zero or repeating the old block's boundary sample.
  int16_t block_a[101];
  int16_t block_b[101];
  for (size_t i = 0; i < 101; ++i) {
    block_a[i] = (int16_t)i;
    block_b[i] = (int16_t)(1000 + i);
  }
  vw_audio_input_t input_a = {.pcm_data = block_a,
                              .frame_count = 101,
                              .pts_us = 2000000,
                              .format = VW_AUDIO_FORMAT_S16,
                              .sample_rate = 44100,
                              .channels = 1};
  vw_audio_input_t input_b = input_a;
  input_b.pcm_data = block_b;
  input_b.pts_us += 1000000;
  assert(vw_audio_capture_process_block(&cap, &input_a) == true);
  vw_audio_chunk_t chunk_a;
  assert(vw_spsc_queue_pop(q, &chunk_a) != NULL);
  assert(chunk_a.bytes == 37 * sizeof(int16_t));
  int16_t* chunk_a_pcm = (int16_t*)chunk_a.pcm_data;
  assert(chunk_a_pcm[0] == 0);
  assert(chunk_a_pcm[1] == 3);

  assert(vw_audio_capture_process_block(&cap, &input_b) == true);
  vw_audio_chunk_t chunk_b;
  assert(vw_spsc_queue_pop(q, &chunk_b) != NULL);
  assert(chunk_b.bytes == 36 * sizeof(int16_t));
  int16_t* chunk_b_pcm = (int16_t*)chunk_b.pcm_data;
  assert(chunk_b_pcm[0] == 1001);
  assert(chunk_b_pcm[1] == 1004);
  assert(vw_spsc_queue_pop(q, &chunk_b) == NULL);

  (void)chunk_a;
  (void)chunk_b;
  (void)chunk_a_pcm;
  (void)chunk_b_pcm;

  (void)success;
  (void)chunk1_pcm;
  (void)chunk2;

  vw_spsc_queue_destroy(q);
  free(pcm);

  printf("test_audio_capture PASSED\n");
  return 0;
}
