#define _POSIX_C_SOURCE 200809L

#include "vw_protocol_codec.h"

#include <string.h>

// Encode a header struct into a byte buffer. Returns true on success, false on failure (e.g., buffer too small).
bool vw_protocol_encode_header(const vw_frame_header_t* header, uint8_t* buffer, size_t buffer_size) {
  if (!header || !buffer || buffer_size < sizeof(vw_frame_header_t)) return false;
  memcpy(buffer, header, sizeof(vw_frame_header_t));
  return true;
}

// Decode a header struct from a byte buffer. Returns true on success, false on failure (e.g., buffer too small or
// invalid header).
bool vw_protocol_decode_header(const uint8_t* buffer, size_t buffer_size, vw_frame_header_t* header) {
  if (!buffer || !header || buffer_size < sizeof(vw_frame_header_t)) return false;
  memcpy(header, buffer, sizeof(vw_frame_header_t));
  return vw_protocol_validate_header(header);
}

// Encode a payload struct into a byte buffer. Returns true on success, false on failure (e.g., buffer too small).
#define ENC_FIELD(val)                                     \
  do {                                                     \
    if (written + sizeof(val) > buffer_size) return false; \
    memcpy(buffer + written, &(val), sizeof(val));         \
    written += sizeof(val);                                \
  } while (0)

// Encode a byte array into a buffer. Returns true on success, false on failure (e.g., buffer too small).
#define ENC_BYTES(ptr, len)                          \
  do {                                               \
    if (written + (len) > buffer_size) return false; \
    memcpy(buffer + written, (ptr), (len));          \
    written += (len);                                \
  } while (0)

// Encode a pointer to a byte array into a proper protocol type buffer. Returns true on success, false on failure (e.g.,
// buffer too small).
bool vw_protocol_encode_payload(vw_message_type_t type, const void* payload, uint8_t* buffer, size_t buffer_size,
                                size_t* out_written) {
  if (!buffer || !out_written) return false;
  if (!payload && type != VW_MSG_SHUTDOWN) return false;
  size_t written = 0;
  switch (type) {
    case VW_MSG_HELLO: {
      const vw_msg_hello_t* p = (const vw_msg_hello_t*)payload;
      ENC_FIELD(p->min_major);
      ENC_FIELD(p->max_major);
      ENC_BYTES(p->auth_token, VW_AUTH_TOKEN_BYTES);
      ENC_FIELD(p->client_version_length);
      ENC_BYTES(p->client_version, p->client_version_length);
      break;
    }
    case VW_MSG_HELLO_ACK: {
      const vw_msg_hello_ack_t* p = (const vw_msg_hello_ack_t*)payload;
      ENC_FIELD(p->selected_major);
      ENC_FIELD(p->selected_minor);
      ENC_FIELD(p->capability_flags);
      ENC_FIELD(p->worker_version_length);
      ENC_BYTES(p->worker_version, p->worker_version_length);
      break;
    }
    case VW_MSG_START_SESSION: {
      const vw_msg_start_t* p = (const vw_msg_start_t*)payload;
      ENC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      ENC_FIELD(p->timeline_origin_pts_us);
      ENC_FIELD(p->sample_rate);
      ENC_FIELD(p->channels);
      ENC_FIELD(p->sample_format);
      uint16_t model_id_len = (uint16_t)strnlen(p->model_id, VW_MAX_MODEL_ID_BYTES);
      if (model_id_len >= VW_MAX_MODEL_ID_BYTES) model_id_len = VW_MAX_MODEL_ID_BYTES - 1;
      ENC_FIELD(model_id_len);
      ENC_BYTES(p->model_id, model_id_len);
      uint16_t lang_len = (uint16_t)strnlen(p->language, 16);
      if (lang_len >= 16) lang_len = 15;
      ENC_FIELD(lang_len);
      ENC_BYTES(p->language, lang_len);
      ENC_FIELD(p->source_kind);
      uint16_t url_len =
          p->source_url_len > 0 ? p->source_url_len : (uint16_t)strnlen(p->source_url, VW_MAX_SOURCE_URL_BYTES);
      if (url_len >= VW_MAX_SOURCE_URL_BYTES) url_len = VW_MAX_SOURCE_URL_BYTES - 1;
      ENC_FIELD(url_len);
      if (url_len > 0) {
        ENC_BYTES(p->source_url, url_len);
      }
      break;
    }
    case VW_MSG_POSITION: {
      const vw_msg_position_t* p = (const vw_msg_position_t*)payload;
      ENC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      ENC_FIELD(p->current_pts_us);
      ENC_FIELD(p->input_time_us);
      ENC_FIELD(p->playback_rate);
      ENC_FIELD(p->flags);
      break;
    }
    case VW_MSG_AUDIO_PCM: {
      const vw_msg_audio_t* p = (const vw_msg_audio_t*)payload;
      ENC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      ENC_FIELD(p->start_pts_us);
      ENC_FIELD(p->duration_us);
      ENC_FIELD(p->pcm_bytes);
      ENC_BYTES(p->pcm_data, p->pcm_bytes);
      break;
    }
    case VW_MSG_PAUSE:
    case VW_MSG_RESUME:
    case VW_MSG_STOP_SESSION: {
      const vw_msg_control_t* p = (const vw_msg_control_t*)payload;
      ENC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      ENC_FIELD(p->reason);
      break;
    }
    case VW_MSG_CAPTION_SEGMENT: {
      const vw_caption_segment_t* p = (const vw_caption_segment_t*)payload;
      if (p->text_bytes > 0 && !p->text_utf8) return false;
      ENC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      ENC_FIELD(p->segment_id);
      ENC_FIELD(p->start_pts_us);
      ENC_FIELD(p->end_pts_us);
      ENC_FIELD(p->is_final);
      ENC_FIELD(p->text_bytes);
      ENC_BYTES(p->text_utf8, p->text_bytes);
      break;
    }
    case VW_MSG_STATUS: {
      const vw_msg_status_t* p = (const vw_msg_status_t*)payload;
      ENC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      ENC_FIELD(p->state);
      ENC_FIELD(p->queued_audio_us);
      ENC_FIELD(p->inference_us);
      ENC_FIELD(p->dropped_audio_us);
      ENC_BYTES(p->resolved_backend, 16);
      break;
    }
    case VW_MSG_MODEL_CTRL: {
      const vw_msg_model_ctrl_t* p = (const vw_msg_model_ctrl_t*)payload;
      ENC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      ENC_FIELD(p->action);
      ENC_BYTES(p->model_id, 32);
      break;
    }
    case VW_MSG_MODEL_PROGRESS: {
      const vw_msg_model_progress_t* p = (const vw_msg_model_progress_t*)payload;
      ENC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      ENC_FIELD(p->stage);
      ENC_FIELD(p->pct);
      ENC_FIELD(p->bytes_done);
      ENC_FIELD(p->bytes_total);
      ENC_BYTES(p->model_id, 32);
      break;
    }
    case VW_MSG_ERROR: {
      const vw_msg_error_t* p = (const vw_msg_error_t*)payload;
      ENC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      ENC_FIELD(p->error_code);
      ENC_FIELD(p->recoverable);
      // Reject messages that cannot carry their own NUL terminator within the buffer.
      // Enforcing strlen < VW_MAX_ERROR_MSG_BYTES guarantees every encoded message is
      // NUL-terminated, so the decoder can preserve it byte-for-byte (see decode path).
      if (strnlen(p->message, VW_MAX_ERROR_MSG_BYTES) >= VW_MAX_ERROR_MSG_BYTES) {
        return false;
      }
      ENC_BYTES(p->message, VW_MAX_ERROR_MSG_BYTES);
      break;
    }
    case VW_MSG_STARTED: {
      const vw_msg_started_t* p = (const vw_msg_started_t*)payload;
      ENC_FIELD(p->source_active);
      break;
    }
    case VW_MSG_SHUTDOWN:
      break;
    default:
      return false;
  }
  *out_written = written;
  return true;
}

#define DEC_FIELD(val)                                      \
  do {                                                      \
    if (read_pos + sizeof(val) > buffer_size) return false; \
    memcpy(&(val), buffer + read_pos, sizeof(val));         \
    read_pos += sizeof(val);                                \
  } while (0)

#define DEC_BYTES(ptr, len)                           \
  do {                                                \
    if (read_pos + (len) > buffer_size) return false; \
    memcpy((ptr), buffer + read_pos, (len));          \
    read_pos += (len);                                \
  } while (0)

#define DEC_PTR(ptr, len)                             \
  do {                                                \
    if (read_pos + (len) > buffer_size) return false; \
    (ptr) = (void*)(buffer + read_pos);               \
    read_pos += (len);                                \
  } while (0)

bool vw_protocol_decode_payload(vw_message_type_t type, const uint8_t* buffer, size_t buffer_size, void* out_payload) {
  if (!buffer) return false;
  if (!out_payload && type != VW_MSG_SHUTDOWN) return false;
  size_t read_pos = 0;
  switch (type) {
    case VW_MSG_HELLO: {
      vw_msg_hello_t* p = (vw_msg_hello_t*)out_payload;
      DEC_FIELD(p->min_major);
      DEC_FIELD(p->max_major);
      DEC_BYTES(p->auth_token, VW_AUTH_TOKEN_BYTES);
      DEC_FIELD(p->client_version_length);
      DEC_PTR(p->client_version, p->client_version_length);
      break;
    }
    case VW_MSG_HELLO_ACK: {
      vw_msg_hello_ack_t* p = (vw_msg_hello_ack_t*)out_payload;
      DEC_FIELD(p->selected_major);
      DEC_FIELD(p->selected_minor);
      DEC_FIELD(p->capability_flags);
      DEC_FIELD(p->worker_version_length);
      DEC_PTR(p->worker_version, p->worker_version_length);
      break;
    }
    case VW_MSG_START_SESSION: {
      vw_msg_start_t* p = (vw_msg_start_t*)out_payload;
      memset(p, 0, sizeof(*p));
      DEC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      DEC_FIELD(p->timeline_origin_pts_us);
      DEC_FIELD(p->sample_rate);
      DEC_FIELD(p->channels);
      DEC_FIELD(p->sample_format);
      uint16_t model_id_len = 0;
      DEC_FIELD(model_id_len);
      if (model_id_len >= VW_MAX_MODEL_ID_BYTES) return false;
      DEC_BYTES(p->model_id, model_id_len);
      p->model_id[model_id_len] = '\0';
      uint16_t lang_len = 0;
      DEC_FIELD(lang_len);
      if (lang_len >= 16) return false;
      DEC_BYTES(p->language, lang_len);
      p->language[lang_len] = '\0';
      DEC_FIELD(p->source_kind);
      p->source_url_len = 0;
      p->source_url[0] = '\0';
      if (read_pos < buffer_size) {
        uint16_t url_len = 0;
        DEC_FIELD(url_len);
        if (url_len >= VW_MAX_SOURCE_URL_BYTES) return false;
        if (url_len > 0) {
          DEC_BYTES(p->source_url, url_len);
        }
        p->source_url[url_len] = '\0';
        p->source_url_len = url_len;
      }
      break;
    }
    case VW_MSG_POSITION: {
      vw_msg_position_t* p = (vw_msg_position_t*)out_payload;
      DEC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      DEC_FIELD(p->current_pts_us);
      DEC_FIELD(p->input_time_us);
      DEC_FIELD(p->playback_rate);
      DEC_FIELD(p->flags);
      break;
    }
    case VW_MSG_AUDIO_PCM: {
      vw_msg_audio_t* p = (vw_msg_audio_t*)out_payload;
      DEC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      DEC_FIELD(p->start_pts_us);
      DEC_FIELD(p->duration_us);
      DEC_FIELD(p->pcm_bytes);
      DEC_PTR(p->pcm_data, p->pcm_bytes);
      break;
    }
    case VW_MSG_PAUSE:
    case VW_MSG_RESUME:
    case VW_MSG_STOP_SESSION: {
      vw_msg_control_t* p = (vw_msg_control_t*)out_payload;
      DEC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      DEC_FIELD(p->reason);
      break;
    }
    case VW_MSG_CAPTION_SEGMENT: {
      vw_caption_segment_t* p = (vw_caption_segment_t*)out_payload;
      DEC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      DEC_FIELD(p->segment_id);
      DEC_FIELD(p->start_pts_us);
      DEC_FIELD(p->end_pts_us);
      DEC_FIELD(p->is_final);
      DEC_FIELD(p->text_bytes);
      DEC_PTR(p->text_utf8, p->text_bytes);
      break;
    }
    case VW_MSG_STATUS: {
      vw_msg_status_t* p = (vw_msg_status_t*)out_payload;
      if (buffer_size >= 60) {
        DEC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
        DEC_FIELD(p->state);
        DEC_FIELD(p->queued_audio_us);
        DEC_FIELD(p->inference_us);
        DEC_FIELD(p->dropped_audio_us);
        DEC_BYTES(p->resolved_backend, 16);
        // Ensure NUL termination for string safety (spec: NUL-padded "gpu"|"cpu").
        p->resolved_backend[15] = '\0';
      } else {
        if (buffer_size < 44) return false;
        DEC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
        DEC_FIELD(p->state);
        DEC_FIELD(p->queued_audio_us);
        DEC_FIELD(p->inference_us);
        DEC_FIELD(p->dropped_audio_us);
        memset(p->resolved_backend, 0, 16);
      }
      break;
    }
    case VW_MSG_MODEL_CTRL: {
      vw_msg_model_ctrl_t* p = (vw_msg_model_ctrl_t*)out_payload;
      if (buffer_size != VW_MSG_MODEL_CTRL_PAYLOAD_BYTES) return false;
      DEC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      DEC_FIELD(p->action);
      DEC_BYTES(p->model_id, 32);
      p->model_id[31] = '\0';
      break;
    }
    case VW_MSG_MODEL_PROGRESS: {
      vw_msg_model_progress_t* p = (vw_msg_model_progress_t*)out_payload;
      if (buffer_size != VW_MSG_MODEL_PROGRESS_PAYLOAD_BYTES) return false;
      DEC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      DEC_FIELD(p->stage);
      DEC_FIELD(p->pct);
      DEC_FIELD(p->bytes_done);
      DEC_FIELD(p->bytes_total);
      DEC_BYTES(p->model_id, 32);
      p->model_id[31] = '\0';
      break;
    }
    case VW_MSG_ERROR: {
      vw_msg_error_t* p = (vw_msg_error_t*)out_payload;
      DEC_BYTES(p->session_id.bytes, VW_SESSION_ID_BYTES);
      DEC_FIELD(p->error_code);
      DEC_FIELD(p->recoverable);
      DEC_BYTES(p->message, VW_MAX_ERROR_MSG_BYTES);
      // Preserve contract-conforming messages byte-for-byte: they carry their own NUL
      // terminator, so leave the buffer untouched. Only force-terminate hostile or
      // truncated payloads that contain no NUL anywhere in the fixed-size field.
      if (!memchr(p->message, '\0', VW_MAX_ERROR_MSG_BYTES)) {
        p->message[VW_MAX_ERROR_MSG_BYTES - 1] = '\0';
      }
      break;
    }
    case VW_MSG_STARTED: {
      vw_msg_started_t* p = (vw_msg_started_t*)out_payload;
      DEC_FIELD(p->source_active);
      break;
    }
    case VW_MSG_SHUTDOWN:
      break;
    default:
      return false;
  }
  return true;
}
