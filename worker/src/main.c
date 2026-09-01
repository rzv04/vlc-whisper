#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif

#include "vw_log.h"
#include "vw_worker.h"
#include "vw_worker_config.h"

#ifdef _WIN32
// Converts the Unicode process command line into owned UTF-8 arguments for the worker's internal contracts.
static bool vw_worker_get_utf8_arguments(int* out_argc, char*** out_argv) {
  if (!out_argc || !out_argv) return false;
  int wide_argc = 0;
  wchar_t** wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
  if (!wide_argv || wide_argc <= 0) return false;
  char** utf8_argv = (char**)calloc((size_t)wide_argc + 1U, sizeof(char*));
  if (!utf8_argv) {
    LocalFree(wide_argv);
    return false;
  }
  for (int i = 0; i < wide_argc; i++) {
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1, NULL, 0, NULL, NULL);
    if (bytes <= 0) {
      for (int j = 0; j < i; j++) free(utf8_argv[j]);
      free(utf8_argv);
      LocalFree(wide_argv);
      return false;
    }
    utf8_argv[i] = (char*)malloc((size_t)bytes);
    if (!utf8_argv[i] ||
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1, utf8_argv[i], bytes, NULL, NULL) <= 0) {
      for (int j = 0; j <= i; j++) free(utf8_argv[j]);
      free(utf8_argv);
      LocalFree(wide_argv);
      return false;
    }
  }
  LocalFree(wide_argv);
  *out_argc = wide_argc;
  *out_argv = utf8_argv;
  return true;
}

// Releases the UTF-8 argument vector returned by vw_worker_get_utf8_arguments().
static void vw_worker_free_utf8_arguments(int argc, char** argv) {
  if (!argv) return;
  for (int i = 0; i < argc; i++) free(argv[i]);
  free(argv);
}

// Opens a UTF-8 log path through the Unicode Windows filesystem API.
static FILE* vw_worker_open_log_utf8(const char* path) {
  if (!path) return NULL;
  int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
  if (chars <= 0) return NULL;
  wchar_t* wide_path = (wchar_t*)malloc((size_t)chars * sizeof(wchar_t));
  if (!wide_path) return NULL;
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide_path, chars) <= 0) {
    free(wide_path);
    return NULL;
  }
  FILE* file = _wfopen(wide_path, L"w");
  free(wide_path);
  return file;
}
#endif

// Optional lifecycle log file: written to the platform temp directory, truncated every run so a single
// file holds the last worker session. Override with --log-file <path>. Content is the same privacy-safe
// vw_log_event stream (no PCM/transcript/token); pipe names and paths are kept.
static const char* vw_worker_default_log_dir(void) {
#ifdef _WIN32
  static char utf8_dir[VW_PATH_MAX_BYTES];
  wchar_t wide_dir[VW_PATH_MAX_BYTES];
  DWORD chars = GetTempPathW((DWORD)(sizeof(wide_dir) / sizeof(wide_dir[0])), wide_dir);
  if (chars > 0 && chars < sizeof(wide_dir) / sizeof(wide_dir[0]) &&
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_dir, -1, utf8_dir, sizeof(utf8_dir), NULL, NULL) > 0) {
    return utf8_dir;
  }
  return "C:\\Windows\\Temp";
#else
  const char* dir = getenv("XDG_RUNTIME_DIR");
  if (dir && dir[0]) {
    return dir;
  }
  dir = getenv("TMPDIR");
  if (dir && dir[0]) {
    return dir;
  }
  return "/tmp";
#endif
}

// Opens the log file in append-free truncate mode ("w": last run wins). A NULL config path selects
// the platform temp dir; failure to open falls back to stderr-only logging (never fatal).
static void vw_worker_setup_log_file(const vw_worker_config_t* config) {
  if (!config->logging_enabled) return;
  char path[1024];
  if (config->log_file[0]) {
    snprintf(path, sizeof(path), "%s", config->log_file);
  } else {
    const char* dir = vw_worker_default_log_dir();
    snprintf(path, sizeof(path), "%s%cvlc-whisper-worker.log", dir,
#ifdef _WIN32
             '\\'
#else
             '/'
#endif
    );
  }
#ifdef _WIN32
  FILE* f = vw_worker_open_log_utf8(path);
#else
  FILE* f = fopen(path, "w");
#endif
  if (f) {
    vw_log_set_file(f);
  } else {
    fprintf(stderr, "vw_log: failed to open log file '%s'; logging to stderr only\n", path);
  }
}

int main(int argc, char** argv) {
#ifdef _WIN32
  (void)argc;
  (void)argv;
  int utf8_argc = 0;
  char** utf8_argv = NULL;
  if (!vw_worker_get_utf8_arguments(&utf8_argc, &utf8_argv)) return 2;
  argc = utf8_argc;
  argv = utf8_argv;
#endif
  vw_worker_config_t config;
  vw_worker_config_init_defaults(&config);  // zeros auth_token, sets model/language/rate

  int parse_rc = vw_worker_config_parse_args(&config, argc, argv);
#ifdef _WIN32
  vw_worker_free_utf8_arguments(argc, argv);
#endif
  if (parse_rc != 0) {
    return parse_rc;
  }

  vw_log_set_enabled(config.logging_enabled);
  vw_worker_setup_log_file(&config);
  return vw_worker_run(&config);
}
