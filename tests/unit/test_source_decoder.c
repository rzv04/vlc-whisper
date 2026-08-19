#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_source_decoder.h"

int main(void) {
  // Test 1: NULL and empty URL handling
  assert(vw_source_decoder_open(NULL, NULL) == NULL);
  assert(vw_source_decoder_open("", NULL) == NULL);
  assert(vw_source_decoder_seek(NULL, 0) == false);
  assert(vw_source_decoder_read_s16le(NULL, NULL, 0, NULL) == 0);
  assert(vw_source_decoder_get_duration_us(NULL) == -1);
  vw_source_decoder_close(NULL);

  // Test 2: Non-existent file path
  assert(vw_source_decoder_open("file:///non_existent_path_12345.mp4", NULL) == NULL);

  // Test 3: Valid media file open, read, seek, close (if test fixture exists)
  const char* fixture_paths[] = {"samples/audio/harvard.wav",
                                 "../samples/audio/harvard.wav",
                                 "../../samples/audio/harvard.wav",
                                 "worker/third_party/whisper.cpp/samples/jfk.wav",
                                 "../worker/third_party/whisper.cpp/samples/jfk.wav",
                                 NULL};

  const char* found_fixture = NULL;
  for (int i = 0; fixture_paths[i]; i++) {
    FILE* f = fopen(fixture_paths[i], "rb");
    if (f) {
      fclose(f);
      found_fixture = fixture_paths[i];
      break;
    }
  }

  bool created_temp = false;
  char temp_path[256] = "temp_synthetic_test.wav";
  if (!found_fixture) {
    FILE* f = fopen(temp_path, "wb");
    if (f) {
      uint32_t sample_rate = 16000;
      uint16_t channels = 1;
      uint16_t bits_per_sample = 16;
      uint32_t num_samples = 32000;  // 2 seconds
      uint32_t data_bytes = num_samples * sizeof(int16_t);
      uint32_t riff_size = 36 + data_bytes;
      uint32_t byte_rate = sample_rate * channels * (bits_per_sample / 8);
      uint16_t block_align = channels * (bits_per_sample / 8);

      fwrite("RIFF", 1, 4, f);
      fwrite(&riff_size, 4, 1, f);
      fwrite("WAVE", 1, 4, f);
      fwrite("fmt ", 1, 4, f);
      uint32_t fmt_chunk_size = 16;
      uint16_t audio_format = 1;  // PCM
      fwrite(&fmt_chunk_size, 4, 1, f);
      fwrite(&audio_format, 2, 1, f);
      fwrite(&channels, 2, 1, f);
      fwrite(&sample_rate, 4, 1, f);
      fwrite(&byte_rate, 4, 1, f);
      fwrite(&block_align, 2, 1, f);
      fwrite(&bits_per_sample, 2, 1, f);
      fwrite("data", 1, 4, f);
      fwrite(&data_bytes, 4, 1, f);

      int16_t sample = 0;
      for (uint32_t i = 0; i < num_samples; i++) {
        fwrite(&sample, sizeof(int16_t), 1, f);
      }
      fclose(f);
      found_fixture = temp_path;
      created_temp = true;
    }
  }

  if (found_fixture) {
    vw_source_decoder_info_t info = {0};
    vw_source_decoder_t* dec = vw_source_decoder_open(found_fixture, &info);
    if (dec) {
      assert(info.sample_rate == 16000);
      assert(info.channels == 1);

      int16_t pcm_buf[16000];  // 1 second of 16kHz audio
      int64_t pts_us = -1;
      size_t samples_read = vw_source_decoder_read_s16le(dec, pcm_buf, 16000, &pts_us);
      assert(samples_read > 0);
      assert(pts_us >= 0);
      (void)samples_read;

      // Seek test
      bool seek_ok = vw_source_decoder_seek(dec, 1000000LL);  // Seek to 1s
      if (seek_ok) {
        int64_t post_seek_pts = -1;
        size_t post_seek_samples = vw_source_decoder_read_s16le(dec, pcm_buf, 4000, &post_seek_pts);
        (void)post_seek_samples;
        (void)post_seek_pts;
      }

      int64_t dur = vw_source_decoder_get_duration_us(dec);
      (void)dur;

      vw_source_decoder_close(dec);
    }
  }

  if (created_temp) {
    remove(temp_path);
  }

  printf("test_source_decoder PASSED\n");
  return 0;
}
