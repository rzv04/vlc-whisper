#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "ggml-backend.h"
#include "vw_platform.h"
#include "vw_protocol_types.h"
#include "vw_quality_hook.h"
#include "vw_worker_client.h"
#include "whisper.h"

#define VW_QUALITY_SAMPLE_RATE 16000U
#define VW_QUALITY_CHANNELS 1U
#define VW_QUALITY_SAMPLE_WIDTH 2U
#define VW_QUALITY_LIVE_CHUNK_SAMPLES 320U
// 500 ms holdback plus up to one full 1 s hop guarantees a post-speech inference frontier.
#define VW_QUALITY_LIVE_TAIL_SAMPLES (VW_QUALITY_SAMPLE_RATE * 3U / 2U)
#define VW_QUALITY_POSITION_INTERVAL_US 100000LL
#define VW_QUALITY_MAX_AUDIO_SECONDS 60U
#define VW_QUALITY_MAX_SEGMENTS 512U
#define VW_QUALITY_POLL_TIMEOUT_US 1000U
#define VW_QUALITY_COMPLETION_POLL_US 100000U
#define VW_QUALITY_COMPLETION_TIMEOUT_US 120000000LL

typedef enum vw_quality_mode {
  VW_QUALITY_MODE_OFFLINE = 0,
  VW_QUALITY_MODE_LIVE = 1,
  VW_QUALITY_MODE_LOOKAHEAD = 2
} vw_quality_mode_t;

typedef struct vw_quality_audio {
  int16_t* samples;
  size_t sample_count;
} vw_quality_audio_t;

typedef struct vw_quality_segment {
  int64_t start_pts_us;
  int64_t end_pts_us;
  char text[VW_MAX_TEXT_BYTES + 1U];
} vw_quality_segment_t;

typedef struct vw_quality_result {
  vw_quality_segment_t* segments;
  size_t segment_count;
  int64_t queued_audio_us;
  int64_t inference_us;
  int64_t dropped_audio_us;
  char resolved_backend[17];
} vw_quality_result_t;

typedef struct vw_quality_options {
  const char* worker_path;
  const char* model_path;
  const char* model_dir;
  const char* audio_path;
  const char* language;
  const char* backend;
  int threads;
  vw_quality_mode_t mode;
} vw_quality_options_t;

static uint16_t vw_quality_u16le(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8U); }

static uint32_t vw_quality_u32le(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static bool vw_quality_read_exact(FILE* file, void* buffer, size_t bytes) {
  return file && buffer && fread(buffer, 1, bytes, file) == bytes;
}

static bool vw_quality_skip_chunk(FILE* file, uint32_t size) {
  uint64_t padded = (uint64_t)size + (uint64_t)(size & 1U);
  if (padded > (uint64_t)LONG_MAX) return false;
  return fseek(file, (long)padded, SEEK_CUR) == 0;
}

static bool vw_quality_load_wav(const char* path, vw_quality_audio_t* out_audio) {
  if (!path || !out_audio) return false;
  memset(out_audio, 0, sizeof(*out_audio));

  FILE* file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "quality runner: cannot open WAV '%s': %s\n", path, strerror(errno));
    return false;
  }

  uint8_t riff[12];
  if (!vw_quality_read_exact(file, riff, sizeof(riff)) || memcmp(riff, "RIFF", 4) != 0 ||
      memcmp(riff + 8, "WAVE", 4) != 0) {
    fprintf(stderr, "quality runner: '%s' is not a RIFF/WAVE file\n", path);
    fclose(file);
    return false;
  }

  bool fmt_found = false;
  bool data_found = false;
  uint16_t audio_format = 0;
  uint16_t channels = 0;
  uint16_t bits_per_sample = 0;
  uint32_t sample_rate = 0;
  uint32_t data_bytes = 0;
  long data_offset = 0;

  for (;;) {
    uint8_t header[8];
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) break;
    uint32_t chunk_size = vw_quality_u32le(header + 4);
    if (memcmp(header, "fmt ", 4) == 0) {
      if (chunk_size < 16U) break;
      uint8_t fmt[16];
      if (!vw_quality_read_exact(file, fmt, sizeof(fmt))) break;
      audio_format = vw_quality_u16le(fmt);
      channels = vw_quality_u16le(fmt + 2);
      sample_rate = vw_quality_u32le(fmt + 4);
      bits_per_sample = vw_quality_u16le(fmt + 14);
      uint32_t remainder = chunk_size - 16U;
      if (remainder > 0U && !vw_quality_skip_chunk(file, remainder)) break;
      fmt_found = true;
    } else if (memcmp(header, "data", 4) == 0) {
      data_offset = ftell(file);
      data_bytes = chunk_size;
      data_found = data_offset >= 0;
      break;
    } else if (!vw_quality_skip_chunk(file, chunk_size)) {
      break;
    }
  }

  if (!fmt_found || !data_found || audio_format != 1U || channels != VW_QUALITY_CHANNELS ||
      sample_rate != VW_QUALITY_SAMPLE_RATE || bits_per_sample != 16U || data_bytes == 0U ||
      (data_bytes % VW_QUALITY_SAMPLE_WIDTH) != 0U) {
    fprintf(stderr, "quality runner: WAV must be 16 kHz mono PCM S16LE\n");
    fclose(file);
    return false;
  }

  const uint64_t max_bytes = (uint64_t)VW_QUALITY_MAX_AUDIO_SECONDS * VW_QUALITY_SAMPLE_RATE * VW_QUALITY_SAMPLE_WIDTH;
  if ((uint64_t)data_bytes > max_bytes) {
    fprintf(stderr, "quality runner: WAV exceeds %u second safety bound\n", VW_QUALITY_MAX_AUDIO_SECONDS);
    fclose(file);
    return false;
  }

  int16_t* samples = (int16_t*)malloc(data_bytes);
  if (!samples) {
    fclose(file);
    return false;
  }
  if (fseek(file, data_offset, SEEK_SET) != 0 || !vw_quality_read_exact(file, samples, data_bytes)) {
    fprintf(stderr, "quality runner: failed reading WAV PCM payload\n");
    free(samples);
    fclose(file);
    return false;
  }
  fclose(file);

  out_audio->samples = samples;
  out_audio->sample_count = data_bytes / sizeof(int16_t);
  return true;
}

static void vw_quality_free_audio(vw_quality_audio_t* audio) {
  if (!audio) return;
  free(audio->samples);
  audio->samples = NULL;
  audio->sample_count = 0;
}

static bool vw_quality_random_hex(char* out, size_t out_size, size_t random_bytes_count) {
  if (!out || random_bytes_count == 0 || random_bytes_count > 32 || out_size < random_bytes_count * 2U + 1U)
    return false;
  uint8_t random_bytes[32];
  if (!vw_platform_get_random_bytes(random_bytes, random_bytes_count)) return false;
  for (size_t i = 0; i < random_bytes_count; i++) snprintf(out + i * 2U, 3U, "%02x", random_bytes[i]);
  out[random_bytes_count * 2U] = '\0';
  memset(random_bytes, 0, sizeof(random_bytes));
  return true;
}

static bool vw_quality_make_endpoint(char* out, size_t out_size) {
  char hex[33];
  if (!vw_quality_random_hex(hex, sizeof(hex), 16U)) return false;
#ifdef _WIN32
  int written = snprintf(out, out_size, "\\\\.\\pipe\\vlc-whisper-quality-%s", hex);
#else
  int written = snprintf(out, out_size, "/tmp/vlc-whisper-quality-%s.sock", hex);
#endif
  memset(hex, 0, sizeof(hex));
  return written > 0 && (size_t)written < out_size;
}

static bool vw_quality_marker_path(char* out, size_t out_size, const char* prefix, const char* suffix) {
  if (!out || out_size == 0 || !prefix || !suffix) return false;
  int written = snprintf(out, out_size, "%s%s", prefix, suffix);
  return written > 0 && (size_t)written < out_size;
}

static bool vw_quality_make_marker_prefix(char* out, size_t out_size) {
  char hex[33];
  if (!out || !vw_quality_random_hex(hex, sizeof(hex), 16U)) return false;
#ifdef _WIN32
  char temp_dir[MAX_PATH];
  DWORD temp_len = GetTempPathA((DWORD)sizeof(temp_dir), temp_dir);
  if (temp_len == 0 || temp_len >= sizeof(temp_dir)) return false;
  int written = snprintf(out, out_size, "%svlc-whisper-quality-%s", temp_dir, hex);
#else
  int written = snprintf(out, out_size, "/tmp/vlc-whisper-quality-%s", hex);
#endif
  memset(hex, 0, sizeof(hex));
  return written > 0 && (size_t)written < out_size;
}

static bool vw_quality_set_marker_env(const char* prefix) {
  if (!prefix || prefix[0] == '\0') return false;
#ifdef _WIN32
  return SetEnvironmentVariableA(VW_QUALITY_MARKER_ENV, prefix) != 0;
#else
  return setenv(VW_QUALITY_MARKER_ENV, prefix, 1) == 0;
#endif
}

static void vw_quality_clear_marker_env(void) {
#ifdef _WIN32
  SetEnvironmentVariableA(VW_QUALITY_MARKER_ENV, NULL);
#else
  unsetenv(VW_QUALITY_MARKER_ENV);
#endif
}

static bool vw_quality_prepare_markers(const char* prefix) {
  char path[VW_PATH_MAX_BYTES];
  if (!vw_quality_marker_path(path, sizeof(path), prefix, VW_QUALITY_EOF_MARKER_SUFFIX)) return false;
  remove(path);
  if (!vw_quality_marker_path(path, sizeof(path), prefix, VW_QUALITY_DROPS_MARKER_SUFFIX)) return false;
  FILE* drops = fopen(path, "wb");
  if (!drops) return false;
  bool write_ok = fputs("uninitialized", drops) >= 0;
  bool close_ok = fclose(drops) == 0;
  bool ok = write_ok && close_ok;
  if (!ok) remove(path);
  return ok && vw_quality_set_marker_env(prefix);
}

static void vw_quality_cleanup_markers(const char* prefix) {
  char path[VW_PATH_MAX_BYTES];
  vw_quality_clear_marker_env();
  if (!prefix) return;
  if (vw_quality_marker_path(path, sizeof(path), prefix, VW_QUALITY_EOF_MARKER_SUFFIX)) remove(path);
  if (vw_quality_marker_path(path, sizeof(path), prefix, VW_QUALITY_DROPS_MARKER_SUFFIX)) remove(path);
}

static bool vw_quality_source_eof_seen(const char* prefix) {
  char path[VW_PATH_MAX_BYTES];
  if (!vw_quality_marker_path(path, sizeof(path), prefix, VW_QUALITY_EOF_MARKER_SUFFIX)) return false;
  FILE* file = fopen(path, "rb");
  if (!file) return false;
  int marker = fgetc(file);
  fclose(file);
  return marker == '1';
}

static bool vw_quality_read_drop_marker(const char* prefix, int64_t* out_dropped_audio_us) {
  if (!out_dropped_audio_us) return false;
  char path[VW_PATH_MAX_BYTES];
  if (!vw_quality_marker_path(path, sizeof(path), prefix, VW_QUALITY_DROPS_MARKER_SUFFIX)) return false;
  FILE* file = fopen(path, "rb");
  if (!file) return false;
  uint64_t value = 0;
  int matched = fscanf(file, "%" SCNu64, &value);
  fclose(file);
  if (matched != 1 || value > (uint64_t)INT64_MAX) return false;
  *out_dropped_audio_us = (int64_t)value;
  return true;
}

static bool vw_quality_append_segment(vw_quality_result_t* result, const vw_caption_segment_t* segment) {
  if (!result || !segment || !segment->text_utf8 || segment->text_bytes > VW_MAX_TEXT_BYTES) return false;
  if (result->segment_count >= VW_QUALITY_MAX_SEGMENTS) {
    fprintf(stderr, "quality runner: segment count exceeded %u\n", VW_QUALITY_MAX_SEGMENTS);
    return false;
  }
  vw_quality_segment_t* dst = &result->segments[result->segment_count++];
  dst->start_pts_us = segment->start_pts_us;
  dst->end_pts_us = segment->end_pts_us;
  memcpy(dst->text, segment->text_utf8, segment->text_bytes);
  dst->text[segment->text_bytes] = '\0';
  return true;
}

static bool vw_quality_consume_received(vw_worker_client_t* client, vw_quality_result_t* result,
                                        const vw_worker_recv_t* recv) {
  if (!client || !result || !recv) return false;
  switch (recv->type) {
    case VW_MSG_CAPTION_SEGMENT:
      if (memcmp(recv->segment.session_id.bytes, client->session_id, VW_SESSION_ID_BYTES) == 0 &&
          recv->segment.is_final) {
        return vw_quality_append_segment(result, &recv->segment);
      }
      return true;
    case VW_MSG_STATUS:
      if (memcmp(recv->status.session_id.bytes, client->session_id, VW_SESSION_ID_BYTES) == 0) {
        result->queued_audio_us = recv->status.queued_audio_us;
        result->inference_us = recv->status.inference_us;
        memcpy(result->resolved_backend, recv->status.resolved_backend, 16U);
        result->resolved_backend[16] = '\0';
      }
      return true;
    case VW_MSG_ERROR:
      fprintf(stderr, "quality runner: worker error code=%u recoverable=%u message=%.*s\n", recv->error.error_code,
              recv->error.recoverable, (int)strnlen(recv->error.message, VW_MAX_ERROR_MSG_BYTES), recv->error.message);
      return false;
    default:
      return true;
  }
}

static int vw_quality_receive_once(vw_worker_client_t* client, vw_quality_result_t* result, uint32_t timeout_us,
                                   bool* out_received) {
  if (out_received) *out_received = false;
  vw_worker_recv_t recv;
  int rc = vw_worker_client_receive_frame(client, timeout_us, &recv);
  if (rc != VW_IPC_RECV_OK) return rc;
  if (out_received) *out_received = true;
  return vw_quality_consume_received(client, result, &recv) ? VW_IPC_RECV_OK : VW_IPC_RECV_FATAL;
}

static bool vw_quality_drain_available(vw_worker_client_t* client, vw_quality_result_t* result) {
  for (;;) {
    bool received = false;
    int rc = vw_quality_receive_once(client, result, VW_QUALITY_POLL_TIMEOUT_US, &received);
    if (rc == VW_IPC_RECV_TIMEOUT) return true;
    if (rc != VW_IPC_RECV_OK) {
      fprintf(stderr, "quality runner: worker transport failed while receiving\n");
      return false;
    }
    if (!received) return true;
  }
}

static bool vw_quality_sleep_until(int64_t deadline_us) {
  for (;;) {
    int64_t now_us = vw_platform_get_monotonic_time_us();
    if (now_us < 0) return false;
    if (now_us >= deadline_us) return true;
    int64_t remaining_us = deadline_us - now_us;
    uint32_t sleep_ms = remaining_us > 2000 ? (uint32_t)(remaining_us / 1000) - 1U : 1U;
    vw_platform_sleep_ms(sleep_ms);
  }
}

static bool vw_quality_send_live_chunk(vw_worker_client_t* client, const int16_t* samples, size_t sample_count,
                                       int64_t start_pts_us) {
  if (!client || !samples || sample_count == 0 || sample_count > VW_QUALITY_LIVE_CHUNK_SAMPLES) return false;
  vw_audio_chunk_t chunk;
  memset(&chunk, 0, sizeof(chunk));
  chunk.start_pts_us = start_pts_us;
  chunk.duration_us = (int64_t)((sample_count * 1000000ULL) / VW_QUALITY_SAMPLE_RATE);
  chunk.sample_rate = VW_QUALITY_SAMPLE_RATE;
  chunk.channels = VW_QUALITY_CHANNELS;
  chunk.bytes = (uint32_t)(sample_count * sizeof(int16_t));
  memcpy(chunk.pcm_data, samples, chunk.bytes);
  return vw_worker_client_send_audio(client, &chunk);
}

static bool vw_quality_make_completion_deadline(int64_t* out_deadline_us) {
  if (!out_deadline_us) return false;
  int64_t now_us = vw_platform_get_monotonic_time_us();
  if (now_us < 0 || VW_QUALITY_COMPLETION_TIMEOUT_US > INT64_MAX - now_us) return false;
  *out_deadline_us = now_us + VW_QUALITY_COMPLETION_TIMEOUT_US;
  return true;
}

static bool vw_quality_wait_for_source_eof(vw_worker_client_t* client, vw_quality_result_t* result,
                                           const char* marker_prefix) {
  int64_t deadline_us = 0;
  if (!vw_quality_make_completion_deadline(&deadline_us)) return false;
  while (vw_platform_get_monotonic_time_us() < deadline_us) {
    if (vw_quality_source_eof_seen(marker_prefix)) return true;
    bool received = false;
    int rc = vw_quality_receive_once(client, result, VW_QUALITY_COMPLETION_POLL_US, &received);
    if (rc == VW_IPC_RECV_FATAL) {
      fprintf(stderr, "quality runner: worker exited before look-ahead source EOF\n");
      return false;
    }
    if (rc != VW_IPC_RECV_OK && rc != VW_IPC_RECV_TIMEOUT) return false;
  }
  fprintf(stderr, "quality runner: timed out waiting for look-ahead source EOF\n");
  return false;
}

static bool vw_quality_shutdown_and_drain(vw_worker_client_t* client, vw_quality_result_t* result) {
  if (!client) return false;
  vw_worker_client_shutdown(client);
  int64_t deadline_us = 0;
  if (!vw_quality_make_completion_deadline(&deadline_us)) return false;
  while (vw_platform_get_monotonic_time_us() < deadline_us) {
    bool received = false;
    int rc = vw_quality_receive_once(client, result, VW_QUALITY_COMPLETION_POLL_US, &received);
    if (rc == VW_IPC_RECV_OK || rc == VW_IPC_RECV_TIMEOUT) continue;
    if (rc == VW_IPC_RECV_FATAL) {
      if (client->worker_process && !vw_platform_wait_process(client->worker_process, 5000U)) {
        fprintf(stderr, "quality runner: worker pipe closed but process did not terminate\n");
        return false;
      }
      return true;
    }
    return false;
  }
  fprintf(stderr, "quality runner: timed out waiting for worker shutdown barrier\n");
  return false;
}

static bool vw_quality_run_live(vw_worker_client_t* client, const vw_quality_audio_t* audio,
                                vw_quality_result_t* result, int64_t* out_runtime_us) {
  if (!vw_worker_client_start_session(client, 0, "quality-benchmark", NULL)) {
    fprintf(stderr, "quality runner: failed to start live worker session\n");
    return false;
  }
  if (vw_worker_client_is_source_active(client)) {
    fprintf(stderr, "quality runner: live session unexpectedly entered source mode\n");
    return false;
  }

  int64_t wall_start_us = vw_platform_get_monotonic_time_us();
  if (wall_start_us < 0) return false;
  size_t offset = 0;
  int64_t pts_us = 0;

  while (offset < audio->sample_count) {
    size_t count = audio->sample_count - offset;
    if (count > VW_QUALITY_LIVE_CHUNK_SAMPLES) count = VW_QUALITY_LIVE_CHUNK_SAMPLES;
    if (!vw_quality_send_live_chunk(client, audio->samples + offset, count, pts_us)) {
      fprintf(stderr, "quality runner: failed sending live PCM\n");
      return false;
    }
    offset += count;
    pts_us += (int64_t)((count * 1000000ULL) / VW_QUALITY_SAMPLE_RATE);
    if (!vw_quality_drain_available(client, result)) return false;
    if (!vw_quality_sleep_until(wall_start_us + pts_us)) return false;
  }

  int16_t silence[VW_QUALITY_LIVE_CHUNK_SAMPLES] = {0};
  size_t tail_sent = 0;
  while (tail_sent < VW_QUALITY_LIVE_TAIL_SAMPLES) {
    size_t count = VW_QUALITY_LIVE_TAIL_SAMPLES - tail_sent;
    if (count > VW_QUALITY_LIVE_CHUNK_SAMPLES) count = VW_QUALITY_LIVE_CHUNK_SAMPLES;
    if (!vw_quality_send_live_chunk(client, silence, count, pts_us)) {
      fprintf(stderr, "quality runner: failed sending live EOF settle PCM\n");
      return false;
    }
    tail_sent += count;
    pts_us += (int64_t)((count * 1000000ULL) / VW_QUALITY_SAMPLE_RATE);
    if (!vw_quality_drain_available(client, result)) return false;
    if (!vw_quality_sleep_until(wall_start_us + pts_us)) return false;
  }

  if (!vw_quality_shutdown_and_drain(client, result)) return false;
  int64_t wall_end_us = vw_platform_get_monotonic_time_us();
  if (wall_end_us < 0) return false;
  *out_runtime_us = wall_end_us - wall_start_us;
  return true;
}

static bool vw_quality_run_lookahead(vw_worker_client_t* client, const vw_quality_options_t* options,
                                     const vw_quality_audio_t* audio, vw_quality_result_t* result,
                                     const char* marker_prefix, int64_t* out_runtime_us) {
  if (!vw_worker_client_start_session(client, 0, "quality-benchmark", options->audio_path)) {
    fprintf(stderr, "quality runner: failed to start look-ahead worker session\n");
    return false;
  }
  if (!vw_worker_client_is_source_active(client)) {
    fprintf(stderr, "quality runner: look-ahead source decoder did not activate\n");
    return false;
  }

  int64_t duration_us = (int64_t)((audio->sample_count * 1000000ULL) / VW_QUALITY_SAMPLE_RATE);
  int64_t wall_start_us = vw_platform_get_monotonic_time_us();
  if (wall_start_us < 0) return false;
  int64_t position_us = 0;
  while (position_us < duration_us) {
    if (!vw_worker_client_send_position(client, position_us, position_us, 1.0f, 0U)) {
      fprintf(stderr, "quality runner: failed sending look-ahead POSITION\n");
      return false;
    }
    if (!vw_quality_drain_available(client, result)) return false;
    position_us += VW_QUALITY_POSITION_INTERVAL_US;
    if (position_us > duration_us) position_us = duration_us;
    if (!vw_quality_sleep_until(wall_start_us + position_us)) return false;
  }
  if (!vw_worker_client_send_position(client, duration_us, duration_us, 1.0f, 0U)) {
    fprintf(stderr, "quality runner: failed sending final look-ahead POSITION\n");
    return false;
  }

  if (!vw_quality_wait_for_source_eof(client, result, marker_prefix)) return false;
  if (!vw_quality_shutdown_and_drain(client, result)) return false;
  int64_t wall_end_us = vw_platform_get_monotonic_time_us();
  if (wall_end_us < 0) return false;
  *out_runtime_us = wall_end_us - wall_start_us;
  return true;
}

static const char* vw_quality_mode_name(vw_quality_mode_t mode) {
  switch (mode) {
    case VW_QUALITY_MODE_OFFLINE:
      return "offline";
    case VW_QUALITY_MODE_LIVE:
      return "live";
    case VW_QUALITY_MODE_LOOKAHEAD:
      return "lookahead";
    default:
      return "unknown";
  }
}

#ifdef _WIN32
typedef struct {
  FILE* file;
} vw_quality_file_loader_t;

static size_t vw_quality_file_read(void* context, void* output, size_t read_size) {
  vw_quality_file_loader_t* loader = (vw_quality_file_loader_t*)context;
  return loader && loader->file ? fread(output, 1, read_size, loader->file) : 0;
}

static bool vw_quality_file_eof(void* context) {
  vw_quality_file_loader_t* loader = (vw_quality_file_loader_t*)context;
  return !loader || !loader->file || feof(loader->file) != 0;
}

static void vw_quality_file_close(void* context) {
  vw_quality_file_loader_t* loader = (vw_quality_file_loader_t*)context;
  if (!loader) return;
  if (loader->file) fclose(loader->file);
  free(loader);
}

static struct whisper_context* vw_quality_whisper_init_utf8_path(const char* model_path,
                                                                 struct whisper_context_params params) {
  int wide_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, model_path, -1, NULL, 0);
  if (wide_length <= 0) return NULL;
  wchar_t* wide_path = (wchar_t*)calloc((size_t)wide_length, sizeof(wchar_t));
  if (!wide_path) return NULL;
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, model_path, -1, wide_path, wide_length) <= 0) {
    free(wide_path);
    return NULL;
  }
  vw_quality_file_loader_t* context = (vw_quality_file_loader_t*)calloc(1, sizeof(*context));
  if (context) context->file = _wfopen(wide_path, L"rb");
  free(wide_path);
  if (!context || !context->file) {
    free(context);
    return NULL;
  }
  struct whisper_model_loader loader = {context, vw_quality_file_read, vw_quality_file_eof, vw_quality_file_close};
  return whisper_init_with_params(&loader, params);
}
#endif

static void vw_quality_whisper_log(enum ggml_log_level level, const char* text, void* user_data) {
  (void)user_data;
  if (level == GGML_LOG_LEVEL_ERROR && text) {
    fprintf(stderr, "whisper: %s", text);
  }
}

static bool vw_quality_run_offline(const vw_quality_options_t* options, const vw_quality_audio_t* audio,
                                   vw_quality_result_t* result, int64_t* out_runtime_us) {
  if (!options || !audio || !result || !out_runtime_us) return false;

  whisper_log_set(vw_quality_whisper_log, NULL);

  struct whisper_context_params cparams = whisper_context_default_params();
  cparams.use_gpu = (strcmp(options->backend, "cpu") != 0);
  cparams.gpu_device = 0;

  struct whisper_context* ctx = NULL;
#ifdef _WIN32
  ctx = vw_quality_whisper_init_utf8_path(options->model_path, cparams);
#else
  ctx = whisper_init_from_file_with_params(options->model_path, cparams);
#endif
  if (!ctx) {
    fprintf(stderr, "quality runner: failed to load whisper model '%s'\n", options->model_path);
    return false;
  }

  bool gpu_active = false;
  if (cparams.use_gpu) {
    size_t dev_count = ggml_backend_dev_count();
    size_t remaining = (size_t)cparams.gpu_device;
    for (size_t i = 0; i < dev_count; i++) {
      ggml_backend_dev_t dev = ggml_backend_dev_get(i);
      enum ggml_backend_dev_type type = ggml_backend_dev_type(dev);
      if (type == GGML_BACKEND_DEVICE_TYPE_GPU || type == GGML_BACKEND_DEVICE_TYPE_ACCEL) {
        if (remaining == 0) {
          gpu_active = true;
          break;
        }
        remaining--;
      }
    }
  }
  snprintf(result->resolved_backend, sizeof(result->resolved_backend), "%s", gpu_active ? "gpu" : "cpu");

  float* pcm = (float*)malloc(audio->sample_count * sizeof(float));
  if (!pcm) {
    whisper_free(ctx);
    return false;
  }
  for (size_t i = 0; i < audio->sample_count; i++) {
    pcm[i] = (float)audio->samples[i] / 32768.0f;
  }

  struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  wparams.strategy = WHISPER_SAMPLING_GREEDY;
  wparams.print_progress = false;
  wparams.print_special = false;
  wparams.print_realtime = false;
  wparams.print_timestamps = false;
  wparams.translate = false;
  wparams.language = options->language;
  wparams.n_threads = options->threads;

  int64_t start_us = vw_platform_get_monotonic_time_us();
  int rc = whisper_full(ctx, wparams, pcm, (int)audio->sample_count);
  int64_t end_us = vw_platform_get_monotonic_time_us();

  free(pcm);

  if (rc != 0) {
    fprintf(stderr, "quality runner: whisper_full failed with code %d\n", rc);
    whisper_free(ctx);
    return false;
  }

  int64_t elapsed_us = end_us - start_us;
  if (elapsed_us < 0) elapsed_us = 0;
  *out_runtime_us = elapsed_us;
  result->inference_us = elapsed_us;
  result->queued_audio_us = (int64_t)((audio->sample_count * 1000000ULL) / VW_QUALITY_SAMPLE_RATE);
  result->dropped_audio_us = 0;

  int n_segments = whisper_full_n_segments(ctx);
  for (int i = 0; i < n_segments; i++) {
    const char* txt = whisper_full_get_segment_text(ctx, i);
    if (!txt) continue;
    int64_t t0 = whisper_full_get_segment_t0(ctx, i);
    int64_t t1 = whisper_full_get_segment_t1(ctx, i);

    vw_caption_segment_t seg;
    memset(&seg, 0, sizeof(seg));
    seg.start_pts_us = t0 * 10000LL;
    seg.end_pts_us = t1 * 10000LL;
    seg.text_utf8 = (char*)txt;
    size_t len = strlen(txt);
    if (len > VW_MAX_TEXT_BYTES) len = VW_MAX_TEXT_BYTES;
    seg.text_bytes = (uint16_t)len;

    if (!vw_quality_append_segment(result, &seg)) {
      fprintf(stderr, "quality runner: failed appending segment in offline mode\n");
      whisper_free(ctx);
      return false;
    }
  }

  whisper_free(ctx);
  return true;
}

static void vw_quality_json_string(FILE* out, const char* text) {
  fputc('"', out);
  if (text) {
    for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
      switch (*p) {
        case '"':
          fputs("\\\"", out);
          break;
        case '\\':
          fputs("\\\\", out);
          break;
        case '\b':
          fputs("\\b", out);
          break;
        case '\f':
          fputs("\\f", out);
          break;
        case '\n':
          fputs("\\n", out);
          break;
        case '\r':
          fputs("\\r", out);
          break;
        case '\t':
          fputs("\\t", out);
          break;
        default:
          if (*p < 0x20U)
            fprintf(out, "\\u%04x", (unsigned int)*p);
          else
            fputc(*p, out);
          break;
      }
    }
  }
  fputc('"', out);
}

static void vw_quality_print_result(const vw_quality_options_t* options, const vw_quality_audio_t* audio,
                                    const vw_quality_result_t* result, int64_t runtime_us) {
  int64_t audio_duration_us = (int64_t)((audio->sample_count * 1000000ULL) / VW_QUALITY_SAMPLE_RATE);
  printf("{\n  \"mode\": ");
  vw_quality_json_string(stdout, vw_quality_mode_name(options->mode));
  printf(",\n  \"language\": ");
  vw_quality_json_string(stdout, options->language);
  printf(",\n  \"audio_duration_us\": %" PRId64, audio_duration_us);
  printf(",\n  \"runtime_us\": %" PRId64, runtime_us);
  printf(",\n  \"queued_audio_us\": %" PRId64, result->queued_audio_us);
  printf(",\n  \"inference_us\": %" PRId64, result->inference_us);
  printf(",\n  \"dropped_audio_us\": %" PRId64, result->dropped_audio_us);
  printf(",\n  \"resolved_backend\": ");
  vw_quality_json_string(stdout, result->resolved_backend);
  printf(",\n  \"segments\": [");
  for (size_t i = 0; i < result->segment_count; i++) {
    const vw_quality_segment_t* segment = &result->segments[i];
    printf("%s\n    {\"start_pts_us\": %" PRId64 ", \"end_pts_us\": %" PRId64 ", \"text\": ", i ? "," : "",
           segment->start_pts_us, segment->end_pts_us);
    vw_quality_json_string(stdout, segment->text);
    fputc('}', stdout);
  }
  printf("\n  ]\n}\n");
}

static void vw_quality_usage(const char* argv0) {
  fprintf(stderr,
          "Usage: %s --model PATH --audio WAV --language en|ro --mode offline|live|lookahead "
          "[--worker PATH] [--backend auto|gpu|cpu] [--threads 1..16] [--model-dir DIR]\n",
          argv0);
}

static bool vw_quality_parse_args(int argc, char** argv, vw_quality_options_t* options) {
  memset(options, 0, sizeof(*options));
  options->backend = "auto";
  options->threads = 4;
  bool mode_set = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--worker") == 0 && i + 1 < argc)
      options->worker_path = argv[++i];
    else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc)
      options->model_path = argv[++i];
    else if (strcmp(argv[i], "--model-dir") == 0 && i + 1 < argc)
      options->model_dir = argv[++i];
    else if (strcmp(argv[i], "--audio") == 0 && i + 1 < argc)
      options->audio_path = argv[++i];
    else if (strcmp(argv[i], "--language") == 0 && i + 1 < argc)
      options->language = argv[++i];
    else if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc)
      options->backend = argv[++i];
    else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
      options->threads = atoi(argv[++i]);
    else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
      const char* mode = argv[++i];
      if (strcmp(mode, "offline") == 0)
        options->mode = VW_QUALITY_MODE_OFFLINE;
      else if (strcmp(mode, "live") == 0)
        options->mode = VW_QUALITY_MODE_LIVE;
      else if (strcmp(mode, "lookahead") == 0)
        options->mode = VW_QUALITY_MODE_LOOKAHEAD;
      else
        return false;
      mode_set = true;
    } else {
      return false;
    }
  }
  if (!options->model_path || !options->audio_path || !options->language || !mode_set) return false;
  if (options->mode != VW_QUALITY_MODE_OFFLINE && !options->worker_path) return false;
  if (strcmp(options->language, "en") != 0 && strcmp(options->language, "ro") != 0) return false;
  if (strcmp(options->backend, "auto") != 0 && strcmp(options->backend, "gpu") != 0 &&
      strcmp(options->backend, "cpu") != 0)
    return false;
  return options->threads >= 1 && options->threads <= 16;
}

int main(int argc, char** argv) {
  vw_quality_options_t options;
  if (!vw_quality_parse_args(argc, argv, &options)) {
    vw_quality_usage(argv[0]);
    return 2;
  }

  vw_quality_audio_t audio;
  if (!vw_quality_load_wav(options.audio_path, &audio)) return 1;

  vw_quality_result_t result;
  memset(&result, 0, sizeof(result));
  result.segments = (vw_quality_segment_t*)calloc(VW_QUALITY_MAX_SEGMENTS, sizeof(vw_quality_segment_t));
  if (!result.segments) {
    vw_quality_free_audio(&audio);
    return 1;
  }

  if (options.mode == VW_QUALITY_MODE_OFFLINE) {
    int64_t runtime_us = 0;
    bool ok = vw_quality_run_offline(&options, &audio, &result, &runtime_us);
    if (ok) vw_quality_print_result(&options, &audio, &result, runtime_us);
    free(result.segments);
    vw_quality_free_audio(&audio);
    return ok ? 0 : 1;
  }

  char marker_prefix[VW_PATH_MAX_BYTES];
  if (!vw_quality_make_marker_prefix(marker_prefix, sizeof(marker_prefix)) ||
      !vw_quality_prepare_markers(marker_prefix)) {
    fprintf(stderr, "quality runner: failed preparing worker completion markers\n");
    free(result.segments);
    vw_quality_free_audio(&audio);
    return 1;
  }

  uint8_t auth_token[VW_AUTH_TOKEN_BYTES];
  char endpoint[VW_MAX_SOURCE_URL_BYTES];
  if (!vw_platform_get_random_bytes(auth_token, sizeof(auth_token)) ||
      !vw_quality_make_endpoint(endpoint, sizeof(endpoint))) {
    fprintf(stderr, "quality runner: failed generating IPC credentials\n");
    vw_quality_cleanup_markers(marker_prefix);
    free(result.segments);
    vw_quality_free_audio(&audio);
    return 1;
  }

  vw_worker_client_t* client = vw_worker_client_launch_and_connect_ex(
      options.worker_path, endpoint, auth_token, options.model_path, options.backend, options.language, options.threads,
      -1, options.model_dir, false);
  memset(auth_token, 0, sizeof(auth_token));
  if (!client) {
    fprintf(stderr, "quality runner: failed launching/connecting worker\n");
    vw_quality_cleanup_markers(marker_prefix);
    free(result.segments);
    vw_quality_free_audio(&audio);
    return 1;
  }

  int64_t runtime_us = 0;
  bool ok = options.mode == VW_QUALITY_MODE_LIVE
                ? vw_quality_run_live(client, &audio, &result, &runtime_us)
                : vw_quality_run_lookahead(client, &options, &audio, &result, marker_prefix, &runtime_us);

  vw_worker_client_disconnect(client);

  int64_t final_dropped_audio_us = 0;
  if (!vw_quality_read_drop_marker(marker_prefix, &final_dropped_audio_us)) {
    fprintf(stderr, "quality runner: invalid benchmark worker; enable VW_QUALITY_BENCHMARK_HOOKS when configuring\n");
    ok = false;
  } else {
    result.dropped_audio_us = final_dropped_audio_us;
  }
  vw_quality_cleanup_markers(marker_prefix);

  if (ok && result.dropped_audio_us > 0) {
    fprintf(stderr, "quality runner: invalid benchmark; worker dropped %" PRId64 " us of audio\n",
            result.dropped_audio_us);
    ok = false;
  }
  if (ok) vw_quality_print_result(&options, &audio, &result, runtime_us);

  free(result.segments);
  vw_quality_free_audio(&audio);
  return ok ? 0 : 1;
}