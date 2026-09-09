#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vw_source_decoder.h"

static bool vw_fail_resampler;
static int vw_open_calls;

int __real_av_seek_frame(AVFormatContext* context, int stream, int64_t timestamp, int flags);
int __wrap_av_seek_frame(AVFormatContext* context, int stream, int64_t timestamp, int flags) {
  (void)timestamp;
  return __real_av_seek_frame(context, stream, 0, flags);
}

int __real_swr_init(struct SwrContext* context);
int __wrap_swr_init(struct SwrContext* context) {
  return vw_fail_resampler ? AVERROR(ENOMEM) : __real_swr_init(context);
}

int __real_avformat_open_input(AVFormatContext** context, const char* url, const AVInputFormat* format,
                               AVDictionary** options);
int __wrap_avformat_open_input(AVFormatContext** context, const char* url, const AVInputFormat* format,
                               AVDictionary** options) {
  vw_open_calls++;
  return __real_avformat_open_input(context, url, format, options);
}

int main(int argc, char** argv) {
  const char* mode = argc > 1 ? argv[1] : "preroll";
  if (strcmp(mode, "paths") == 0) {
    char oversized[5000];
    memset(oversized, 'x', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    assert(vw_source_decoder_open(oversized, NULL) == NULL);
    assert(vw_open_calls == 0);
    assert(vw_source_decoder_open("file:///tmp/example%00.wav", NULL) == NULL);
    assert(vw_open_calls == 0);
    return 0;
  }

  char path[] = "/tmp/vw-decoder-boundary-XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  FILE* file = fdopen(fd, "wb");
  assert(file);
  const unsigned char header[44] = {'R', 'I', 'F', 'F', 0x24, 0xfa, 0,   0,   'W', 'A',  'V',  'E',  'f', 'm', 't',
                                    ' ', 16,  0,   0,   0,    1,    0,   1,   0,   0x80, 0x3e, 0,    0,   0,   0x7d,
                                    0,   0,   2,   0,   16,   0,    'd', 'a', 't', 'a',  0,    0xfa, 0,   0};
  assert(fwrite(header, sizeof(header), 1, file) == 1);
  for (unsigned i = 0; i < 32000; i++) {
    const unsigned char sample[2] = {(unsigned char)(i >= 16000 ? 2 : 1), 0};
    assert(fwrite(sample, sizeof(sample), 1, file) == 1);
  }
  assert(fclose(file) == 0);
  vw_source_decoder_t* decoder = vw_source_decoder_open(path, NULL);
  assert(decoder);
  assert(unlink(path) == 0);
  if (strcmp(mode, "failure") == 0) {
    vw_fail_resampler = true;
    assert(!vw_source_decoder_seek(decoder, 1000000));
    vw_fail_resampler = false;
  } else {
    assert(vw_source_decoder_seek(decoder, 1000000));
  }
  int16_t pcm[128];
  int64_t pts = -1;
  size_t count = 0;
  vw_source_decoder_read_status_t result = vw_source_decoder_read_s16le(decoder, pcm, 128, &count, &pts);
  assert(result == VW_SOURCE_DECODER_READ_OK);
  assert(count == 128);
  if (strcmp(mode, "failure") == 0) {
    assert(pts == 0);  // Failed seek must leave the original decoder usable at its original position.
    assert(pcm[0] == 1);
  } else {
    assert(pts >= 1000000);
    for (size_t i = 0; i < count; i++) assert(pcm[i] == 2);
  }
  vw_source_decoder_close(decoder);
  return 0;
}
