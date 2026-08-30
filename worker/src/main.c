#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_log.h"
#include "vw_worker.h"
#include "vw_worker_config.h"

// Optional lifecycle log file: written to the platform temp directory, truncated every run so a single
// file holds the last worker session. Override with --log-file <path>. Content is the same privacy-safe
// vw_log_event stream (no PCM/transcript/token); pipe names and paths are kept.
static const char* vw_worker_default_log_dir(void) {
#ifdef _WIN32
  const char* dir = getenv("TEMP");
  if (dir && dir[0]) {
    return dir;
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
  FILE* f = fopen(path, "w");
  if (f) {
    vw_log_set_file(f);
  } else {
    fprintf(stderr, "vw_log: failed to open log file '%s'; logging to stderr only\n", path);
  }
}

int main(int argc, char** argv) {
  vw_worker_config_t config;
  vw_worker_config_init_defaults(&config);  // zeros auth_token, sets model/language/rate

  int parse_rc = vw_worker_config_parse_args(&config, argc, argv);
  if (parse_rc != 0) {
    return parse_rc;
  }

  vw_log_set_enabled(config.logging_enabled);
  vw_worker_setup_log_file(&config);
  return vw_worker_run(&config);
}
