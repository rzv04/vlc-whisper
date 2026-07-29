#ifndef VW_IPC_TRANSPORT_H_
#define VW_IPC_TRANSPORT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct vw_ipc_handle {
  void* pipe_handle;
} vw_ipc_handle_t;

// Platform transport abstraction prototypes

// Listens on local IPC endpoint (Named Pipe on Win32, Unix domain socket on POSIX).
// Waits up to 10 seconds for incoming connection before timing out and returning NULL.
vw_ipc_handle_t* vw_ipc_listen(const char* endpoint_name);

// Connects to local IPC endpoint.
vw_ipc_handle_t* vw_ipc_connect(const char* endpoint_name);

// Sends raw frame bytes over transport handle (3-second send timeout).
// Returns true on success, false on error or timeout.
bool vw_ipc_send(vw_ipc_handle_t* handle, const void* data, size_t size);

// Receives raw frame bytes over transport handle (3-second receive timeout).
// Returns > 0: Number of bytes read into buffer.
// Returns   0: Read timeout (no data available within 3s; connection remains open/valid for pause).
// Returns  -1: Fatal I/O error or peer closed connection (EOF / broken pipe).
int32_t vw_ipc_receive(vw_ipc_handle_t* handle, void* buffer, size_t buffer_size);

// Closes IPC transport handle and frees resources.
void vw_ipc_close(vw_ipc_handle_t* handle);

#endif  // VW_IPC_TRANSPORT_H_
