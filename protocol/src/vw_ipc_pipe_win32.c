#include <stdlib.h>

#include "vw_ipc_transport.h"

#if defined(_WIN32) || defined(__MINGW32__)
// Windows named pipe implementation stubs
vw_ipc_handle_t* vw_ipc_listen(const char* endpoint_name, const uint8_t token[32]) {
  (void)endpoint_name;
  (void)token;
  return NULL;
}

vw_ipc_handle_t* vw_ipc_connect(const char* endpoint_name, const uint8_t token[32]) {
  (void)endpoint_name;
  (void)token;
  return NULL;
}

bool vw_ipc_send(vw_ipc_handle_t* handle, const void* data, size_t size) {
  (void)handle;
  (void)data;
  (void)size;
  return false;
}

int32_t vw_ipc_receive(vw_ipc_handle_t* handle, void* buffer, size_t buffer_size) {
  (void)handle;
  (void)buffer;
  (void)buffer_size;
  return -1;
}

void vw_ipc_close(vw_ipc_handle_t* handle) {
  if (handle) {
    free(handle);
  }
}
#else
// Non-Windows fallback stubs
vw_ipc_handle_t* vw_ipc_listen(const char* endpoint_name, const uint8_t token[32]) {
  (void)endpoint_name;
  (void)token;
  return NULL;
}

vw_ipc_handle_t* vw_ipc_connect(const char* endpoint_name, const uint8_t token[32]) {
  (void)endpoint_name;
  (void)token;
  return NULL;
}

bool vw_ipc_send(vw_ipc_handle_t* handle, const void* data, size_t size) {
  (void)handle;
  (void)data;
  (void)size;
  return false;
}

int32_t vw_ipc_receive(vw_ipc_handle_t* handle, void* buffer, size_t buffer_size) {
  (void)handle;
  (void)buffer;
  (void)buffer_size;
  return -1;
}

void vw_ipc_close(vw_ipc_handle_t* handle) {
  if (handle) {
    free(handle);
  }
}
#endif
