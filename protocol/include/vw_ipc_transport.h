#ifndef VW_IPC_TRANSPORT_H_
#define VW_IPC_TRANSPORT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct vw_ipc_handle vw_ipc_handle_t;

// Platform transport abstraction prototypes
vw_ipc_handle_t *vw_ipc_listen(const char *endpoint_name, const uint8_t token[32]);
vw_ipc_handle_t *vw_ipc_connect(const char *endpoint_name, const uint8_t token[32]);
bool vw_ipc_send(vw_ipc_handle_t *handle, const void *data, size_t size);
int32_t vw_ipc_receive(vw_ipc_handle_t *handle, void *buffer, size_t buffer_size);
void vw_ipc_close(vw_ipc_handle_t *handle);

#endif // VW_IPC_TRANSPORT_H_
