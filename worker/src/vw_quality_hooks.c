#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_log.h"
#include "vw_protocol_types.h"
#include "vw_quality_hook.h"
#include "vw_source_decoder.h"
#include "vw_worker_queue.h"

#if defined(VW_QUALITY_LINK_WRAPS)

vw_source_decoder_read_status_t __real_vw_source_decoder_read_s16le(vw_source_decoder_t* decoder, int16_t* out_pcm,
                                                                    size_t max_samples, size_t* out_sample_count,
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
  if (!vw_quality_marker_path(path, sizeof(path), suffix)) {
    vw_log_event(VW_LOG_LEVEL_WARN, "QUALITY_HOOKS", "marker path exceeds maximum buffer size for suffix '%s'", suffix);
    return;
  }
  FILE* file = fopen(path, "wb");
  if (!file) {
    vw_log_event(VW_LOG_LEVEL_WARN, "QUALITY_HOOKS", "cannot open quality marker file '%s': %s", path, strerror(errno));
    return;
  }
  int printed = fprintf(file, "%s", value);
  if (printed < 0) {
    vw_log_event(VW_LOG_LEVEL_WARN, "QUALITY_HOOKS", "fprintf failed writing to marker file '%s': %s", path,
                 strerror(errno));
  }
  if (fclose(file) != 0) {
    vw_log_event(VW_LOG_LEVEL_WARN, "QUALITY_HOOKS", "fclose failed for marker file '%s': %s", path, strerror(errno));
  }
}

static void vw_quality_write_drop_marker(uint64_t dropped_audio_us) {
  char value[32];
  snprintf(value, sizeof(value), "%" PRIu64, dropped_audio_us);
  vw_quality_write_marker(VW_QUALITY_DROPS_MARKER_SUFFIX, value);
}

void vw_quality_hook_on_queue_drop(uint64_t dropped_audio_us) {
  static uint64_t s_last_dropped_audio_us = 0;
  static bool s_has_written_drop = false;
  if (!s_has_written_drop || dropped_audio_us != s_last_dropped_audio_us) {
    s_last_dropped_audio_us = dropped_audio_us;
    s_has_written_drop = true;
    vw_quality_write_drop_marker(dropped_audio_us);
  }
}

vw_source_decoder_read_status_t __wrap_vw_source_decoder_read_s16le(vw_source_decoder_t* decoder, int16_t* out_pcm,
                                                                    size_t max_samples, size_t* out_sample_count,
                                                                    int64_t* out_pts_us) {
  vw_source_decoder_read_status_t status =
      __real_vw_source_decoder_read_s16le(decoder, out_pcm, max_samples, out_sample_count, out_pts_us);
  if (status == VW_SOURCE_DECODER_READ_EOF) vw_quality_write_marker(VW_QUALITY_EOF_MARKER_SUFFIX, "1");
  return status;
}

bool __wrap_vw_worker_queue_push(vw_worker_queue_t* q, uint16_t type, uint8_t* payload, uint32_t payload_len) {
  bool accepted = __real_vw_worker_queue_push(q, type, payload, payload_len);
  uint64_t dropped_audio_us = q ? vw_worker_queue_get_dropped_audio_us(q) : 0;
  vw_quality_hook_on_queue_drop(dropped_audio_us);
  return accepted;
}

#else
void vw_quality_hook_on_queue_drop(uint64_t dropped_audio_us) { (void)dropped_audio_us; }
#endif
