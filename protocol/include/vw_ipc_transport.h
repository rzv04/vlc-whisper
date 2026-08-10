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

// vw_ipc_receive() return codes.
#define VW_IPC_RECV_OK (1)        // complete frame/message received (whole-frame APIs: receive_all, receive_frame)
#define VW_IPC_RECV_TIMEOUT (-1)  // 3s read timeout — connection open, retry/keep waiting
#define VW_IPC_RECV_FATAL (-2)    // fatal I/O error or peer closed (EOF / broken pipe) — abort

// Receives raw frame bytes over transport handle (3-second receive timeout).
// Returns  > 0: Number of bytes read into buffer.
// Returns VW_IPC_RECV_TIMEOUT (-1): Read timeout (no data available within 3s;
//   connection remains open/valid, e.g. during video pause — callers retry/keep waiting).
// Returns VW_IPC_RECV_FATAL (-2): Fatal I/O error or peer closed connection
//   (EOF / broken pipe); the handle must be treated as dead and the caller abort.
int32_t vw_ipc_receive(vw_ipc_handle_t* handle, void* buffer, size_t buffer_size);

// Receives raw frame bytes over transport handle with a specific timeout in microseconds.
// Returns > 0 on success, VW_IPC_RECV_TIMEOUT on timeout, VW_IPC_RECV_FATAL on error.
int32_t vw_ipc_receive_timeout(vw_ipc_handle_t* handle, void* buffer, size_t buffer_size, uint32_t timeout_us);

// Closes IPC transport handle and frees resources.
void vw_ipc_close(vw_ipc_handle_t* handle);

#endif  // VW_IPC_TRANSPORT_H_
