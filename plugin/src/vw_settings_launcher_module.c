#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#include <vlc_common.h>
#include <vlc_access.h>
// clang-format on

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char** environ;
#endif

#define VW_LAUNCHER_PATH_CHARS 4096
#define VW_LAUNCHER_WAIT_MS 1500

typedef struct {
  char result[2];
  size_t offset;
} vw_settings_launcher_sys_t;

#ifdef _WIN32
static bool vw_settings_launcher_executable(wchar_t* out, size_t out_count) {
  if (!out || out_count == 0) return false;

  HMODULE module = NULL;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)(uintptr_t)&vw_settings_launcher_executable, &module))
    return false;

  wchar_t path[VW_LAUNCHER_PATH_CHARS];
  DWORD length = GetModuleFileNameW(module, path, (DWORD)(sizeof(path) / sizeof(path[0])));
  if (length == 0 || length >= sizeof(path) / sizeof(path[0])) return false;
  path[length] = L'\0';

  // <VLC>/plugins/audio_filter/libvlc_whisper_plugin.dll -> <VLC>
  for (int i = 0; i < 3; ++i) {
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return false;
    *slash = L'\0';
  }

  int written = swprintf(out, out_count, L"%ls\\vlc-whisper-settings\\vlc-whisper-settings.exe", path);
  if (written <= 0 || (size_t)written >= out_count) return false;
  DWORD attributes = GetFileAttributesW(out);
  return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool vw_settings_launcher_start(void) {
  wchar_t executable[VW_LAUNCHER_PATH_CHARS];
  if (!vw_settings_launcher_executable(executable, sizeof(executable) / sizeof(executable[0]))) return false;

  wchar_t working_dir[VW_LAUNCHER_PATH_CHARS];
  if (wcslen(executable) >= sizeof(working_dir) / sizeof(working_dir[0])) return false;
  wcscpy(working_dir, executable);
  wchar_t* slash = wcsrchr(working_dir, L'\\');
  if (!slash) return false;
  *slash = L'\0';

  wchar_t command[VW_LAUNCHER_PATH_CHARS * 2];
  int written = swprintf(command, sizeof(command) / sizeof(command[0]), L"\"%ls\" --launch-detached", executable);
  if (written <= 0 || (size_t)written >= sizeof(command) / sizeof(command[0])) return false;

  STARTUPINFOW startup;
  PROCESS_INFORMATION process;
  ZeroMemory(&startup, sizeof(startup));
  ZeroMemory(&process, sizeof(process));
  startup.cb = sizeof(startup);

  if (!CreateProcessW(executable, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, working_dir, &startup, &process))
    return false;

  DWORD wait_result = WaitForSingleObject(process.hProcess, VW_LAUNCHER_WAIT_MS);
  if (wait_result == WAIT_TIMEOUT) {
    if (TerminateProcess(process.hProcess, 1))
      (void)WaitForSingleObject(process.hProcess, VW_LAUNCHER_WAIT_MS);
  }
  DWORD exit_code = 1;
  bool ok = wait_result == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exit_code) && exit_code == 0;
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return ok;
}
#else
static bool vw_settings_launcher_start(void) {
  const char* executable = "/usr/bin/vlc-whisper-settings";
  char* const args[] = {(char*)executable, (char*)"--launch-detached", NULL};
  pid_t pid = 0;
  if (posix_spawn(&pid, executable, NULL, NULL, args, environ) != 0) return false;

  int status = 0;
  const struct timespec pause = {.tv_sec = 0, .tv_nsec = 10 * 1000 * 1000};
  for (int elapsed_ms = 0; elapsed_ms < VW_LAUNCHER_WAIT_MS; elapsed_ms += 10) {
    pid_t waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid) return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (waited < 0) return false;
    nanosleep(&pause, NULL);
  }

  // The bootstrap should only live long enough to call QProcess::startDetached().
  // Reap an unexpected hung bootstrap so VLC never accumulates launcher artifacts.
  kill(pid, SIGKILL);
  (void)waitpid(pid, &status, 0);
  return false;
}
#endif

static ssize_t vw_settings_launcher_read(stream_t* stream, void* buffer, size_t length) {
  vw_settings_launcher_sys_t* sys = (vw_settings_launcher_sys_t*)stream->p_sys;
  if (!sys || length == 0 || sys->offset >= sizeof(sys->result)) return 0;
  size_t available = sizeof(sys->result) - sys->offset;
  size_t count = length < available ? length : available;
  if (buffer) memcpy(buffer, sys->result + sys->offset, count);
  sys->offset += count;
  return (ssize_t)count;
}

static int vw_settings_launcher_control(stream_t* stream, int query, va_list args) {
  (void)stream;
  switch (query) {
    case STREAM_CAN_SEEK:
    case STREAM_CAN_FASTSEEK:
    case STREAM_CAN_PAUSE:
      *va_arg(args, bool*) = false;
      return VLC_SUCCESS;
    case STREAM_CAN_CONTROL_PACE:
      *va_arg(args, bool*) = true;
      return VLC_SUCCESS;
    case STREAM_GET_SIZE:
      *va_arg(args, uint64_t*) = 2;
      return VLC_SUCCESS;
    case STREAM_GET_PTS_DELAY:
      *va_arg(args, int64_t*) = 0;
      return VLC_SUCCESS;
    default:
      return VLC_EGENERIC;
  }
}

int vw_settings_launcher_open(vlc_object_t* object) {
  stream_t* p_access = (stream_t*)object;
  if (!p_access->psz_location || strcmp(p_access->psz_location, "launch") != 0) return VLC_EGENERIC;

  vw_settings_launcher_sys_t* sys = (vw_settings_launcher_sys_t*)calloc(1, sizeof(*sys));
  if (!sys) return VLC_ENOMEM;
  sys->result[0] = vw_settings_launcher_start() ? '1' : '0';
  sys->result[1] = '\n';
  p_access->p_sys = sys;
  ACCESS_SET_CALLBACKS(vw_settings_launcher_read, NULL, vw_settings_launcher_control, NULL);
  return VLC_SUCCESS;
}

void vw_settings_launcher_close(vlc_object_t* object) {
  stream_t* stream = (stream_t*)object;
  free(stream->p_sys);
}
