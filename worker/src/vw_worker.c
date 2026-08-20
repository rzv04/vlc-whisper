#include "vw_worker.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_ipc_transport.h"
#include "vw_log.h"
#include "vw_platform.h"
#include "vw_protocol_codec.h"
#include "vw_protocol_types.h"
#include "vw_protocol_util.h"
#include "vw_source_decoder.h"
#include "vw_vad.h"
#include "vw_worker_queue.h"

#ifdef _WIN32
#include <mfapi.h>
#include <process.h>
#else
#include <unistd.h>
#endif

// Argument bundle passed to the worker IPC reader thread.
typedef struct vw_worker_reader_arg {
  vw_ipc_handle_t* handle;
  vw_worker_queue_t* queue;
  _Atomic bool* running;
} vw_worker_reader_arg_t;

// Constant-time comparison of two 32-byte tokens to prevent timing attacks
static bool verify_token_constant_time(const uint8_t token_a[VW_AUTH_TOKEN_BYTES],
                                       const uint8_t token_b[VW_AUTH_TOKEN_BYTES]) {
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < VW_AUTH_TOKEN_BYTES; i++) {
    diff |= (token_a[i] ^ token_b[i]);
  }
  return diff == 0;
}

// Returns the symbolic name of a STOP reason code for logs, or the numeric value as a string when
// the code has no documented macro (e.g. 0 = plain user stop). STOP-only: PAUSE/RESUME also use
// reason 1U, so this must never be called for those message types (the name would mislabel them).
static const char* vw_worker_stop_reason_name(uint16_t reason) {
  switch (reason) {
    case VW_CTRL_REASON_USER_STOP:
      return "USER_STOP";
    case VW_CTRL_REASON_SEEK_DISCONTINUITY:
      return "SEEK_DISCONTINUITY";
    case VW_CTRL_REASON_MEDIA_END:
      return "MEDIA_END";
    default: {
      // Thread-local so a log from any thread cannot alias/overwrite the buffer.
      _Thread_local static char buf[16];
      snprintf(buf, sizeof(buf), "%u", reason);
      return buf;
    }
  }
}

// Builds and sends a VW_MSG_ERROR frame over IPC. Returns true on success.
static bool send_error(vw_ipc_handle_t* handle, const uint8_t session_id[VW_SESSION_ID_BYTES], vw_error_code_t code,
                       uint8_t recoverable, const char* msg, uint32_t* sequence) {
  vw_msg_error_t err_msg;
  memset(&err_msg, 0, sizeof(err_msg));
  memcpy(err_msg.session_id.bytes, session_id, VW_SESSION_ID_BYTES);
  err_msg.error_code = code;
  err_msg.recoverable = recoverable;
  snprintf(err_msg.message, sizeof(err_msg.message), "%s", msg);

  uint8_t err_payload[512];
  size_t err_len = 0;
  if (!vw_protocol_encode_payload(VW_MSG_ERROR, &err_msg, err_payload, sizeof(err_payload), &err_len)) {
    return false;
  }
  vw_frame_header_t err_hdr = {.magic = VW_PROTOCOL_MAGIC,
                               .major = VW_PROTOCOL_VERSION_MAJOR,
                               .type = VW_MSG_ERROR,
                               .payload_length = (uint32_t)err_len,
                               .sequence = ++(*sequence)};
  uint8_t err_hdr_buf[sizeof(vw_frame_header_t)];
  vw_protocol_encode_header(&err_hdr, err_hdr_buf, sizeof(err_hdr_buf));
  vw_ipc_send(handle, err_hdr_buf, sizeof(err_hdr_buf));
  vw_ipc_send(handle, err_payload, err_len);
  return true;
}

// Dedicated IPC reader thread (ADR-013): the only thread that reads from the pipe. Continuously
// drains frames into the bounded worker frame queue so inference on the main loop never stalls
// transport reads. Never sends; all replies stay single-writer in vw_worker_run's main loop.
static void* vw_worker_reader_main(void* arg) {
  vw_worker_reader_arg_t* a = (vw_worker_reader_arg_t*)arg;
  uint8_t header_buf[sizeof(vw_frame_header_t)];
  uint8_t* payload_buf = NULL;

  while (atomic_load(a->running)) {
    // Receive the 20-byte header, retrying on timeout but leaving promptly when shutting down.
    int32_t bytes_read = 0;
    while (bytes_read < (int32_t)sizeof(vw_frame_header_t)) {
      int32_t res = vw_ipc_receive(a->handle, header_buf + bytes_read, sizeof(vw_frame_header_t) - bytes_read);
      if (res < 0) {
        if (res == VW_IPC_RECV_TIMEOUT) {
          if (!atomic_load(a->running)) return NULL;  // bounded join: exit on shutdown
          continue;                                   // timeout — keep waiting (video pause)
        }
        goto fatal;  // VW_IPC_RECV_FATAL: peer closed / broken pipe
      }
      bytes_read += res;
    }

    vw_frame_header_t header;
    if (!vw_protocol_decode_header(header_buf, sizeof(vw_frame_header_t), &header)) {
      goto fatal;
    }
    if (!vw_protocol_validate_header(&header)) {
      goto fatal;
    }

    payload_buf = NULL;
    if (header.payload_length > 0) {
      payload_buf = (uint8_t*)malloc(header.payload_length);
      if (!payload_buf) {
        goto fatal;
      }
      uint32_t payload_read = 0;
      while (payload_read < header.payload_length) {
        int32_t res = vw_ipc_receive(a->handle, payload_buf + payload_read, header.payload_length - payload_read);
        if (res < 0) {
          if (res == VW_IPC_RECV_TIMEOUT) {
            if (!atomic_load(a->running)) {
              free(payload_buf);
              return NULL;
            }
            continue;
          }
          goto fatal;
        }
        payload_read += (uint32_t)res;
      }
    }

    // The queue takes ownership of payload and frees it if the frame is dropped on overflow.
    // A failed push means a counted AUDIO drop, or a control dropped because the all-control queue
    // held nothing safe to evict (queued START/STOP/SHUTDOWN transitions win); log it to surface
    // queue backpressure.
    if (!vw_worker_queue_push(a->queue, header.type, payload_buf, header.payload_length)) {
      vw_log_event(VW_LOG_LEVEL_WARN, "WORKER_QUEUE", "frame queue push failed (type=%u); frame dropped", header.type);
    }
    payload_buf = NULL;
  }
  return NULL;

fatal:
  free(payload_buf);
  atomic_store(a->running, false);
  vw_log_event(VW_LOG_LEVEL_WARN, "WORKER_READER", "fatal pipe read (peer closed); waking main loop");
  // Wake the main loop with a synthetic SHUTDOWN so it exits instead of polling an empty queue.
  vw_worker_queue_push(a->queue, VW_MSG_SHUTDOWN, NULL, 0);
  return NULL;
}

int vw_worker_run(const vw_worker_config_t* config) {
  if (!config) {
    return 1;
  }

#ifdef _WIN32
  MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
#endif

  vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_LIFECYCLE", "worker started (pid %d)", (int)getpid());
  vw_ipc_handle_t* handle = vw_ipc_listen(config->pipe_name);
  if (!handle) {
    vw_log_event(VW_LOG_LEVEL_ERROR, "WORKER_LIFECYCLE", "vw_ipc_listen FAILED for %s; worker exiting",
                 config->pipe_name);
#ifdef _WIN32
    MFShutdown();
#endif
    return 1;
  }
  vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_LIFECYCLE", "listening on %s", config->pipe_name);

  _Atomic bool running = true;
  bool authenticated = false;

  // Heap-allocate the 8s analysis window once (128k floats) rather than a 512KB stack frame per message.
  float* window_samples = (float*)malloc(VW_WINDOW_SAMPLES * sizeof(float));
  if (!window_samples) {
    vw_ipc_close(handle);
#ifdef _WIN32
    MFShutdown();
#endif
    return 1;
  }

  vw_whisper_engine_t* engine = vw_whisper_engine_init(config->model_path, config->backend, config->gpu_device);
  vw_log_event(engine ? VW_LOG_LEVEL_INFO : VW_LOG_LEVEL_WARN, "WORKER_ENGINE",
               engine ? "whisper engine loaded" : "whisper engine init FAILED (model missing/invalid)");

  struct whisper_vad_context* vad_ctx = NULL;
  if (config->vad_model_path[0] != '\0') {
    vad_ctx = vw_vad_init_default(config->vad_model_path);
  }
  vw_log_event(
      VW_LOG_LEVEL_INFO, "WORKER_VAD",
      vad_ctx ? "Silero VAD model loaded (%s)" : "Silero VAD model not loaded; using zero-config RMS Energy fallback",
      config->vad_model_path[0] != '\0' ? config->vad_model_path : "none");

  vw_audio_buffer_t* audio_buf = vw_audio_buffer_create(160000);  // 10s at 16kHz
  vw_segment_builder_t* builder = vw_segment_builder_create();
  if (!audio_buf || !builder) {
    if (audio_buf) vw_audio_buffer_free(audio_buf);
    if (builder) vw_segment_builder_free(builder);
    if (engine) vw_whisper_engine_free(engine);
    if (vad_ctx) vw_vad_free(vad_ctx);
    free(window_samples);
    vw_ipc_close(handle);
#ifdef _WIN32
    MFShutdown();
#endif
    return 1;
  }
  vw_session_id_t session_id;
  memset(&session_id, 0, sizeof(session_id));
  bool session_active = false;
  bool paused = false;  // PAUSE suspends window accumulation; RESUME clears it (step 16)
  uint32_t sequence = 1;

  // Source Look-Ahead Mode State
  vw_source_decoder_t* source_decoder = NULL;
  bool source_mode = false;
  bool source_eof = false;
  int eof_retry_count = 0;
  int64_t current_playback_pts_us = 0;
  int64_t last_playback_pts_us = -1;
  int64_t decoded_pts_us = 0;
  const int64_t lead_target_us = 30000000LL;  // 30s look-ahead horizon

  vw_worker_queue_t* queue = vw_worker_queue_create(32);
  if (!queue) {
    free(window_samples);
    if (audio_buf) vw_audio_buffer_free(audio_buf);
    if (builder) vw_segment_builder_free(builder);
    if (engine) vw_whisper_engine_free(engine);
    if (vad_ctx) vw_vad_free(vad_ctx);
    vw_ipc_close(handle);
#ifdef _WIN32
    MFShutdown();
#endif
    return 1;
  }

  vw_worker_reader_arg_t reader_arg = {.handle = handle, .queue = queue, .running = &running};
  vw_thread_t reader_thread;
  if (!vw_platform_thread_create(&reader_thread, vw_worker_reader_main, &reader_arg)) {
    // Reader spawn failure: fail closed rather than starve the pipe.
    vw_log_event(VW_LOG_LEVEL_ERROR, "WORKER_READER", "reader thread creation FAILED; worker exiting");
    vw_worker_queue_destroy(queue);
    free(window_samples);
    if (audio_buf) vw_audio_buffer_free(audio_buf);
    if (builder) vw_segment_builder_free(builder);
    if (engine) vw_whisper_engine_free(engine);
    if (vad_ctx) vw_vad_free(vad_ctx);
    vw_ipc_close(handle);
#ifdef _WIN32
    MFShutdown();
#endif
    return 1;
  }
  vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_READER", "reader thread started; draining pipe into frame queue");

  while (atomic_load(&running)) {
    vw_worker_frame_t frame;
    bool has_frame = false;

    while (atomic_load(&running)) {
      if (vw_worker_queue_pop(queue, &frame)) {
        has_frame = true;
        break;
      }
      // If we have look-ahead decoding work to do (and haven't hit EOF), don't sleep
      if (session_active && source_mode && source_decoder && !paused && !source_eof &&
          (decoded_pts_us < vw_saturating_add_i64(current_playback_pts_us, lead_target_us))) {
        break;
      }
      vw_platform_sleep_ms(5);
    }
    if (!atomic_load(&running)) {
      break;
    }

    if (has_frame) {
      union {
        vw_msg_hello_t hello;
        vw_msg_start_t start;
        vw_msg_audio_t audio;
        vw_msg_control_t control;
        vw_msg_status_t status;
        vw_msg_position_t position;
      } payload_decoded;

      memset(&payload_decoded, 0, sizeof(payload_decoded));

      bool valid_payload = false;
      if (vw_protocol_decode_payload(frame.type, frame.payload, frame.payload_len, &payload_decoded)) {
        if (vw_protocol_validate_payload(frame.type, &payload_decoded)) {
          valid_payload = true;
        }
      }

      // also enforce after receiving
      if (!valid_payload && frame.payload_len > 0) {
        vw_log_event(VW_LOG_LEVEL_ERROR, "WORKER_PROTOCOL", "invalid payload (type=%u len=%u); exiting", frame.type,
                     frame.payload_len);
        free(frame.payload);
        break;  // Invalid payload
      }

      if (!authenticated) {
        if (frame.type != VW_MSG_HELLO) {
          vw_log_event(VW_LOG_LEVEL_ERROR, "WORKER_AUTH", "first frame was not HELLO (type=%u); rejecting", frame.type);
          free(frame.payload);
          break;  // First message must be HELLO
        }
        if (!verify_token_constant_time(config->auth_token, payload_decoded.hello.auth_token)) {
          vw_log_event(VW_LOG_LEVEL_ERROR, "WORKER_AUTH", "HELLO token mismatch; rejecting connection");
          free(frame.payload);
          break;  // Auth failed
        }
        authenticated = true;
        vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_AUTH", "HELLO authenticated; replying HELLO_ACK");

        // Reply HELLO_ACK with the negotiated version and supported capabilities (including SOURCE_MODE)
        vw_msg_hello_ack_t ack = {.selected_major = VW_PROTOCOL_VERSION_MAJOR,
                                  .selected_minor = VW_PROTOCOL_VERSION_MINOR,
                                  .capability_flags = VW_CAPABILITY_PCM_S16LE_16K_MONO | VW_CAPABILITY_SOURCE_MODE,
                                  .worker_version = VW_WORKER_VERSION,
                                  .worker_version_length = VW_WORKER_VERSION_LENGTH};
        uint8_t ack_payload[256];
        size_t ack_len = 0;
        if (!vw_protocol_encode_payload(VW_MSG_HELLO_ACK, &ack, ack_payload, sizeof(ack_payload), &ack_len)) {
          free(frame.payload);
          break;
        }
        vw_frame_header_t ack_hdr = {.magic = VW_PROTOCOL_MAGIC,
                                     .major = VW_PROTOCOL_VERSION_MAJOR,
                                     .type = VW_MSG_HELLO_ACK,
                                     .payload_length = (uint32_t)ack_len,
                                     .sequence = 1};
        uint8_t ack_hdr_buf[sizeof(vw_frame_header_t)];
        if (!vw_protocol_encode_header(&ack_hdr, ack_hdr_buf, sizeof(ack_hdr_buf))) {
          free(frame.payload);
          break;
        }
        vw_ipc_send(handle, ack_hdr_buf, sizeof(ack_hdr_buf));
        vw_ipc_send(handle, ack_payload, ack_len);

        free(frame.payload);
        continue;
      }

      switch (frame.type) {
        case VW_MSG_START_SESSION: {
          if (session_active) {
            if (memcmp(session_id.bytes, payload_decoded.start.session_id.bytes, VW_SESSION_ID_BYTES) == 0) {
              break;  // Duplicate START for the active session — ignore
            }
            // Media swap or new epoch detected mid-session: cleanly close old session
            vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_SESSION",
                         "media swap / new epoch detected (restarting worker session)");
            if (source_decoder) {
              vw_source_decoder_close(source_decoder);
              source_decoder = NULL;
            }
            if (audio_buf) vw_audio_buffer_clear(audio_buf);
            if (builder) {
              vw_caption_segment_t stale_seg;
              while (vw_segment_builder_pop(builder, &stale_seg)) {
                if (stale_seg.text_utf8) free(stale_seg.text_utf8);
              }
            }
            if (vad_ctx) vw_vad_reset_state(vad_ctx);
            session_active = false;
          }
          if (payload_decoded.start.sample_rate != VW_AUDIO_SAMPLE_RATE) {
            vw_log_event(VW_LOG_LEVEL_WARN, "WORKER_SESSION", "START rejected: E_AUDIO_FORMAT (rate=%u)",
                         payload_decoded.start.sample_rate);
            send_error(handle, payload_decoded.start.session_id.bytes, E_AUDIO_FORMAT, 1,
                       "Unsupported sample rate (expected 16000)", &sequence);
            break;
          }
          if (!engine) {
            // Model absent or invalid: reply with ERROR frame (recoverable = 0)
            vw_log_event(VW_LOG_LEVEL_WARN, "WORKER_SESSION", "START rejected: E_MODEL_MISSING");
            send_error(handle, payload_decoded.start.session_id.bytes, E_MODEL_MISSING, 0,
                       "Whisper model file missing or invalid", &sequence);
            break;
          }

          memcpy(session_id.bytes, payload_decoded.start.session_id.bytes, VW_SESSION_ID_BYTES);
          session_active = true;
          // Discard any segment-builder hypothesis and reset VAD LSTM state
          if (builder) {
            vw_segment_builder_clear(builder);
          }
          if (vad_ctx) {
            vw_vad_reset_state(vad_ctx);
          }

          // Check if source_url is supplied for Ahead-of-Time Look-Ahead Decoding
          if (source_decoder) {
            vw_source_decoder_close(source_decoder);
            source_decoder = NULL;
            source_mode = false;
          }
          if (payload_decoded.start.source_url_len > 0 && payload_decoded.start.source_url[0] != '\0') {
            vw_source_decoder_info_t sinfo = {0};
            source_decoder = vw_source_decoder_open(payload_decoded.start.source_url, &sinfo);
            if (source_decoder) {
              source_mode = true;
              source_eof = false;
              current_playback_pts_us = payload_decoded.start.timeline_origin_pts_us;
              last_playback_pts_us = current_playback_pts_us;
              decoded_pts_us = current_playback_pts_us;
              source_eof = false;
              eof_retry_count = 0;
              vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_SOURCE",
                           "source look-ahead mode ACTIVE for '%s' (dur=%lldus fmt=%s)",
                           payload_decoded.start.source_url, (long long)sinfo.duration_us, sinfo.container_format);
            } else {
              source_mode = false;
              source_eof = false;
              eof_retry_count = 0;
              vw_log_event(VW_LOG_LEVEL_WARN, "WORKER_SOURCE",
                           "failed to open source url '%s'; falling back to live PCM stream",
                           payload_decoded.start.source_url);
              send_error(handle, payload_decoded.start.session_id.bytes, E_SOURCE_OPEN, 1,
                         "Failed to open source MRL; falling back to live PCM stream", &sequence);
            }
          } else {
            source_mode = false;
            source_eof = false;
            eof_retry_count = 0;
          }

          vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_SESSION", "session started (STARTED sent source_active=%d)",
                       source_mode ? 1 : 0);

          // Reply STARTED with 1-byte payload indicating source_active status
          vw_msg_started_t started_payload = {.source_active =
                                                  source_mode ? VW_SOURCE_ACTIVE_ACTIVE : VW_SOURCE_ACTIVE_INACTIVE};
          uint8_t started_payload_buf[1];
          size_t started_written = 0;
          vw_protocol_encode_payload(VW_MSG_STARTED, &started_payload, started_payload_buf, sizeof(started_payload_buf),
                                     &started_written);

          vw_frame_header_t started_hdr = {.magic = VW_PROTOCOL_MAGIC,
                                           .major = VW_PROTOCOL_VERSION_MAJOR,
                                           .type = VW_MSG_STARTED,
                                           .payload_length = (uint32_t)started_written,
                                           .sequence = ++sequence};
          uint8_t started_hdr_buf[sizeof(vw_frame_header_t)];
          vw_protocol_encode_header(&started_hdr, started_hdr_buf, sizeof(started_hdr_buf));
          vw_ipc_send(handle, started_hdr_buf, sizeof(started_hdr_buf));
          if (started_written > 0) {
            vw_ipc_send(handle, started_payload_buf, started_written);
          }
          break;
        }

        case VW_MSG_POSITION: {
          if (!session_active ||
              memcmp(payload_decoded.position.session_id.bytes, session_id.bytes, VW_SESSION_ID_BYTES) != 0) {
            break;
          }
          current_playback_pts_us = payload_decoded.position.current_pts_us;
          bool is_pos_paused = (payload_decoded.position.flags & VW_POSITION_FLAG_PAUSED) != 0;
          if (is_pos_paused != paused) {
            paused = is_pos_paused;
            if (paused) {
              if (audio_buf) vw_audio_buffer_clear(audio_buf);
              if (builder) vw_segment_builder_clear(builder);
              if (vad_ctx) vw_vad_reset_state(vad_ctx);
            } else {
              if (source_mode && source_decoder && current_playback_pts_us >= 0) {
                vw_source_decoder_seek(source_decoder, current_playback_pts_us);
                decoded_pts_us = current_playback_pts_us;
                last_playback_pts_us = current_playback_pts_us;
                source_eof = false;
                eof_retry_count = 0;
                if (audio_buf) vw_audio_buffer_clear(audio_buf);
                if (builder) vw_segment_builder_clear(builder);
                if (vad_ctx) vw_vad_reset_state(vad_ctx);
              }
            }
          }

          if (source_mode && source_decoder) {
            bool seek_flag = (payload_decoded.position.flags & VW_POSITION_FLAG_SEEK) != 0;
            bool backward_jump =
                (last_playback_pts_us >= 0 &&
                 payload_decoded.position.current_pts_us < vw_saturating_sub_i64(last_playback_pts_us, 500000LL));
            bool forward_past_decoded =
                (payload_decoded.position.current_pts_us > vw_saturating_add_i64(decoded_pts_us, 1000000LL));

            if ((seek_flag || backward_jump || forward_past_decoded) &&
                payload_decoded.position.current_pts_us != last_playback_pts_us) {
              vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_SEEK",
                           "re-seeking source decoder to %lldus (flag=%d back=%d fwd=%d)",
                           (long long)payload_decoded.position.current_pts_us, (int)seek_flag, (int)backward_jump,
                           (int)forward_past_decoded);
              vw_source_decoder_seek(source_decoder, payload_decoded.position.current_pts_us);
              decoded_pts_us = payload_decoded.position.current_pts_us;
              source_eof = false;
              eof_retry_count = 0;
              if (audio_buf) vw_audio_buffer_clear(audio_buf);
              if (builder) vw_segment_builder_clear(builder);
              if (vad_ctx) vw_vad_reset_state(vad_ctx);
            }
            last_playback_pts_us = payload_decoded.position.current_pts_us;
          }
          break;
        }

        case VW_MSG_AUDIO_PCM: {
          if (!session_active || paused || source_mode ||
              memcmp(payload_decoded.audio.session_id.bytes, session_id.bytes, VW_SESSION_ID_BYTES) != 0) {
            break;  // dropped if paused, inactive, or in source mode
          }

          const int16_t* pcm16 = (const int16_t*)payload_decoded.audio.pcm_data;
          size_t sample_count = payload_decoded.audio.pcm_bytes / sizeof(int16_t);
          int64_t pts_us = payload_decoded.audio.start_pts_us;

          if (audio_buf && pcm16 && sample_count > 0) {
            vw_audio_buffer_append_s16le(audio_buf, pcm16, sample_count, pts_us);

            // 8-second window with 2-second hop
            while (vw_audio_buffer_get_count(audio_buf) >= VW_WINDOW_SAMPLES) {
              int64_t window_pts_us = 0;
              size_t read_cnt =
                  vw_audio_buffer_get_samples(audio_buf, window_samples, VW_WINDOW_SAMPLES, &window_pts_us);

              if (read_cnt > 0 && engine) {
                if (vw_vad_detect_speech(window_samples, read_cnt, vad_ctx)) {
                  vw_log_event(VW_LOG_LEVEL_DEBUG, "WORKER_INFERENCE", "speech window @%lldus; transcribing",
                               (long long)window_pts_us);
                  if (vw_whisper_engine_transcribe_pcm(engine, window_samples, read_cnt)) {
                    if (builder) {
                      int n_segs = vw_whisper_engine_get_segment_count(engine);
                      for (int s_idx = 0; s_idx < n_segs; s_idx++) {
                        vw_whisper_segment_t seg_info;
                        if (vw_whisper_engine_get_segment(engine, s_idx, &seg_info) && seg_info.text_utf8) {
                          if (seg_info.no_speech_prob >= 0.60f) {
                            continue;
                          }
                          int64_t seg_start_pts = vw_saturating_add_i64(window_pts_us, seg_info.t0_us);
                          int64_t seg_end_pts = vw_saturating_add_i64(window_pts_us, seg_info.t1_us);

                          vw_segment_builder_push_hypothesis(builder, seg_info.text_utf8, seg_start_pts, seg_end_pts);
                        }
                      }
                    }
                  } else {
                    vw_log_event(VW_LOG_LEVEL_WARN, "WORKER_INFERENCE", "whisper_full FAILED @%lldus",
                                 (long long)window_pts_us);
                  }
                }
              }
              vw_audio_buffer_drain(audio_buf, VW_HOP_SAMPLES);
            }
          }
          break;
        }

        case VW_MSG_PAUSE: {
          if (!session_active) break;
          paused = true;
          if (audio_buf) vw_audio_buffer_clear(audio_buf);
          if (builder) vw_segment_builder_clear(builder);
          if (vad_ctx) vw_vad_reset_state(vad_ctx);
          vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_SESSION", "paused; window cleared, transcription suspended");
          break;
        }

        case VW_MSG_RESUME: {
          if (!session_active) break;
          paused = false;
          if (source_mode && source_decoder && current_playback_pts_us >= 0) {
            vw_source_decoder_seek(source_decoder, current_playback_pts_us);
            decoded_pts_us = current_playback_pts_us;
            last_playback_pts_us = current_playback_pts_us;
            source_eof = false;
            eof_retry_count = 0;
            if (audio_buf) vw_audio_buffer_clear(audio_buf);
            if (builder) vw_segment_builder_clear(builder);
            if (vad_ctx) vw_vad_reset_state(vad_ctx);
          }
          vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_SESSION", "resumed; transcription active");
          break;
        }

        case VW_MSG_STOP_SESSION: {
          session_active = false;
          last_playback_pts_us = -1;
          vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_SESSION", "session stopped (reason=%s)",
                       vw_worker_stop_reason_name(payload_decoded.control.reason));
          if (source_decoder) {
            vw_source_decoder_close(source_decoder);
            source_decoder = NULL;
            source_mode = false;
          }
          if (audio_buf) vw_audio_buffer_clear(audio_buf);
          if (builder) vw_segment_builder_clear(builder);
          if (vad_ctx) vw_vad_reset_state(vad_ctx);
          break;
        }

        case VW_MSG_SHUTDOWN:
          vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_SESSION", "shutdown requested; exiting");
          if (source_decoder) {
            vw_source_decoder_close(source_decoder);
            source_decoder = NULL;
            source_mode = false;
          }
          atomic_store(&running, false);
          break;

        default:
          break;
      }

      free(frame.payload);
    }

    // Ahead-of-Time Look-Ahead Decoding Step
    if (session_active && source_mode && source_decoder && !paused && !source_eof &&
        (decoded_pts_us < vw_saturating_add_i64(current_playback_pts_us, lead_target_us))) {
      int16_t decode_chunk[VW_LOOKAHEAD_CHUNK_SAMPLES];  // 2 seconds at 16kHz
      int64_t chunk_pts_us = -1;
      size_t samples_read =
          vw_source_decoder_read_s16le(source_decoder, decode_chunk, VW_LOOKAHEAD_CHUNK_SAMPLES, &chunk_pts_us);
      if (samples_read > 0 && audio_buf) {
        eof_retry_count = 0;
        int64_t actual_pts = (chunk_pts_us >= 0) ? chunk_pts_us : decoded_pts_us;
        vw_audio_buffer_append_s16le(audio_buf, decode_chunk, samples_read, actual_pts);
        decoded_pts_us = actual_pts + (int64_t)((samples_read * 1000000ULL) / 16000ULL);

        while (vw_audio_buffer_get_count(audio_buf) >= VW_WINDOW_SAMPLES) {
          int64_t window_pts_us = 0;
          size_t read_cnt = vw_audio_buffer_get_samples(audio_buf, window_samples, VW_WINDOW_SAMPLES, &window_pts_us);
          if (read_cnt > 0 && engine) {
            if (vw_vad_detect_speech(window_samples, read_cnt, vad_ctx)) {
              vw_log_event(VW_LOG_LEVEL_DEBUG, "WORKER_INFERENCE", "lookahead speech window @%lldus; transcribing",
                           (long long)window_pts_us);
              if (vw_whisper_engine_transcribe_pcm(engine, window_samples, read_cnt)) {
                if (builder) {
                  int n_segs = vw_whisper_engine_get_segment_count(engine);
                  for (int s_idx = 0; s_idx < n_segs; s_idx++) {
                    vw_whisper_segment_t seg_info;
                    if (vw_whisper_engine_get_segment(engine, s_idx, &seg_info) && seg_info.text_utf8) {
                      if (seg_info.no_speech_prob >= 0.60f) {
                        continue;
                      }
                      int64_t seg_start_pts = vw_saturating_add_i64(window_pts_us, seg_info.t0_us);
                      int64_t seg_end_pts = vw_saturating_add_i64(window_pts_us, seg_info.t1_us);

                      vw_segment_builder_push_hypothesis(builder, seg_info.text_utf8, seg_start_pts, seg_end_pts);
                    }
                  }
                }
              }
            }
          }
          vw_audio_buffer_drain(audio_buf, VW_HOP_SAMPLES);
        }
      } else if (samples_read == 0) {
        if (++eof_retry_count >= 3) {
          source_eof = true;
          // Trailing flush at source EOF (S-03)
          if (audio_buf && vw_audio_buffer_get_count(audio_buf) > 0) {
            int64_t window_pts_us = 0;
            size_t remaining =
                vw_audio_buffer_get_samples(audio_buf, window_samples, VW_WINDOW_SAMPLES, &window_pts_us);
            if (remaining > 0 && engine) {
              if (vw_vad_detect_speech(window_samples, remaining, vad_ctx)) {
                vw_log_event(VW_LOG_LEVEL_DEBUG, "WORKER_INFERENCE",
                             "lookahead trailing speech window @%lldus; transcribing", (long long)window_pts_us);
                if (vw_whisper_engine_transcribe_pcm(engine, window_samples, remaining)) {
                  if (builder) {
                    int n_segs = vw_whisper_engine_get_segment_count(engine);
                    for (int s_idx = 0; s_idx < n_segs; s_idx++) {
                      vw_whisper_segment_t seg_info;
                      if (vw_whisper_engine_get_segment(engine, s_idx, &seg_info) && seg_info.text_utf8) {
                        if (seg_info.no_speech_prob >= 0.60f) {
                          continue;
                        }
                        int64_t seg_start_pts = vw_saturating_add_i64(window_pts_us, seg_info.t0_us);
                        int64_t seg_end_pts = vw_saturating_add_i64(window_pts_us, seg_info.t1_us);

                        vw_segment_builder_push_hypothesis(builder, seg_info.text_utf8, seg_start_pts, seg_end_pts);
                      }
                    }
                  }
                }
              }
            }
            vw_audio_buffer_clear(audio_buf);
          }
        } else {
          vw_platform_sleep_ms(5);
        }
      }
    }

    // Drain completed caption segments from builder and emit over IPC
    if (builder) {
      vw_caption_segment_t seg;
      while (vw_segment_builder_pop(builder, &seg)) {
        memcpy(seg.session_id.bytes, session_id.bytes, VW_SESSION_ID_BYTES);
        uint8_t seg_payload[VW_CAPTION_SEGMENT_FIXED_BYTES + VW_SEGMENT_BUILDER_MAX_TEXT_BYTES];
        size_t seg_len = 0;
        if (vw_protocol_encode_payload(VW_MSG_CAPTION_SEGMENT, &seg, seg_payload, sizeof(seg_payload), &seg_len)) {
          vw_frame_header_t seg_hdr = {.magic = VW_PROTOCOL_MAGIC,
                                       .major = VW_PROTOCOL_VERSION_MAJOR,
                                       .type = VW_MSG_CAPTION_SEGMENT,
                                       .payload_length = (uint32_t)seg_len,
                                       .sequence = ++sequence};
          uint8_t seg_hdr_buf[sizeof(vw_frame_header_t)];
          vw_protocol_encode_header(&seg_hdr, seg_hdr_buf, sizeof(seg_hdr_buf));
          vw_ipc_send(handle, seg_hdr_buf, sizeof(seg_hdr_buf));
          vw_ipc_send(handle, seg_payload, seg_len);
          vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_SEGMENT",
                       "emitted segment id=%llu start=%lld end=%lld is_final=%d text_len=%zu",
                       (unsigned long long)seg.segment_id, (long long)seg.start_pts_us, (long long)seg.end_pts_us,
                       seg.is_final, seg.text_utf8 ? strlen(seg.text_utf8) : 0);
        }
        if (seg.text_utf8) free(seg.text_utf8);
      }
    }
  }

  // Shutdown order
  atomic_store(&running, false);
  vw_platform_thread_join(reader_thread);
  const uint64_t dropped_audio_us = vw_worker_queue_get_dropped_audio_us(queue);
  vw_worker_queue_destroy(queue);

  if (source_decoder) {
    vw_source_decoder_close(source_decoder);
    source_decoder = NULL;
  }

  free(window_samples);
  if (audio_buf) vw_audio_buffer_free(audio_buf);
  if (builder) vw_segment_builder_free(builder);
  if (engine) vw_whisper_engine_free(engine);
  if (vad_ctx) vw_vad_free(vad_ctx);

  vw_ipc_close(handle);
#ifdef _WIN32
  MFShutdown();
#endif
  vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_LIFECYCLE", "worker exiting (rc=%d, dropped_audio_us=%llu)",
               authenticated ? 0 : 1, (unsigned long long)dropped_audio_us);
  return authenticated ? 0 : 1;
}
