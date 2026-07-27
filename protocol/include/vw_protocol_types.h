#ifndef VW_PROTOCOL_TYPES_H_
#define VW_PROTOCOL_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VW_PROTOCOL_MAGIC 0x564C4357U  // 'VLCW'
#define VW_PROTOCOL_VERSION_MAJOR 1U
#define VW_PROTOCOL_VERSION_MINOR 0U
#define VW_MAX_PAYLOAD_BYTES (1048576U)  // 1 MB max frame payload

// Binary frame header (20 bytes packed on wire)
#pragma pack(push, 1)
typedef struct vw_frame_header {
  uint32_t magic;           // VW_PROTOCOL_MAGIC
  uint16_t major;           // VW_PROTOCOL_VERSION_MAJOR
  uint16_t type;            // vw_message_type_t
  uint32_t payload_length;  // payload byte count
  uint64_t sequence;        // monotonic sequence counter per session
} vw_frame_header_t;
#pragma pack(pop)

typedef enum vw_message_type {
  VW_MSG_HELLO = 1,
  VW_MSG_HELLO_ACK = 2,
  VW_MSG_START_SESSION = 3,
  VW_MSG_AUDIO_PCM = 4,
  VW_MSG_PAUSE = 5,
  VW_MSG_RESUME = 6,
  VW_MSG_STOP_SESSION = 7,
  VW_MSG_CAPTION_SEGMENT = 8,
  VW_MSG_STATUS = 9,
  VW_MSG_ERROR = 10,
  VW_MSG_SHUTDOWN = 11
} vw_message_type_t;

typedef struct vw_session_id {
  uint8_t bytes[16];
} vw_session_id_t;

// Segment structure containing start/end timestamps, text, and flags
// A segment contains a single timestamped utterance.
typedef struct vw_caption_segment {
  uint64_t segment_id;
  int64_t start_pts_us;
  int64_t end_pts_us;
  bool is_final;
  char* text_utf8;
  uint16_t text_bytes;
  uint8_t flags;  // bit0: final, bit1: replace

} vw_caption_segment_t;

#endif  // VW_PROTOCOL_TYPES_H_
