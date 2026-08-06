#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "vw_ipc_transport.h"

vw_ipc_handle_t* vw_ipc_listen(const char* endpoint_name) {
  int server_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (server_fd < 0) return NULL;

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, endpoint_name, sizeof(addr.sun_path) - 1);

  // Unlink the socket file if it already exists to avoid bind errors
  unlink(endpoint_name);

  if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(server_fd);
    return NULL;
  }

  if (listen(server_fd, 1) < 0) {
    close(server_fd);
    return NULL;
  }

  struct pollfd pfd = {.fd = server_fd, .events = POLLIN};
  if (poll(&pfd, 1, 10000) <= 0) {
    close(server_fd);
    return NULL;
  }

  int client_fd = accept(server_fd, NULL, NULL);
  close(server_fd);  // We only accept one connection
  if (client_fd < 0) {
    return NULL;
  }

  struct timeval tv;
  tv.tv_sec = 3;
  tv.tv_usec = 0;
  // Set 3 second timeout for send/receive operations
  setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  vw_ipc_handle_t* handle = (vw_ipc_handle_t*)calloc(1, sizeof(vw_ipc_handle_t));
  if (!handle) {
    close(client_fd);
    return NULL;
  }
  handle->pipe_handle = (void*)(intptr_t)client_fd;
  return handle;
}

vw_ipc_handle_t* vw_ipc_connect(const char* endpoint_name) {
  int client_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (client_fd < 0) return NULL;

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, endpoint_name, sizeof(addr.sun_path) - 1);

  if (connect(client_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(client_fd);
    return NULL;
  }

  struct timeval tv;
  tv.tv_sec = 3;
  tv.tv_usec = 0;
  setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  vw_ipc_handle_t* handle = (vw_ipc_handle_t*)calloc(1, sizeof(vw_ipc_handle_t));
  if (!handle) {
    close(client_fd);
    return NULL;
  }
  handle->pipe_handle = (void*)(intptr_t)client_fd;
  return handle;
}

bool vw_ipc_send(vw_ipc_handle_t* handle, const void* data, size_t size) {
  if (!handle) return false;
  int fd = (int)(intptr_t)handle->pipe_handle;
  ssize_t bytes = send(fd, data, size, 0);
  return bytes == (ssize_t)size;
}

int32_t vw_ipc_receive(vw_ipc_handle_t* handle, void* buffer, size_t buffer_size) {
  if (!handle) return VW_IPC_RECV_FATAL;  // fatal: invalid handle
  int fd = (int)(intptr_t)handle->pipe_handle;
  ssize_t bytes = recv(fd, buffer, buffer_size, 0);
  if (bytes > 0) return (int32_t)bytes;
  if (bytes == 0) return VW_IPC_RECV_FATAL;  // EOF — peer closed connection (fatal)
  // bytes < 0: check for timeout vs real error
  if (errno == EAGAIN || errno == EWOULDBLOCK) return VW_IPC_RECV_TIMEOUT;  // timeout — keep waiting
  return VW_IPC_RECV_FATAL;                                                 // real error (fatal)
}

void vw_ipc_close(vw_ipc_handle_t* handle) {
  if (handle) {
    int fd = (int)(intptr_t)handle->pipe_handle;
    if (fd >= 0) close(fd);
    free(handle);
  }
}
#endif
