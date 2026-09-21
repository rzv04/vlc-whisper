#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef _WIN32
// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
// clang-format on
#endif

#include "vw_log.h"
#include "vw_process_policy.h"
#include "vw_settings_service.h"
#include "vw_worker.h"
#include "vw_worker_config.h"
#include "vw_worker_log_policy.h"

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
#endif

int main(int argc, char** argv) {
  if (!vw_process_install_worker_signal_policy()) return 2;
#ifdef _WIN32
  (void)argc;
  (void)argv;
  int utf8_argc = 0;
  char** utf8_argv = NULL;
  if (!vw_worker_get_utf8_arguments(&utf8_argc, &utf8_argv)) return 2;
  argc = utf8_argc;
  argv = utf8_argv;
#endif
  if (argc > 1 && (strcmp(argv[1], "--settings-download") == 0 || strcmp(argv[1], "--settings-translate") == 0)) {
    vw_log_set_enabled(false);
    int service_rc = vw_settings_service_run(argc, argv);
#ifdef _WIN32
    vw_worker_free_utf8_arguments(argc, argv);
#endif
    return service_rc;
  }
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
  int worker_rc = vw_worker_run(&config);
  vw_worker_teardown_log_file();
  return worker_rc;
}
