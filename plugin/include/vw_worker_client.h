#ifndef VW_WORKER_CLIENT_H_
#define VW_WORKER_CLIENT_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_audio_capture.h"
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
} vw_worker_client_t;

// Spawns worker process if executable_path is non-NULL, connects over IPC, and performs HELLO/HELLO_ACK handshake.
vw_worker_client_t* vw_worker_client_launch_and_connect(const char* executable_path, const char* endpoint_name,
                                                        const uint8_t auth_token[VW_AUTH_TOKEN_BYTES]);

// Starts a new captioning session by sending a START frame to the worker and waiting up to 5s for STARTED confirmation.
bool vw_worker_client_start_session(vw_worker_client_t* client, int64_t timeline_origin_pts_us, const char* model_id);

// Encodes and sends a PCM audio chunk over the IPC pipe to the worker during an active caption session.
bool vw_worker_client_send_audio(vw_worker_client_t* client, const vw_audio_chunk_t* chunk);

// Sends a STOP control frame over IPC to request the worker to stop processing the current caption session.
// The 'reason' argument can include in the future, SEEK_DISCONTINUITY
void vw_worker_client_stop_session(vw_worker_client_t* client, uint16_t reason);

// Sends a SHUTDOWN frame over IPC instructing the worker process to cleanly terminate its main event loop.
void vw_worker_client_shutdown(vw_worker_client_t* client);

// Closes IPC connection, frees client resources, and waits up to 5s for spawned worker process to cleanly exit.
void vw_worker_client_disconnect(vw_worker_client_t* client);

#endif  // VW_WORKER_CLIENT_H_
