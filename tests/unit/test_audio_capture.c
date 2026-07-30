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
  // Note: our boxcar filter averages 3 input frames (6 samples) for the first output frame.
  // Since we only set the first frame, sum = 1.0, count = 6, avg = 1.0/6.0.
  // int16 val = (1.0/6.0) * 32767 = 5461.
  assert(chunk1_pcm[0] >= 5460 && chunk1_pcm[0] <= 5462);

  vw_audio_chunk_t chunk2;
  assert(vw_spsc_queue_pop(q, &chunk2) != NULL);
  assert(chunk2.start_pts_us == chunk1.start_pts_us + chunk1.duration_us);
  assert(chunk2.bytes == 1808 * sizeof(int16_t));
  assert(chunk2.duration_us == (1808 * 1000000LL) / 16000);

  assert(vw_spsc_queue_pop(q, &chunk2) == NULL);

  vw_spsc_queue_destroy(q);
  free(pcm);

  printf("test_audio_capture PASSED\n");
  return 0;
}
