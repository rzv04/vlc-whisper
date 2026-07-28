#include "vw_worker_client.h"

#include <stdlib.h>



vw_worker_client_t *vw_worker_client_launch_and_connect(const char *executable_path) {
  (void)executable_path;
  return NULL;
}

void vw_worker_client_disconnect(vw_worker_client_t *client) {
  if (client) {
    free(client);
  }
}
