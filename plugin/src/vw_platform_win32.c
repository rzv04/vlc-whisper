#if defined(_WIN32) || defined(__MINGW32__)
// clang-format off
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// clang-format on

#ifndef BCRYPT_RNG_ALG_HANDLE
#define BCRYPT_RNG_ALG_HANDLE ((BCRYPT_ALG_HANDLE)0x00000081)
#endif

#include "vw_platform.h"

bool vw_platform_get_random_bytes(void* buffer, size_t size) {
  if (!buffer || size == 0) return false;
  NTSTATUS status = BCryptGenRandom(NULL, (PUCHAR)buffer, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  return BCRYPT_SUCCESS(status);
}

int64_t vw_platform_get_time_us(void) {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  return (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10);
}

int64_t vw_platform_get_monotonic_time_us(void) {
  static volatile LONGLONG frequency = 0;
  LONGLONG freq = InterlockedCompareExchange64(&frequency, 0, 0);
  if (freq <= 0) {
    LARGE_INTEGER discovered;
    if (!QueryPerformanceFrequency(&discovered) || discovered.QuadPart <= 0) return -1;
    LONGLONG previous = InterlockedCompareExchange64(&frequency, discovered.QuadPart, 0);
    freq = previous > 0 ? previous : discovered.QuadPart;
  }
  LARGE_INTEGER counter;
  if (!QueryPerformanceCounter(&counter)) return -1;
  LONGLONG sec = counter.QuadPart / freq;
  LONGLONG rem = counter.QuadPart % freq;
  return sec * 1000000LL + (rem * 1000000LL) / freq;
}

bool vw_platform_spawn_process(const char* executable_path, const char* const argv[], vw_process_t* out_process) {
  if (!executable_path || !argv) return false;
#ifdef _WIN32
  // Windows: require an absolute trusted path — bare names would resolve via CWD/PATH hijack.
  bool is_absolute = false;
  if (executable_path[0] == '\\' && executable_path[1] == '\\')
    is_absolute = true;  // UNC
  else if (executable_path[0] == '/' || executable_path[0] == '\\')
    is_absolute = true;
  else if (((executable_path[0] >= 'A' && executable_path[0] <= 'Z') ||
            (executable_path[0] >= 'a' && executable_path[0] <= 'z')) &&
           executable_path[1] == ':' && (executable_path[2] == '\\' || executable_path[2] == '/'))
    is_absolute = true;
  if (!is_absolute) return false;
#endif

  size_t cmd_len = strlen(executable_path) * 2 + 10;
  for (size_t i = 1; argv[i] != NULL; i++) cmd_len += strlen(argv[i]) * 2 + 10;
  char* cmd = (char*)malloc(cmd_len + 1);
  if (!cmd) return false;
  size_t pos = 0;

  const char* all_args[64];
  size_t arg_count = 0;
  all_args[arg_count++] = executable_path;
  for (size_t i = 1; argv[i] != NULL && arg_count < 63; i++) all_args[arg_count++] = argv[i];
  all_args[arg_count] = NULL;

  for (size_t a = 0; a < arg_count; a++) {
    if (a > 0) cmd[pos++] = ' ';
    const char* arg = all_args[a];
    cmd[pos++] = '"';
    for (size_t i = 0; arg[i] != '\0'; i++) {
      if (arg[i] == '\\') {
        size_t num_slashes = 1;
        while (arg[i + 1] == '\\') {
          num_slashes++;
          i++;
        }
        if (arg[i + 1] == '"' || arg[i + 1] == '\0') {
          for (size_t s = 0; s < num_slashes * 2; s++) cmd[pos++] = '\\';
        } else {
          for (size_t s = 0; s < num_slashes; s++) cmd[pos++] = '\\';
        }
      } else if (arg[i] == '"') {
        cmd[pos++] = '\\';
        cmd[pos++] = '"';
      } else {
        cmd[pos++] = arg[i];
      }
    }
    cmd[pos++] = '"';
  }
  cmd[pos] = '\0';
  int wapp_len = MultiByteToWideChar(CP_UTF8, 0, executable_path, -1, NULL, 0);
  wchar_t* wapp = NULL;
  if (wapp_len > 0) {
    wapp = (wchar_t*)malloc((size_t)wapp_len * sizeof(wchar_t));
    if (wapp) {
      if (MultiByteToWideChar(CP_UTF8, 0, executable_path, -1, wapp, wapp_len) <= 0) {
        free(wapp);
        wapp = NULL;
      }
    }
  }
  if (!wapp) {
    free(cmd);
    return false;
  }
  int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd, -1, NULL, 0);
  if (wlen <= 0) {
    free(cmd);
    free(wapp);
    return false;
  }
  wchar_t* wcmd = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
  if (!wcmd) {
    free(cmd);
    free(wapp);
    return false;
  }
  MultiByteToWideChar(CP_UTF8, 0, cmd, -1, wcmd, wlen);
  free(cmd);

  STARTUPINFOEXW six;
  PROCESS_INFORMATION pi;
  ZeroMemory(&six, sizeof(six));
  six.StartupInfo.cb = sizeof(six);
  ZeroMemory(&pi, sizeof(pi));

  // The worker is a console executable, but it is an implementation detail of the VLC filter.
  // Keep both console-subsystem and GUI-subsystem builds out of the user's taskbar/window list.
  bool hide_worker_window = strstr(executable_path, "vlc-whisper-worker") != NULL;
  if (hide_worker_window) {
    six.StartupInfo.dwFlags |= STARTF_USESHOWWINDOW;
    six.StartupInfo.wShowWindow = SW_HIDE;
  }

  HANDLE hLog = INVALID_HANDLE_VALUE;
  HANDLE hStdin = INVALID_HANDLE_VALUE;
  LPPROC_THREAD_ATTRIBUTE_LIST attr_list = NULL;
  BOOL inherit = FALSE;
  DWORD creation_flags = hide_worker_window ? CREATE_NO_WINDOW : 0;
  if (strstr(executable_path, "vlc-whisper-worker") != NULL) {
    char tmp_dir[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tmp_dir) > 0) {
      char log_path[MAX_PATH];
      int log_len = snprintf(log_path, sizeof(log_path), "%svlc-whisper-worker.log", tmp_dir);
      if (log_len >= 0 && (size_t)log_len < sizeof(log_path)) {
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = NULL;
        sa.bInheritHandle = TRUE;
        hLog = CreateFileA(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
        hStdin = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, NULL);
        if (hLog != INVALID_HANDLE_VALUE && hStdin != INVALID_HANDLE_VALUE) {
          SIZE_T attr_size = 0;
          InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
          attr_list = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_size);
          if (attr_list && InitializeProcThreadAttributeList(attr_list, 1, 0, &attr_size)) {
            HANDLE allowed_handles[2] = {hLog, hStdin};
            if (UpdateProcThreadAttribute(attr_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, allowed_handles,
                                          sizeof(allowed_handles), NULL, NULL)) {
              six.lpAttributeList = attr_list;
              six.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
              six.StartupInfo.hStdOutput = hLog;
              six.StartupInfo.hStdError = hLog;
              six.StartupInfo.hStdInput = hStdin;
              inherit = TRUE;
              creation_flags |= EXTENDED_STARTUPINFO_PRESENT;
            }
          }
        }
      }
    }
  }

  BOOL success = CreateProcessW(wapp, wcmd, NULL, NULL, inherit, creation_flags, NULL, NULL, &six.StartupInfo, &pi);

  if (attr_list) {
    if (six.lpAttributeList) DeleteProcThreadAttributeList(attr_list);
    free(attr_list);
  }
  if (hStdin != INVALID_HANDLE_VALUE) CloseHandle(hStdin);
  if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
  free(wapp);
  free(wcmd);
  if (!success) return false;

  CloseHandle(pi.hThread);
  if (out_process)
    *out_process = pi.hProcess;
  else
    CloseHandle(pi.hProcess);
  return true;
}

bool vw_platform_wait_process(vw_process_t process, uint32_t timeout_ms) {
  if (!process || process == INVALID_HANDLE_VALUE) return false;
  HANDLE hProcess = (HANDLE)process;
  DWORD result = WaitForSingleObject(hProcess, timeout_ms);
  return result == WAIT_OBJECT_0;
}

void vw_platform_terminate_process(vw_process_t process) {
  if (process && process != INVALID_HANDLE_VALUE) {
    HANDLE hProcess = (HANDLE)process;
    TerminateProcess(hProcess, 1);
    CloseHandle(hProcess);
  }
}

void vw_platform_close_process(vw_process_t process) {
  if (process && process != INVALID_HANDLE_VALUE) CloseHandle((HANDLE)process);
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

#else
typedef int vw_platform_win32_empty_tu_t;
#endif