#ifndef VW_WORKER_CLIENT_H_
#define VW_WORKER_CLIENT_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct vw_worker_client vw_worker_client_t;

vw_worker_client_t *vw_worker_client_launch_and_connect(const char *executable_path);
void vw_worker_client_disconnect(vw_worker_client_t *client);

#endif // VW_WORKER_CLIENT_H_
