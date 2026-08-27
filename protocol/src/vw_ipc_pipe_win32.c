#if defined(_WIN32) || defined(__MINGW32__)
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <windows.h>

#include "vw_ipc_transport.h"

vw_ipc_handle_t* vw_ipc_listen(const char* endpoint_name) {
  HANDLE pipe = CreateNamedPipeA(endpoint_name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                 PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 65536, 65536, 0, NULL);

  if (pipe == INVALID_HANDLE_VALUE) return NULL;

  OVERLAPPED ov = {0};
  ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
  BOOL connected = ConnectNamedPipe(pipe, &ov);
  if (!connected) {
    DWORD err = GetLastError();
    if (err == ERROR_IO_PENDING) {
      if (WaitForSingleObject(ov.hEvent, 10000) == WAIT_OBJECT_0) {
        DWORD dummy;
        connected = GetOverlappedResult(pipe, &ov, &dummy, FALSE);
      } else {
        CancelIo(pipe);
      }
    } else if (err == ERROR_PIPE_CONNECTED) {
      connected = TRUE;
    }
  }
  if (ov.hEvent) CloseHandle(ov.hEvent);

  if (!connected) {
    CloseHandle(pipe);
    return NULL;
  }

  vw_ipc_handle_t* handle = (vw_ipc_handle_t*)calloc(1, sizeof(vw_ipc_handle_t));
  if (!handle) {
    CloseHandle(pipe);
    return NULL;
  }
  handle->pipe_handle = pipe;
  return handle;
}

vw_ipc_handle_t* vw_ipc_connect(const char* endpoint_name) {
  HANDLE pipe =
      CreateFileA(endpoint_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

  if (pipe == INVALID_HANDLE_VALUE) return NULL;

  DWORD mode = PIPE_READMODE_MESSAGE;
  if (!SetNamedPipeHandleState(pipe, &mode, NULL, NULL)) {
    CloseHandle(pipe);
    return NULL;
  }

  vw_ipc_handle_t* handle = (vw_ipc_handle_t*)calloc(1, sizeof(vw_ipc_handle_t));
  if (!handle) {
    CloseHandle(pipe);
    return NULL;
  }
  handle->pipe_handle = pipe;
  return handle;
}

bool vw_ipc_send(vw_ipc_handle_t* handle, const void* data, size_t size) {
  if (!handle || !handle->pipe_handle || handle->pipe_handle == INVALID_HANDLE_VALUE) return false;
  HANDLE pipe = (HANDLE)handle->pipe_handle;

  OVERLAPPED ov = {0};
  ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
  if (!ov.hEvent) {
    return false;
  }
  DWORD bytes_written = 0;
  BOOL res = WriteFile(pipe, data, (DWORD)size, &bytes_written, &ov);
  if (!res) {
    DWORD err = GetLastError();
    if (err == ERROR_IO_PENDING) {
      if (WaitForSingleObject(ov.hEvent, 3000) == WAIT_OBJECT_0) {
        res = GetOverlappedResult(pipe, &ov, &bytes_written, FALSE);
      } else {
        CancelIo(pipe);
        res = FALSE;
      }
    }
  }
  CloseHandle(ov.hEvent);
  return res && (bytes_written == size);
}

int32_t vw_ipc_receive(vw_ipc_handle_t* handle, void* buffer, size_t buffer_size) {
  return vw_ipc_receive_timeout(handle, buffer, buffer_size, 3000000);
}

int32_t vw_ipc_receive_timeout(vw_ipc_handle_t* handle, void* buffer, size_t buffer_size, uint32_t timeout_us) {
  if (!handle || !handle->pipe_handle || handle->pipe_handle == INVALID_HANDLE_VALUE) return VW_IPC_RECV_FATAL;
  HANDLE pipe = (HANDLE)handle->pipe_handle;

  OVERLAPPED ov = {0};
  ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
  if (!ov.hEvent) return VW_IPC_RECV_FATAL;  // fatal: cannot wait for completion

  DWORD bytes_read = 0;
  BOOL res = ReadFile(pipe, buffer, (DWORD)buffer_size, &bytes_read, &ov);
  bool timed_out = false;
  if (!res) {
    DWORD err = GetLastError();
    if (err == ERROR_IO_PENDING) {
      if (WaitForSingleObject(ov.hEvent, (DWORD)(((uint64_t)timeout_us + 999) / 1000)) == WAIT_OBJECT_0) {
        res = GetOverlappedResult(pipe, &ov, &bytes_read, FALSE);
      } else {
        CancelIo(pipe);
        timed_out = true;
      }
    }
  }
  CloseHandle(ov.hEvent);

  if (timed_out) return VW_IPC_RECV_TIMEOUT;              // timeout — keep waiting
  if (!res || bytes_read == 0) return VW_IPC_RECV_FATAL;  // fatal: real error or EOF
  return (int32_t)bytes_read;
}

void vw_ipc_close(vw_ipc_handle_t* handle) {
  if (handle) {
    if (handle->pipe_handle && handle->pipe_handle != INVALID_HANDLE_VALUE) {
      CloseHandle((HANDLE)handle->pipe_handle);
    }
    free(handle);
  }
}

#endif
