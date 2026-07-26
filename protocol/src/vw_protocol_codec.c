#include "vw_protocol_codec.h"

#include <string.h>

bool vw_protocol_encode_header(const vw_frame_header_t *header, uint8_t *buffer, size_t buffer_size) {
  if (!header || !buffer || buffer_size < sizeof(vw_frame_header_t)) {
    return false;
  }
  memcpy(buffer, header, sizeof(vw_frame_header_t));
  return true;
}

bool vw_protocol_decode_header(const uint8_t *buffer, size_t buffer_size, vw_frame_header_t *header) {
  if (!buffer || !header || buffer_size < sizeof(vw_frame_header_t)) {
    return false;
  }
  memcpy(header, buffer, sizeof(vw_frame_header_t));
  return vw_protocol_validate_header(header);
}
