#include "vw_quality_hook.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "vw_protocol_types.h"
#include "vw_source_decoder.h"
#include "vw_worker_queue.h"

#if defined(VW_QUALITY_LINK_WRAPS)

size_t __real_vw_source_decoder_read_s16le(vw_source_decoder_t* decoder, int16_t* out_pcm, size_t max_samples,
                                           int64_t* out_pts_us);
bool __real_vw_worker_queue_push(vw_worker_queue_t* q, uint16_t type, uint8_t* payload, uint32_t payload_len);

static bool vw_quality_marker_path(char* out, size_t out_size, const char* suffix) {
  if (!out || out_size == 0 || !suffix) return false;
  const char* prefix = getenv(VW_QUALITY_MARKER_ENV);
  if (!prefix || prefix[0] == '\0') return false;
  int written = snprintf(out, out_size, "%s%s", prefix, suffix);
  return written > 0 && (size_t)written < out_size;
}

static void vw_quality_write_marker(const char* suffix, const char* value) {
  if (!suffix || !value) return;
  char path[VW_PATH_MAX_BYTES];
  if (!vw_quality_marker_path(path, sizeof(path), suffix)) return;
  FILE* file = fopen(path, "wb");
  if (!file) return;
  fputs(value, file);
  fclose(file);
}

static void vw_quality_write_drop_marker(uint64_t dropped_audio_us) {
  char value[32];
  snprintf(value, sizeof(value), "%" PRIu64, dropped_audio_us);
  vw_quality_write_marker(VW_QUALITY_DROPS_MARKER_SUFFIX, value);
}

size_t __wrap_vw_source_decoder_read_s16le(vw_source_decoder_t* decoder, int16_t* out_pcm, size_t max_samples,
                                           int64_t* out_pts_us) {
  static _Thread_local unsigned int zero_reads = 0;
  size_t samples = __real_vw_source_decoder_read_s16le(decoder, out_pcm, max_samples, out_pts_us);
  if (samples > 0) {
    zero_reads = 0;
  } else if (decoder) {
    zero_reads++;
    if (zero_reads >= VW_QUALITY_SOURCE_EOF_ZERO_READS) {
      vw_quality_write_marker(VW_QUALITY_EOF_MARKER_SUFFIX, "1");
    }
  }
  return samples;
}

bool __wrap_vw_worker_queue_push(vw_worker_queue_t* q, uint16_t type, uint8_t* payload, uint32_t payload_len) {
  uint64_t before = q ? vw_worker_queue_get_dropped_audio_us(q) : 0;
  bool accepted = __real_vw_worker_queue_push(q, type, payload, payload_len);
  uint64_t after = q ? vw_worker_queue_get_dropped_audio_us(q) : before;
  if (after != before) vw_quality_write_drop_marker(after);
  return accepted;
}

#else
typedef int vw_quality_hooks_disabled_t;
#endif
