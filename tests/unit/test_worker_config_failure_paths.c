#include <stdio.h>
#include <string.h>

#include "vw_worker_config.h"

static int g_failures = 0;

static void check_result(const char* name, int actual, int expected) {
  if (actual != expected) {
    fprintf(stderr, "FAIL: %s (expected %d, got %d)\n", name, expected, actual);
    g_failures++;
  }
}

static void fill_string(char* buffer, size_t size, char value) {
  memset(buffer, value, size - 1U);
  buffer[size - 1U] = '\0';
}

int main(void) {
  vw_worker_config_t config;

  char pipe_exact[sizeof(config.pipe_name)];
  fill_string(pipe_exact, sizeof(pipe_exact), 'p');
  check_result("maximum fitting --pipe is accepted", vw_worker_config_init_defaults(&config) ? 0 : 2, 0);
  char* pipe_exact_argv[] = {"vlc-whisper-worker", "--pipe", pipe_exact, NULL};
  check_result("maximum fitting --pipe round-trips", vw_worker_config_parse_args(&config, 3, pipe_exact_argv), 0);
  if (strcmp(config.pipe_name, pipe_exact) != 0) {
    fprintf(stderr, "FAIL: maximum fitting --pipe was not preserved exactly\n");
    g_failures++;
  }

  char pipe_too_long[sizeof(config.pipe_name) + 1U];
  fill_string(pipe_too_long, sizeof(pipe_too_long), 'q');
  check_result("config reset before oversized --pipe", vw_worker_config_init_defaults(&config) ? 0 : 2, 0);
  char* pipe_long_argv[] = {"vlc-whisper-worker", "--pipe", pipe_too_long, NULL};
  check_result("oversized --pipe is rejected instead of truncated",
               vw_worker_config_parse_args(&config, 3, pipe_long_argv), 2);

  char vad_too_long[sizeof(config.vad_model_path) + 1U];
  fill_string(vad_too_long, sizeof(vad_too_long), 'v');
  check_result("config reset before oversized --vad-model", vw_worker_config_init_defaults(&config) ? 0 : 2, 0);
  char* vad_long_argv[] = {"vlc-whisper-worker", "--vad-model", vad_too_long, NULL};
  check_result("oversized --vad-model is rejected instead of truncated",
               vw_worker_config_parse_args(&config, 3, vad_long_argv), 2);

  char log_too_long[sizeof(config.log_file) + 1U];
  fill_string(log_too_long, sizeof(log_too_long), 'l');
  check_result("config reset before oversized --log-file", vw_worker_config_init_defaults(&config) ? 0 : 2, 0);
  char* log_long_argv[] = {"vlc-whisper-worker", "--log-file", log_too_long, NULL};
  check_result("oversized --log-file is rejected instead of truncated",
               vw_worker_config_parse_args(&config, 3, log_long_argv), 2);

  if (g_failures != 0) {
    fprintf(stderr, "test_worker_config_failure_paths: %d contract failure(s)\n", g_failures);
    return 1;
  }

  printf("test_worker_config_failure_paths PASSED\n");
  return 0;
}
