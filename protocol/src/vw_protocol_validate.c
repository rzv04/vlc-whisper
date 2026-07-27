#include "vw_protocol_codec.h"

bool vw_protocol_validate_header(const vw_frame_header_t* header) {
  if (!header) {
    return false;
  }
  if (header->magic != VW_PROTOCOL_MAGIC) {
    return false;
  }
  if (header->major != VW_PROTOCOL_VERSION_MAJOR) {
    return false;
  }
  if (header->payload_length > VW_MAX_PAYLOAD_BYTES) {
    return false;
  }
  return true;
}
