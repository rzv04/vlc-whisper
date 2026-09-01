#if defined(_WIN32) || defined(__MINGW32__)
#include <aclapi.h>
#include <sddl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "vw_ipc_transport.h"

// Fail-closed same-user pipe security: create a DACL that grants GENERIC_READ|GENERIC_WRITE
// only to the current user. On any failure the caller gets NULL and no pipe is created,
// so a permissive DACL is never used. Requires linking with advapi32 (MinGW: -ladvapi32).
static bool vw_pipe_create_sa(SECURITY_ATTRIBUTES* out_sa, SECURITY_DESCRIPTOR* out_sd, PACL* out_acl,
                              PTOKEN_USER* out_token_user, HANDLE* out_token) {
  if (!out_sa || !out_sd || !out_acl || !out_token_user || !out_token) return false;
  *out_acl = NULL;
  *out_token_user = NULL;
  *out_token = NULL;
  HANDLE token = NULL;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  DWORD len = 0;
  GetTokenInformation(token, TokenUser, NULL, 0, &len);
  if (len == 0) {
    CloseHandle(token);
    return false;
  }
  PTOKEN_USER tu = (PTOKEN_USER)malloc(len);
  if (!tu) {
    CloseHandle(token);
    return false;
  }
  if (!GetTokenInformation(token, TokenUser, tu, len, &len)) {
    free(tu);
    CloseHandle(token);
    return false;
  }
  PSID sid = tu->User.Sid;
  DWORD sid_len = GetLengthSid(sid);
  DWORD acl_size = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + sid_len;
  PACL dacl = (PACL)malloc(acl_size);
  if (!dacl) {
    free(tu);
    CloseHandle(token);
    return false;
  }
  if (!InitializeAcl(dacl, acl_size, ACL_REVISION)) {
    free(dacl);
    free(tu);
    CloseHandle(token);
    return false;
  }
  if (!AddAccessAllowedAce(dacl, ACL_REVISION, GENERIC_READ | GENERIC_WRITE, sid)) {
    free(dacl);
    free(tu);
    CloseHandle(token);
    return false;
  }
  if (!InitializeSecurityDescriptor(out_sd, SECURITY_DESCRIPTOR_REVISION)) {
    free(dacl);
    free(tu);
    CloseHandle(token);
    return false;
  }
  if (!SetSecurityDescriptorDacl(out_sd, TRUE, dacl, FALSE)) {
    free(dacl);
    free(tu);
    CloseHandle(token);
    return false;
  }
  out_sa->nLength = sizeof(SECURITY_ATTRIBUTES);
  out_sa->lpSecurityDescriptor = out_sd;
  out_sa->bInheritHandle = FALSE;
  *out_acl = dacl;
  *out_token_user = tu;
  *out_token = token;
  return true;
}

static void vw_pipe_free_sa(PACL acl, PTOKEN_USER tu, HANDLE token) {
  if (acl) free(acl);
  if (tu) free(tu);
  if (token) CloseHandle(token);
}

// Reap a pending overlapped I/O after cancellation. Must be called after CancelIoEx;
// waits for the kernel completion to be delivered and reaps it so the stack OVERLAPPED
// and event are not destroyed while the kernel still references them.
static void vw_pipe_reap_cancel(HANDLE pipe, OVERLAPPED* ov) {
  if (!pipe || pipe == INVALID_HANDLE_VALUE || !ov || !ov->hEvent) return;
  WaitForSingleObject(ov->hEvent, INFINITE);
  DWORD dummy = 0;
  GetOverlappedResult(pipe, ov, &dummy, FALSE);
}

vw_ipc_handle_t* vw_ipc_listen(const char* endpoint_name) {
  if (!endpoint_name || endpoint_name[0] == '\0') return NULL;
  SECURITY_ATTRIBUTES sa;
  SECURITY_DESCRIPTOR sd;
  PACL dacl = NULL;
  PTOKEN_USER tu = NULL;
  HANDLE token = NULL;
  if (!vw_pipe_create_sa(&sa, &sd, &dacl, &tu, &token)) {
    return NULL;
  }
  HANDLE pipe =
      CreateNamedPipeA(endpoint_name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                       PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 65536, 65536, 0, &sa);
  vw_pipe_free_sa(dacl, tu, token);
  if (pipe == INVALID_HANDLE_VALUE) return NULL;
  OVERLAPPED ov = {0};
  ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
  if (!ov.hEvent) {
    CloseHandle(pipe);
    return NULL;
  }
  BOOL connected = ConnectNamedPipe(pipe, &ov);
  if (!connected) {
    DWORD err = GetLastError();
    if (err == ERROR_IO_PENDING) {
      DWORD w = WaitForSingleObject(ov.hEvent, 10000);
      if (w == WAIT_OBJECT_0) {
        DWORD dummy = 0;
        connected = GetOverlappedResult(pipe, &ov, &dummy, FALSE);
      } else if (w == WAIT_TIMEOUT) {
        CancelIoEx(pipe, &ov);
        vw_pipe_reap_cancel(pipe, &ov);
        connected = FALSE;
      } else {
        CancelIoEx(pipe, &ov);
        vw_pipe_reap_cancel(pipe, &ov);
        connected = FALSE;
      }
    } else if (err == ERROR_PIPE_CONNECTED) {
      connected = TRUE;
    } else {
      connected = FALSE;
    }
  }
  CloseHandle(ov.hEvent);
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
      DWORD w = WaitForSingleObject(ov.hEvent, 3000);
      if (w == WAIT_OBJECT_0) {
        res = GetOverlappedResult(pipe, &ov, &bytes_written, FALSE);
      } else if (w == WAIT_TIMEOUT) {
        CancelIoEx(pipe, &ov);
        vw_pipe_reap_cancel(pipe, &ov);
        res = FALSE;
      } else {
        CancelIoEx(pipe, &ov);
        vw_pipe_reap_cancel(pipe, &ov);
        res = FALSE;
      }
    }
  }
  CloseHandle(ov.hEvent);
  return res && (bytes_written == size);
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
  bool fatal_wait = false;
  if (!res) {
    DWORD err = GetLastError();
    if (err == ERROR_IO_PENDING) {
      DWORD w = WaitForSingleObject(ov.hEvent, (DWORD)(((uint64_t)timeout_us + 999) / 1000));
      if (w == WAIT_OBJECT_0) {
        res = GetOverlappedResult(pipe, &ov, &bytes_read, FALSE);
      } else if (w == WAIT_TIMEOUT) {
        CancelIoEx(pipe, &ov);
        vw_pipe_reap_cancel(pipe, &ov);
        timed_out = true;
      } else {
        CancelIoEx(pipe, &ov);
        vw_pipe_reap_cancel(pipe, &ov);
        fatal_wait = true;
      }
    }
  }
  CloseHandle(ov.hEvent);

  if (timed_out) return VW_IPC_RECV_TIMEOUT;  // timeout — keep waiting
  if (fatal_wait) return VW_IPC_RECV_FATAL;
  if (!res || bytes_read == 0) return VW_IPC_RECV_FATAL;  // fatal: real error or EOF
  return (int32_t)bytes_read;
}

// Receives one transport chunk with the standard three-second timeout.
int32_t vw_ipc_receive(vw_ipc_handle_t* handle, void* buffer, size_t buffer_size) {
  return vw_ipc_receive_timeout(handle, buffer, buffer_size, 3000000U);
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
