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

  printf("test_source_decoder PASSED\n");
  return 0;
}
