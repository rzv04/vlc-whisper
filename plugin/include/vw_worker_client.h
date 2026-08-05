#ifndef VW_WORKER_CLIENT_H_
#define VW_WORKER_CLIENT_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

#define VW_WORKER_CLIENT_RETRY_COUNT 40

typedef struct vw_worker_client {
  void* pipe_handle;
} vw_worker_client_t;

vw_worker_client_t* vw_worker_client_launch_and_connect(const char* executable_path, const char* endpoint_name,
                                                        const uint8_t auth_token[VW_AUTH_TOKEN_BYTES]);
void vw_worker_client_disconnect(vw_worker_client_t* client);

#endif  // VW_WORKER_CLIENT_H_
