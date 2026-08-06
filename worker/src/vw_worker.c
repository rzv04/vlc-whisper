#include "vw_worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_ipc_transport.h"
#include "vw_protocol_codec.h"
#include "vw_protocol_types.h"

// Constant-time comparison of two 32-byte tokens to prevent timing attacks
static bool verify_token_constant_time(const uint8_t token_a[VW_AUTH_TOKEN_BYTES],
                                       const uint8_t token_b[VW_AUTH_TOKEN_BYTES]) {
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < VW_AUTH_TOKEN_BYTES; i++) {
    diff |= (token_a[i] ^ token_b[i]);
  }
  return diff == 0;
}

int vw_worker_run(const vw_worker_config_t* config) {
  if (!config) {
    return 1;
  }

  vw_ipc_handle_t* handle = vw_ipc_listen(config->pipe_name);
  if (!handle) {
    return 1;
  }

  bool running = true;
  bool authenticated = false;
  uint8_t header_buf[sizeof(vw_frame_header_t)];

  vw_whisper_engine_t* engine = vw_whisper_engine_init(config->model_path);
  vw_audio_buffer_t* audio_buf = vw_audio_buffer_create(160000);  // 10s at 16kHz
  vw_segment_builder_t* builder = vw_segment_builder_create();
  vw_session_id_t session_id;
  memset(&session_id, 0, sizeof(session_id));
  bool session_active = false;
  uint32_t sequence = 1;

  while (running) {
    int32_t bytes_read = 0;
    while (bytes_read < (int32_t)sizeof(vw_frame_header_t)) {
      int32_t res = vw_ipc_receive(handle, header_buf + bytes_read, sizeof(vw_frame_header_t) - bytes_read);
      if (res < 0) {
        if (res == VW_IPC_RECV_TIMEOUT) continue;  // timeout — keep waiting (video pause)
        running = false;                           // fatal (VW_IPC_RECV_FATAL): peer closed / broken pipe
        break;
      }
      bytes_read += res;
    }
    if (!running) break;

    vw_frame_header_t header;
    if (!vw_protocol_decode_header(header_buf, sizeof(vw_frame_header_t), &header)) {
      break;
    }

    if (!vw_protocol_validate_header(&header)) {
      break;
    }

    uint8_t* payload_buf = NULL;
    if (header.payload_length > 0) {
      payload_buf = (uint8_t*)malloc(header.payload_length);
      if (!payload_buf) break;

      // receive the payload in a loop to handle partial reads
      uint32_t payload_read = 0;
      while (payload_read < header.payload_length) {
        int32_t res = vw_ipc_receive(handle, payload_buf + payload_read, header.payload_length - payload_read);
        if (res < 0) {
          if (res == VW_IPC_RECV_TIMEOUT) continue;  // timeout — keep waiting (video pause)
          running = false;                           // fatal (VW_IPC_RECV_FATAL): peer closed / broken pipe
          break;
        }
        payload_read += res;
      }
    }

    if (!running) {
      free(payload_buf);
      break;
    }

    union {
      vw_msg_hello_t hello;
      vw_msg_start_t start;
      vw_msg_audio_t audio;
      vw_msg_control_t control;
      vw_msg_status_t status;
    } payload_decoded;

    memset(&payload_decoded, 0, sizeof(payload_decoded));

    bool valid_payload = false;
    if (vw_protocol_decode_payload(header.type, payload_buf, header.payload_length, &payload_decoded)) {
      if (vw_protocol_validate_payload(header.type, &payload_decoded)) {
        valid_payload = true;
      }
    }

    // also enforce after receiving
    if (!valid_payload && header.payload_length > 0) {
      free(payload_buf);
      break;  // Invalid payload
    }

    if (!authenticated) {
      if (header.type != VW_MSG_HELLO) {
        free(payload_buf);
        break;  // First message must be HELLO
      }
      if (!verify_token_constant_time(config->auth_token, payload_decoded.hello.auth_token)) {
        free(payload_buf);
        break;  // Auth failed
      }
      authenticated = true;

      // Reply HELLO_ACK with the negotiated version and supported capabilities
      vw_msg_hello_ack_t ack = {.selected_major = VW_PROTOCOL_VERSION_MAJOR,
                                .selected_minor = VW_PROTOCOL_VERSION_MINOR,
                                .capability_flags = VW_CAPABILITY_PCM_S16LE_16K_MONO,
                                .worker_version = VW_WORKER_VERSION,
                                .worker_version_length = VW_WORKER_VERSION_LENGTH};
      uint8_t ack_payload[256];
      size_t ack_len = 0;
      if (!vw_protocol_encode_payload(VW_MSG_HELLO_ACK, &ack, ack_payload, sizeof(ack_payload), &ack_len)) {
        free(payload_buf);
        break;
      }
      vw_frame_header_t ack_hdr = {.magic = VW_PROTOCOL_MAGIC,
                                   .major = VW_PROTOCOL_VERSION_MAJOR,
                                   .type = VW_MSG_HELLO_ACK,
                                   .payload_length = (uint32_t)ack_len,
                                   .sequence = 1};
      uint8_t ack_hdr_buf[sizeof(vw_frame_header_t)];
      if (!vw_protocol_encode_header(&ack_hdr, ack_hdr_buf, sizeof(ack_hdr_buf))) {
        free(payload_buf);
        break;
      }
      vw_ipc_send(handle, ack_hdr_buf, sizeof(ack_hdr_buf));
      vw_ipc_send(handle, ack_payload, ack_len);

      free(payload_buf);
      continue;
    }

    switch (header.type) {
      case VW_MSG_START_SESSION: {
        if (!engine) {
          // Model absent or invalid: reply with ERROR frame (recoverable = 0)
          vw_msg_error_t err_msg;
          memset(&err_msg, 0, sizeof(err_msg));
          memcpy(err_msg.session_id.bytes, payload_decoded.start.session_id.bytes, VW_SESSION_ID_BYTES);
          err_msg.error_code = E_MODEL_MISSING;
          err_msg.recoverable = 0;
          snprintf(err_msg.message, sizeof(err_msg.message), "%s", "Whisper model file missing or invalid");

          uint8_t err_payload[512];
          size_t err_len = 0;
          if (vw_protocol_encode_payload(VW_MSG_ERROR, &err_msg, err_payload, sizeof(err_payload), &err_len)) {
            vw_frame_header_t err_hdr = {.magic = VW_PROTOCOL_MAGIC,
                                         .major = VW_PROTOCOL_VERSION_MAJOR,
                                         .type = VW_MSG_ERROR,
                                         .payload_length = (uint32_t)err_len,
                                         .sequence = ++sequence};
            uint8_t err_hdr_buf[sizeof(vw_frame_header_t)];
            vw_protocol_encode_header(&err_hdr, err_hdr_buf, sizeof(err_hdr_buf));
            vw_ipc_send(handle, err_hdr_buf, sizeof(err_hdr_buf));
            vw_ipc_send(handle, err_payload, err_len);
          }
          break;
        }

        memcpy(session_id.bytes, payload_decoded.start.session_id.bytes, VW_SESSION_ID_BYTES);
        session_active = true;

        // Reply STARTED (header-only)
        vw_frame_header_t started_hdr = {.magic = VW_PROTOCOL_MAGIC,
                                         .major = VW_PROTOCOL_VERSION_MAJOR,
                                         .type = VW_MSG_STARTED,
                                         .payload_length = 0,
                                         .sequence = ++sequence};
        uint8_t started_hdr_buf[sizeof(vw_frame_header_t)];
        vw_protocol_encode_header(&started_hdr, started_hdr_buf, sizeof(started_hdr_buf));
        vw_ipc_send(handle, started_hdr_buf, sizeof(started_hdr_buf));
        break;
      }

      case VW_MSG_AUDIO_PCM: {
        if (!session_active ||
            memcmp(payload_decoded.audio.session_id.bytes, session_id.bytes, VW_SESSION_ID_BYTES) != 0) {
          break;
        }

        const int16_t* pcm16 = (const int16_t*)payload_decoded.audio.pcm_data;
        size_t sample_count = payload_decoded.audio.pcm_bytes / sizeof(int16_t);
        int64_t pts_us = payload_decoded.audio.start_pts_us;

        if (audio_buf && pcm16 && sample_count > 0) {
          vw_audio_buffer_append_s16le(audio_buf, pcm16, sample_count, pts_us);

          // 8-second window with 2-second hop
          while (vw_audio_buffer_get_count(audio_buf) >= VW_WINDOW_SAMPLES) {
            float window_samples[VW_WINDOW_SAMPLES];
            int64_t window_pts_us = 0;
            size_t read_cnt = vw_audio_buffer_get_samples(audio_buf, window_samples, VW_WINDOW_SAMPLES, &window_pts_us);

            if (read_cnt > 0 && engine) {
              if (vw_vad_detect_speech_energy(window_samples, read_cnt, VW_VAD_ENERGY_THRESHOLD)) {
                if (vw_whisper_engine_transcribe_pcm(engine, window_samples, read_cnt)) {
                  const char* text = vw_whisper_engine_get_text(engine);
                  if (text && text[0] != '\0' && builder) {
                    // Calculate duration in microseconds based on sample count and sample rate
                    int64_t duration_us = (int64_t)(((double)read_cnt / VW_AUDIO_SAMPLE_RATE) * 1000000.0);
                    vw_segment_builder_push_hypothesis(builder, text, window_pts_us, window_pts_us + duration_us);
                  }
                }
              }
            }
            vw_audio_buffer_drain(audio_buf, VW_HOP_SAMPLES);
          }
        }
        break;
      }

      case VW_MSG_STOP_SESSION: {
        session_active = false;
        if (audio_buf) vw_audio_buffer_clear(audio_buf);
        break;
      }

      case VW_MSG_SHUTDOWN:
        running = false;
        break;

      default:
        break;
    }

    // Drain completed caption segments from builder and emit over IPC
    if (builder) {
      vw_caption_segment_t seg;
      while (vw_segment_builder_pop(builder, &seg)) {
        memcpy(seg.session_id.bytes, session_id.bytes, VW_SESSION_ID_BYTES);
        // TODO: Handle segment text length exceeding VW_SEGMENT_BUILDER_MAX_TEXT_BYTES (truncate or split), 43 is
        // hardcoded
        uint8_t seg_payload[43 + VW_SEGMENT_BUILDER_MAX_TEXT_BYTES];  // 43 bytes for fixed fields + max text length
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
        }
        if (seg.text_utf8) free(seg.text_utf8);
      }
    }

    free(payload_buf);
  }

  if (audio_buf) vw_audio_buffer_free(audio_buf);
  if (builder) vw_segment_builder_free(builder);
  if (engine) vw_whisper_engine_free(engine);

  vw_ipc_close(handle);
  return authenticated ? 0 : 1;
}
