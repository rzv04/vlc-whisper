#include "vw_protocol_codec.h"

bool vw_protocol_validate_header(const vw_frame_header_t* header) {
  if (!header) return false;
  if (header->magic != VW_PROTOCOL_MAGIC) return false;
  if (header->major != VW_PROTOCOL_VERSION_MAJOR) return false;
  if (header->payload_length > VW_MAX_PAYLOAD_BYTES) return false;
  return true;
}

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Validate if a string is a valid UTF-8 sequence. Returns true if valid, false otherwise.
// Reject overlong sequences, UTF-16 surrogates, and code points above U+10FFFF.
static bool is_valid_utf8(const char* str, size_t len) {
  const uint8_t* s = (const uint8_t*)str;
  for (size_t i = 0; i < len;) {
    uint8_t c = s[i++];

    // 1-byte ASCII
    if (c <= 0x7F) continue;

    size_t extra = 0;
    uint32_t code_point = 0;
    uint32_t min_cp = 0;

    // Decode leading byte and establish bounds
    if ((c & 0xE0) == 0xC0) {  // 2-byte sequence
      extra = 1;
      code_point = c & 0x1F;
      min_cp = 0x80;                  // Minimum valid code point for 2 bytes
    } else if ((c & 0xF0) == 0xE0) {  // 3-byte sequence
      extra = 2;
      code_point = c & 0x0F;
      min_cp = 0x800;                 // Minimum valid code point for 3 bytes
    } else if ((c & 0xF8) == 0xF0) {  // 4-byte sequence
      extra = 3;
      code_point = c & 0x07;
      min_cp = 0x10000;  // Minimum valid code point for 4 bytes
    } else {
      return false;  // Invalid lead byte
    }

    // Ensure we don't read past the end of the buffer
    if (i + extra > len) return false;

    // Decode continuation bytes
    for (size_t j = 0; j < extra; j++) {
      uint8_t next = s[i++];
      if ((next & 0xC0) != 0x80) return false;
      code_point = (code_point << 6) | (next & 0x3F);
    }

    // Check for overlong sequences
    if (code_point < min_cp) return false;

    // Check for UTF-16 surrogates (U+D800 to U+DFFF)
    if (code_point >= 0xD800 && code_point <= 0xDFFF) return false;

    // Check maximum valid Unicode code point
    if (code_point > 0x10FFFF) return false;
  }
  return true;
}

// Check if a string is empty or consists only of whitespace characters (space, tab, newline, carriage return).
static bool is_empty_or_whitespace(const char* s, size_t len) {
  if (len == 0) return true;
  for (size_t i = 0; i < len; i++) {
    if (s[i] != ' ' && s[i] != '\n' && s[i] != '\r' && s[i] != '\t') return false;
  }
  return true;
}

// Validate the payload struct for a given message type. Returns true if valid, false otherwise.
bool vw_protocol_validate_payload(vw_message_type_t type, const void* payload) {
  if (!payload && type != VW_MSG_SHUTDOWN && type != VW_MSG_STARTED) return false;
  switch (type) {
    case VW_MSG_HELLO: {
      const vw_msg_hello_t* p = (const vw_msg_hello_t*)payload;
      if (p->client_version_length > 0 && !p->client_version) return false;
      return true;
    }
    case VW_MSG_HELLO_ACK: {
      const vw_msg_hello_ack_t* p = (const vw_msg_hello_ack_t*)payload;
      if (p->worker_version_length > 0 && !p->worker_version) return false;
      return true;
    }
    case VW_MSG_START_SESSION:
      return true;
    case VW_MSG_AUDIO_PCM: {
      const vw_msg_audio_t* p = (const vw_msg_audio_t*)payload;
      if (p->duration_us <= 0 || p->duration_us > 30000000) return false;
      // pcm_bytes = duration_us * 16000 / 1000000 * 2 = duration_us * 32 / 1000
      // integer truncated; valid audio under 31.25 µs silently passes?. Negligible at 16kHz (0.5 sample)
      uint32_t expected_bytes = (uint32_t)((p->duration_us * 32) / 1000);
      if (p->pcm_bytes != expected_bytes) return false;
      if (p->pcm_bytes > 0 && !p->pcm_data) return false;
      return true;
    }
    case VW_MSG_PAUSE:
    case VW_MSG_RESUME:
    case VW_MSG_STOP_SESSION:
    case VW_MSG_STATUS:
    case VW_MSG_ERROR:
    case VW_MSG_SHUTDOWN:
    case VW_MSG_STARTED:
      return true;
    case VW_MSG_CAPTION_SEGMENT: {
      const vw_caption_segment_t* p = (const vw_caption_segment_t*)payload;
      if (p->end_pts_us <= p->start_pts_us) return false;
      if (p->text_bytes > VW_MAX_TEXT_BYTES) return false;
      if (is_empty_or_whitespace(p->text_utf8, p->text_bytes)) return false;
      if (!is_valid_utf8(p->text_utf8, p->text_bytes)) return false;
      // No control characters except space and newline (handled strictly)
      for (size_t i = 0; i < p->text_bytes; i++) {
        uint8_t c = (uint8_t)p->text_utf8[i];
        if (c < 0x20 && c != '\n' && c != ' ') return false;
      }
      return true;
    }
    default:
      return false;
  }
}
