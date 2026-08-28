#ifndef VW_WORKER_CLIENT_H_
#define VW_WORKER_CLIENT_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_audio_capture.h"
#include "vw_ipc_transport.h"
#include "vw_platform.h"
#include "vw_protocol_types.h"

#define VW_WORKER_CLIENT_RETRY_COUNT 40
#define VW_HANDSHAKE_TIMEOUT_US (5 * 1000 * 1000)  // 5s total budget for the HELLO/HELLO_ACK handshake reads

typedef struct vw_worker_client {
  void* pipe_handle;
  vw_process_t worker_process;
  uint8_t session_id[16];
  uint32_t sequence;
  bool session_active;
  bool worker_source_active;
  uint32_t worker_capabilities;
  uint16_t worker_protocol_minor;
} vw_worker_client_t;

// Returns true if the worker confirmed that look-ahead source file decoding mode is active for the current session via
// the STARTED frame payload, allowing the caller to gate live PCM audio capture and IPC streaming.
bool vw_worker_client_is_source_active(const vw_worker_client_t* client);

// Spawns worker process if executable_path is non-NULL, connects over IPC, and performs HELLO/HELLO_ACK handshake.
// model_path (may be NULL) is appended as --model <path> to the worker argv; NULL omits the flag entirely.
vw_worker_client_t* vw_worker_client_launch_and_connect(const char* executable_path, const char* endpoint_name,
                                                        const uint8_t auth_token[VW_AUTH_TOKEN_BYTES],
                                                        const char* model_path);

// Extended launch forwards backend, language, threads, logging, gpu-device, and model-directory flags to worker argv;
// NULL or empty values are defaulted or omitted automatically, including model_dir when absent.
vw_worker_client_t* vw_worker_client_launch_and_connect_ex(const char* executable_path, const char* endpoint_name,
                                                           const uint8_t auth_token[VW_AUTH_TOKEN_BYTES],
                                                           const char* model_path, const char* backend,
                                                           const char* language, int n_threads, int gpu_device,
                                                           const char* model_dir, bool logging_enabled);

// Starts a new captioning session by transmitting a START frame with media origin and optional source URL over
// IPC, waiting for confirmation from worker.
bool vw_worker_client_start_session(vw_worker_client_t* client, int64_t timeline_origin_pts_us, const char* model_id,
                                    const char* source_url);

// Encodes and transmits a playback position and pacing update over IPC to throttle look-ahead worker decoding,
// adjusting buffer horizons and handling seeks.
bool vw_worker_client_send_position(vw_worker_client_t* client, int64_t current_pts_us, int64_t input_time_us,
                                    float playback_rate, uint32_t flags);

// Encodes and sends a PCM audio chunk over the IPC pipe to the worker during an active caption session.
bool vw_worker_client_send_audio(vw_worker_client_t* client, const vw_audio_chunk_t* chunk);

// Sends a worker-scoped MODEL_CTRL request over authenticated IPC, allowing download or abort with a zero session ID
// before caption START succeeds.
bool vw_worker_client_send_model_ctrl(vw_worker_client_t* client, uint8_t action, const char* model_id);

// Sends a TRANSLATE_CTRL configuration update only when the negotiated worker advertises translation support. A
// translation-disabled update is a successful no-op for older same-major workers; enabling returns false without
// damaging the transport when the capability is absent.
bool vw_worker_client_send_translate_ctrl(vw_worker_client_t* client, bool enabled, const char* source_lang,
                                          const char* target_lang, uint8_t mode);

// Sends a STOP control frame over IPC to request the worker to stop processing the current caption session.
// The 'reason' argument can include in the future, SEEK_DISCONTINUITY.
// If either frame write fails, the transport is dropped: the client's pipe becomes unusable and a new client
// must be created (the worker may be mid-frame and the stream can never be reframed).
void vw_worker_client_stop_session(vw_worker_client_t* client, uint16_t reason);

// Sends a PAUSE control frame (USER_PAUSE) over IPC to suspend transcription while playback is paused.
// The session stays active so audio forwarding can resume; drops the transport on write failure.
void vw_worker_client_pause_session(vw_worker_client_t* client);

// Sends a RESUME control frame (USER_RESUME) over IPC to continue transcription after PAUSE.
// The session stays active; drops the transport on write failure, same fail-closed policy as STOP.
void vw_worker_client_resume_session(vw_worker_client_t* client);

// Sends a SHUTDOWN frame over IPC instructing the worker process to cleanly terminate its main event loop.
void vw_worker_client_shutdown(vw_worker_client_t* client);

// Closes IPC connection, frees client resources, and waits up to 5s for spawned worker process to cleanly exit.
void vw_worker_client_disconnect(vw_worker_client_t* client);

// A worker-to-plugin frame decoded by vw_worker_client_receive_frame. Exactly one field is valid
// depending on `type`; segment.text_utf8 always points into text_buf (owned storage).
typedef struct vw_worker_recv {
  vw_message_type_t type;                  // VW_MSG_CAPTION_SEGMENT | VW_MSG_STATUS | VW_MSG_ERROR | MODEL_PROGRESS
  vw_caption_segment_t segment;            // valid when type == VW_MSG_CAPTION_SEGMENT; text_utf8 points into text_buf
  vw_msg_status_t status;                  // valid when type == VW_MSG_STATUS
  vw_msg_error_t error;                    // valid when type == VW_MSG_ERROR
  vw_msg_model_progress_t progress;        // valid when type == VW_MSG_MODEL_PROGRESS
  char text_buf[VW_MAX_TEXT_BYTES];        // storage that owns segment.text_utf8 (NUL-terminated)
  char trans_text_buf[VW_MAX_TEXT_BYTES];  // storage that owns segment.translated_text_utf8 (NUL-terminated)
} vw_worker_recv_t;

// Reads one worker-to-plugin frame. Returns VW_IPC_RECV_OK (1) = frame decoded into out,
// VW_IPC_RECV_TIMEOUT (-1) = deadline expired at a frame boundary (no frame arrived; connection
// intact, keep polling), VW_IPC_RECV_FATAL (-2) = transport dead or desynced (client must not be
// used again). Frames of any other type are drained (payload consumed) and skipped within the same
// timeout budget. Caller path is zero-heap: out->text_buf owns segment text. Call from the sender
// thread only — the client is not thread-safe and the receiver must not race senders.
int vw_worker_client_receive_frame(vw_worker_client_t* client, uint32_t timeout_us, vw_worker_recv_t* out);

#endif  // VW_WORKER_CLIENT_H_
