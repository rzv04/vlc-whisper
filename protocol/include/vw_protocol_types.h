#ifndef VW_PROTOCOL_TYPES_H_
#define VW_PROTOCOL_TYPES_H_

// Wire format: [vw_frame_header_t (20 bytes)] [payload (payload_length bytes)]
//
//   Offset  0: uint32_t magic           = 0x564C4357 ('VLCW')
//   Offset  4: uint16_t major           = protocol major version
//   Offset  6: uint16_t type            = vw_message_type_t (determines payload struct)
//   Offset  8: uint32_t payload_length  = byte count of payload that follows
//   Offset 12: uint64_t sequence        = monotonic per-session counter
//   Offset 20: uint8_t payload[payload_length]  ← serialized vw_msg_xxx_t fields
//
// header.type selects which struct serializes/deserializes the payload bytes.
// Zero-payload messages (VW_MSG_SHUTDOWN, VW_MSG_STARTED) have payload_length=0
// and no struct — the codec handles them as empty payloads.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VW_PROTOCOL_MAGIC 0x564C4357U  // 'VLCW'
#define VW_PROTOCOL_VERSION_MAJOR 1U
#define VW_PROTOCOL_VERSION_MINOR 0U
#define VW_CLIENT_VERSION "1.0.0"
#define VW_CLIENT_VERSION_LENGTH 5U
#define VW_WORKER_VERSION "1.0.0"
#define VW_WORKER_VERSION_LENGTH 5U
#define VW_MAX_PAYLOAD_BYTES (1048576U)  // 1 MB max frame payload
#define VW_MAX_ERROR_MSG_BYTES 256U      // Safe error message & version string limit
#define VW_MAX_MODEL_ID_BYTES 64U        // Model identifier string limit
#define VW_AUTH_TOKEN_BYTES 32U          // Local IPC 32-byte secret authentication token
#define VW_SESSION_ID_BYTES 16U          // Local IPC session identifier size in bytes
#define VW_MAX_TEXT_BYTES 1024U          // Max caption text length in bytes (UTF-8)
#define VW_PATH_MAX_BYTES 4096U          // Max filesystem path bytes (Linux PATH_MAX; plugin→worker argv contract)

// Capability flags (bitfield)
#define VW_CAPABILITY_PCM_S16LE_16K_MONO (1U << 0)
#define VW_CAPABILITY_PARTIAL_SEGMENTS (1U << 1)
#define VW_CAPABILITY_SEEK_RESET (1U << 2)

// Source kind enum
typedef enum vw_source_kind { VW_SOURCE_LOCAL_FILE = 1 } vw_source_kind_t;

// Error codes for VW_MSG_ERROR frames
typedef enum vw_error_code {
  E_PROTOCOL_VERSION = 1,
  E_AUTH = 2,
  E_MODEL_MISSING = 3,
  E_MODEL_INVALID = 4,
  E_AUDIO_FORMAT = 5,
  E_BACKPRESSURE = 6,
  E_DISCONTINUITY = 7,
  E_WORKER_CRASH = 8,
  E_INTERNAL = 9
} vw_error_code_t;

// Binary frame header (20 bytes packed on wire)
#pragma pack(push, 1)

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
  VW_MSG_SHUTDOWN = 11,  // zero-payload: instruct worker to exit
  VW_MSG_STARTED = 12    // zero-payload: worker confirms session started
} vw_message_type_t;

typedef struct vw_frame_header {
  uint32_t magic;           // VW_PROTOCOL_MAGIC
  uint16_t major;           // VW_PROTOCOL_VERSION_MAJOR
  uint16_t type;            // vw_message_type_t, default to 2 bytes instead of enum 4
  uint32_t payload_length;  // payload byte count
  uint64_t sequence;        // monotonic sequence counter per session
} vw_frame_header_t;
#pragma pack(pop)

typedef struct vw_session_id {
  uint8_t bytes[VW_SESSION_ID_BYTES];
} vw_session_id_t;

// Payload Structs — each serialized as the payload bytes following vw_frame_header_t.
// header.type determines which struct layout applies. String fields are wire-encoded
// as u16 byte_length followed by bytes (not NUL-terminated).

typedef struct vw_msg_hello {
  uint16_t min_major;
  uint16_t max_major;
  uint8_t auth_token[VW_AUTH_TOKEN_BYTES];
  uint16_t client_version_length;
  char* client_version;
} vw_msg_hello_t;

typedef struct vw_msg_hello_ack {
  uint16_t selected_major;
  uint16_t selected_minor;
  uint32_t capability_flags;
  uint16_t worker_version_length;
  char* worker_version;
} vw_msg_hello_ack_t;

typedef struct vw_msg_start {
  vw_session_id_t session_id;
  int64_t timeline_origin_pts_us;
  uint32_t sample_rate;
  uint16_t channels;
  uint16_t sample_format;
  char model_id[VW_MAX_MODEL_ID_BYTES];
  char language[16];
  uint16_t source_kind;
} vw_msg_start_t;

typedef struct vw_msg_audio {  // plugin to worker
  vw_session_id_t session_id;
  int64_t start_pts_us;
  int64_t duration_us;
  uint32_t pcm_bytes;  // duration_us * 16000 / 1_000_000 * 2
  const uint8_t* pcm_data;
} vw_msg_audio_t;

typedef struct vw_msg_control {
  vw_session_id_t session_id;
  uint16_t reason;
} vw_msg_control_t;

typedef struct vw_msg_status {
  vw_session_id_t session_id;
  uint32_t state;
  int64_t queued_audio_us;
  int64_t inference_us;
  int64_t dropped_audio_us;
} vw_msg_status_t;

typedef struct vw_msg_error {
  vw_session_id_t session_id;
  uint32_t error_code;
  uint8_t recoverable;
  char message[VW_MAX_ERROR_MSG_BYTES];
} vw_msg_error_t;

// Segment structure containing start/end timestamps and text
typedef struct vw_caption_segment {  // worker to plugin
  vw_session_id_t session_id;
  uint64_t segment_id;
  int64_t start_pts_us;
  int64_t end_pts_us;
  bool is_final;
  char* text_utf8;
  uint16_t text_bytes;
} vw_caption_segment_t;

// Wire size of vw_caption_segment_t's fixed fields (session 16 + id 8 + start 8 + end 8 + is_final 1 + text_bytes 2),
// used by emitters to size the encode buffer for a max-length caption segment.
#define VW_CAPTION_SEGMENT_FIXED_BYTES 43

#endif  // VW_PROTOCOL_TYPES_H_
