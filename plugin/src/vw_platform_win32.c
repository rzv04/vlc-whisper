#if defined(_WIN32) || defined(__MINGW32__)
// clang-format off
#include <windows.h>
#include <bcrypt.h>
#include <stdlib.h>
#include <string.h>
// clang-format on

#ifndef BCRYPT_RNG_ALG_HANDLE
#define BCRYPT_RNG_ALG_HANDLE ((BCRYPT_ALG_HANDLE)0x00000081)
#endif

#include "vw_platform.h"

bool vw_platform_get_random_bytes(void* buffer, size_t size) {
  if (!buffer || size == 0) {
    return false;
  }
  // memset(buffer, 0x42, size);

  // generate a random size-byte (32) token for authentication
  NTSTATUS status = BCryptGenRandom(NULL, (PUCHAR)buffer, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  if (status != CMC_STATUS_SUCCESS) {
    return false;
  }

  return true;
}

int64_t vw_platform_get_time_us(void) {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  // Convert to microseconds since Unix epoch (January 1, 1970)
  return (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10);
}

bool vw_platform_spawn_process(const char* executable_path, const char* const argv[], vw_process_t* out_process) {
  if (!executable_path || !argv) {
    return false;
  }

  // Build a mutable command line: quoted executable_path followed by argv[1..].
  // argv[0] is the program name (like main/execve) and must not be duplicated.
  size_t cmd_len = strlen(executable_path) + 3;
  for (size_t i = 1; argv[i] != NULL; i++) {
    cmd_len += strlen(argv[i]) + 3;  // space + quotes
  }
  char* cmd = (char*)malloc(cmd_len + 1);
  if (!cmd) {
    return false;
  }
  size_t pos = 0;
  cmd[pos++] = '"';
  size_t exe_len = strlen(executable_path);
  memcpy(cmd + pos, executable_path, exe_len);
  pos += exe_len;
  cmd[pos++] = '"';
  for (size_t i = 1; argv[i] != NULL; i++) {
    cmd[pos++] = ' ';
    cmd[pos++] = '"';
    size_t len = strlen(argv[i]);
    memcpy(cmd + pos, argv[i], len);
    pos += len;
    cmd[pos++] = '"';
  }
  cmd[pos] = '\0';

  // Convert to UTF-16 for CreateProcessW (requires a mutable wchar_t buffer)
  int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd, -1, NULL, 0);
  if (wlen <= 0) {
    free(cmd);
    return false;
  }
  wchar_t* wcmd = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
  if (!wcmd) {
    free(cmd);
    return false;
  }
  MultiByteToWideChar(CP_UTF8, 0, cmd, -1, wcmd, wlen);
  free(cmd);

  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));

  BOOL success = CreateProcessW(NULL, wcmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  free(wcmd);

  if (!success) {
    return false;
  }

  // Close thread handle immediately as it's not needed
  CloseHandle(pi.hThread);

  // Close process handle if not requested, otherwise return it
  if (out_process) {
    *out_process = pi.hProcess;
  } else {
    CloseHandle(pi.hProcess);
  }
  return true;
}

bool vw_platform_wait_process(vw_process_t process, uint32_t timeout_ms) {
  if (!process || process == INVALID_HANDLE_VALUE) return false;
  HANDLE hProcess = (HANDLE)process;
  DWORD result = WaitForSingleObject(hProcess, timeout_ms);
  return result == WAIT_OBJECT_0;
}

void vw_platform_close_process(vw_process_t process) {
  if (process && process != INVALID_HANDLE_VALUE) {
    CloseHandle((HANDLE)process);
  }
}

typedef struct {
  void* (*func)(void*);
  void* arg;
} vw_win32_thread_arg_t;

static DWORD WINAPI vw_win32_thread_proc(LPVOID lpParameter) {
  vw_win32_thread_arg_t* targ = (vw_win32_thread_arg_t*)lpParameter;
  void* (*func)(void*) = targ->func;
  void* arg = targ->arg;
  free(targ);
  func(arg);
  return 0;
}

bool vw_platform_thread_create(vw_thread_t* thread, void* (*func)(void*), void* arg) {
  if (!thread || !func) return false;
  vw_win32_thread_arg_t* targ = (vw_win32_thread_arg_t*)malloc(sizeof(vw_win32_thread_arg_t));
  if (!targ) return false;
  targ->func = func;
  targ->arg = arg;
  HANDLE hThread = CreateThread(NULL, 0, vw_win32_thread_proc, targ, 0, NULL);
  if (!hThread) {
    free(targ);
    return false;
  }
  *thread = (vw_thread_t)hThread;
  return true;
}

void vw_platform_thread_join(vw_thread_t thread) {
  if (thread) {
    HANDLE hThread = (HANDLE)thread;
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
  }
}

void vw_platform_sleep_ms(uint32_t ms) { Sleep(ms); }

#endif