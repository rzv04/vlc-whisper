#ifndef VW_PROTOCOL_CODEC_H_
#define VW_PROTOCOL_CODEC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vw_protocol_types.h"
//////////////////////////////////
// Codec & validation function prototypes
//////////////////////////////////

bool vw_protocol_validate_header(const vw_frame_header_t* header);  // implemented in vw_protocol_validate.c
// Encodes a vw_frame_header_t into a byte buffer. Returns true on success, false on failure (e.g., buffer too small).
bool vw_protocol_encode_header(const vw_frame_header_t* header, uint8_t* buffer, size_t buffer_size);
// Decodes a byte buffer into a vw_frame_header_t. Returns true on success, false on failure (e.g., buffer too small or
// invalid header).
bool vw_protocol_decode_header(const uint8_t* buffer, size_t buffer_size, vw_frame_header_t* header);

bool vw_protocol_encode_payload(vw_message_type_t type, const void* payload, uint8_t* buffer, size_t buffer_size,
                                size_t* out_written);
bool vw_protocol_decode_payload(vw_message_type_t type, const uint8_t* buffer, size_t buffer_size, void* out_payload);
// Validate the payload struct for a given message type. Returns true if valid, false otherwise.
bool vw_protocol_validate_payload(vw_message_type_t type, const void* payload);

#endif  // VW_PROTOCOL_CODEC_H_
