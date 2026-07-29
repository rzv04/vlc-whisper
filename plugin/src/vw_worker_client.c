#include "vw_worker_client.h"

#include <stdlib.h>

#include "vw_ipc_transport.h"

vw_worker_client_t *vw_worker_client_launch_and_connect(const char *executable_path, const char *endpoint_name,
                                                        const uint8_t token[32]) {
  (void)executable_path;
  (void)token;

  vw_ipc_handle_t *ipc = vw_ipc_connect(endpoint_name);
  if (!ipc) {
    return NULL;
  }

  vw_worker_client_t *client = (vw_worker_client_t *)calloc(1, sizeof(vw_worker_client_t));
  if (!client) {
    vw_ipc_close(ipc);
    return NULL;
  }

  client->pipe_handle = ipc;
  return client;
}

void vw_worker_client_disconnect(vw_worker_client_t *client) {
  if (client) {
    if (client->pipe_handle) {
      vw_ipc_close((vw_ipc_handle_t *)client->pipe_handle);
    }
    free(client);
  }
}
