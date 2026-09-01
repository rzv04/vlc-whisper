// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

#ifndef VW_PROTOCOL_TYPES_H_
#define VW_PROTOCOL_TYPES_H_

// Wire format: [vw_frame_header_t (20 bytes)] [payload (payload_length bytes)]
//
//   Offset  0: uint32_t magic           = 0x564C4357 ('VLCW')
//   Offset  4: uint16_t major           = protocol major version
//   Offset  6: uint16_t type            = vw_message_type_t (determines payload struct)
//   Offset  8: uint32_t payload_length  = byte count of payload that follows
//   Offset 12: uint64_t sequence        = strictly increasing per transport direction
//   Offset 20: uint8_t payload[payload_length]  ← serialized vw_msg_xxx_t fields
//
// header.type selects which struct serializes/deserializes the payload bytes.
// Zero-payload message (VW_MSG_SHUTDOWN) has payload_length=0
// and no struct — the codec handles it as an empty payload.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VW_PROTOCOL_MAGIC 0x564C4357U  // 'VLCW'
#define VW_PROTOCOL_VERSION_MAJOR 1U
#define VW_PROTOCOL_VERSION_MINOR 5U
#define VW_CLIENT_VERSION "1.5.0"
#define VW_CLIENT_VERSION_LENGTH 5U
#define VW_WORKER_VERSION "1.5.0"
#define VW_WORKER_VERSION_LENGTH 5U
#define VW_MAX_PAYLOAD_BYTES (1048576U)  // 1 MB max frame payload
#define VW_MAX_ERROR_MSG_BYTES 256U      // Safe error message & version string limit
#define VW_MAX_MODEL_ID_BYTES 64U        // Model identifier string limit
#define VW_MAX_SOURCE_URL_BYTES 1024U    // Max source MRL / file path length
#define VW_AUTH_TOKEN_BYTES 32U          // Local IPC 32-byte secret authentication token
#define VW_SESSION_ID_BYTES 16U          // Local IPC session identifier size in bytes
#define VW_MAX_TEXT_BYTES 1024U          // Max caption text length in bytes (UTF-8)
// Filesystem path bound shared by plugin and worker (plugin passes --model via argv; both sides
// must agree). ≥ the OS max path length per platform so no valid configured path is rejected:
// Linux PATH_MAX is 4096 bytes; Windows long paths (\\?\ prefix) allow up to 32767 chars.
#ifdef _WIN32
#define VW_PATH_MAX_BYTES 32768U
#else
#define VW_PATH_MAX_BYTES 4096U
#endif

// Capability flags (bitfield). Optional minor-version features MUST be gated by a capability before a newer peer sends
// their message types to an older same-major peer.
#define VW_CAPABILITY_PCM_S16LE_16K_MONO (1U << 0)
#define VW_CAPABILITY_PARTIAL_SEGMENTS (1U << 1)
#define VW_CAPABILITY_SEEK_RESET (1U << 2)
#define VW_CAPABILITY_SOURCE_MODE (1U << 3)
#define VW_CAPABILITY_TRANSLATION (1U << 4)

// Source kind enum
typedef enum vw_source_kind { VW_SOURCE_LIVE_AUDIO = 0, VW_SOURCE_LOCAL_FILE = 1 } vw_source_kind_t;

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
  E_INTERNAL = 9,
  E_SOURCE_OPEN = 10
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
  VW_MSG_SHUTDOWN = 11,        // zero-payload: instruct worker to exit
  VW_MSG_STARTED = 12,         // worker confirms session started; carries uint8_t source_active
  VW_MSG_POSITION = 13,        // plugin sends media playback position and pacing updates
  VW_MSG_MODEL_CTRL = 14,      // plugin requests model download or abort by catalog id
  VW_MSG_MODEL_PROGRESS = 15,  // worker reports model download progress and stage
  VW_MSG_TRANSLATE_CTRL = 16   // plugin configures real-time subtitle translation parameters (capability-gated)
} vw_message_type_t;

typedef struct vw_frame_header {
  uint32_t magic;           // VW_PROTOCOL_MAGIC
  uint16_t major;           // VW_PROTOCOL_VERSION_MAJOR
  uint16_t type;            // vw_message_type_t, default to 2 bytes instead of enum 4
  uint32_t payload_length;  // payload byte count
  uint64_t sequence;        // strictly increasing per transport direction
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
  uint16_t source_url_len;
  char source_url[VW_MAX_SOURCE_URL_BYTES];
} vw_msg_start_t;

typedef struct vw_msg_started {
  uint8_t source_active;  // 1 if source file lookahead mode active; 0 if live streaming mode
} vw_msg_started_t;

// STARTED source_active values
#define VW_SOURCE_ACTIVE_INACTIVE 0U
#define VW_SOURCE_ACTIVE_ACTIVE 1U

#define VW_MSG_STARTED_PAYLOAD_BYTES 1U

// Position update flags (bitfield)
#define VW_POSITION_FLAG_SEEK (1U << 0)
#define VW_POSITION_FLAG_PAUSED (1U << 1)

typedef struct vw_msg_position {
  vw_session_id_t session_id;
  int64_t current_pts_us;
  int64_t input_time_us;
  float playback_rate;
  uint32_t flags;
} vw_msg_position_t;

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

// Control-message reason codes (vw_msg_control_t.reason), per docs/api-contracts.md.
#define VW_CTRL_REASON_USER_PAUSE 1U          // PAUSE: user paused playback
#define VW_CTRL_REASON_USER_RESUME 1U         // RESUME: user resumed playback
#define VW_CTRL_REASON_USER_STOP 1U           // STOP: user stopped the session
#define VW_CTRL_REASON_SEEK_DISCONTINUITY 2U  // STOP: seek or discontinuity — new session epoch
#define VW_CTRL_REASON_MEDIA_END 3U           // STOP: media ended

typedef struct vw_msg_status {
  vw_session_id_t session_id;
  uint32_t state;
  int64_t queued_audio_us;
  int64_t inference_us;
  int64_t dropped_audio_us;
  char resolved_backend[16];  // "gpu" | "cpu", NUL-padded
} vw_msg_status_t;
// VW_MODEL_ACTION_DOWNLOAD and VW_MODEL_ACTION_ABORT identify the requested operation in
// vw_msg_model_ctrl_t; download starts a transfer while abort cancels an in-flight download immediately.
#define VW_MODEL_ACTION_DOWNLOAD 1U
#define VW_MODEL_ACTION_ABORT 2U

// VW_MODEL_STAGE_* values track the lifecycle of a model download inside vw_msg_model_progress_t,
// ranging from idle through downloading, verifying, done, failed, and aborting states.
#define VW_MODEL_STAGE_IDLE 0U
#define VW_MODEL_STAGE_DOWNLOADING 1U
#define VW_MODEL_STAGE_VERIFYING 2U
#define VW_MODEL_STAGE_DONE 3U
#define VW_MODEL_STAGE_FAILED 4U
#define VW_MODEL_STAGE_ABORTING 5U

// VW_MSG_MODEL_CTRL_PAYLOAD_BYTES and VW_MSG_MODEL_PROGRESS_PAYLOAD_BYTES define the exact wire
// sizes of the fixed-field model control and progress frames, validating that 49 and 66 byte payloads are received.
#define VW_MSG_MODEL_CTRL_PAYLOAD_BYTES 49U
#define VW_MSG_MODEL_PROGRESS_PAYLOAD_BYTES 66U

// Plugin-to-worker request to download or abort a catalog model; carries session id, action code,
// and NUL-padded model identifier fixed field.
typedef struct vw_msg_model_ctrl {
  vw_session_id_t session_id;
  uint8_t action;
  char model_id[32];
} vw_msg_model_ctrl_t;

// Worker-to-plugin progress update for model download; includes session id, stage, percent, byte
// counters, and NUL-padded model identifier field, emitted at 1 Hz and on stage transitions.
typedef struct vw_msg_model_progress {
  vw_session_id_t session_id;
  uint8_t stage;
  uint8_t pct;
  uint64_t bytes_done;
  uint64_t bytes_total;
  char model_id[32];
} vw_msg_model_progress_t;

// VW_MSG_TRANSLATE_CTRL_PAYLOAD_BYTES defines the exact wire size of fixed-field translation control frames.
#define VW_MSG_TRANSLATE_CTRL_PAYLOAD_BYTES 50U

// Plugin-to-worker translation configuration update specifying enabled state, source/target languages, and display
// mode.
typedef struct vw_msg_translate_ctrl {
  vw_session_id_t session_id;
  uint8_t enabled;
  char source_lang[16];
  char target_lang[16];
  uint8_t mode;  // 0: translation only, 1: dual line (source + translation)
} vw_msg_translate_ctrl_t;

typedef struct vw_msg_error {
  vw_session_id_t session_id;
  uint32_t error_code;
  uint8_t recoverable;
  char message[VW_MAX_ERROR_MSG_BYTES];
} vw_msg_error_t;

// Segment structure containing start/end timestamps, transcribed text, and optional translated subtitle payload.
typedef struct vw_caption_segment {  // worker to plugin
  vw_session_id_t session_id;
  uint64_t segment_id;
  int64_t start_pts_us;
  int64_t end_pts_us;
  bool is_final;
  char* text_utf8;
  uint16_t text_bytes;
  char* translated_text_utf8;
  uint16_t translated_text_bytes;
  bool translation_attempted;
  uint32_t translation_latency_us;
  uint8_t translation_tier;
} vw_caption_segment_t;

// Wire size of vw_caption_segment_t's fixed fields (session 16 + id 8 + start 8 + end 8 + is_final 1 + text_bytes 2),
// used by emitters to size the encode buffer for a max-length caption segment.
#define VW_CAPTION_SEGMENT_FIXED_BYTES 43

#endif  // VW_PROTOCOL_TYPES_H_
