#include <string.h>

#include "vw_test.h"
#include "vw_worker_client.h"

static int spawn_calls;
static char engine_arg[32];

bool __wrap_vw_platform_spawn_process(const char* path, const char* const argv[], vw_process_t* process) {
  (void)path;
  (void)process;
  spawn_calls++;
  for (size_t i = 0; argv[i]; i++) {
    if (strcmp(argv[i], "--asr-engine") == 0 && argv[i + 1]) {
      snprintf(engine_arg, sizeof(engine_arg), "%s", argv[i + 1]);
      break;
    }
  }
  return false;  // Do not connect or create a process: inspect the actual launch boundary.
}

int main(void) {
  uint8_t token[VW_AUTH_TOKEN_BYTES] = {0};
  vw_test_check_false("Whisper launch reaches the process boundary",
                      vw_worker_client_launch_and_connect_engine("/worker", "/ipc", token, NULL, "whisper", "auto",
                                                                 "en", 4, -1, NULL, false) != NULL);
  vw_test_check_true("Whisper identity forwarded independently of backend",
                     spawn_calls == 1 && strcmp(engine_arg, "whisper") == 0);
  vw_worker_client_launch_and_connect_engine("/worker", "/ipc", token, NULL, "nemotron", "cpu", "en", 4, -1, NULL,
                                             false);
  vw_test_check_true("Nemotron selection forwards exactly the requested engine",
                     spawn_calls == 2 && strcmp(engine_arg, "nemotron") == 0);
  vw_worker_client_launch_and_connect_engine("/worker", "/ipc", token, NULL, "nemotron-invalid", "auto", "en", 4, -1,
                                             NULL, false);
  vw_test_check_true("unknown engine fails before worker spawn", spawn_calls == 2);
  return vw_test_finish("test_asr_engine_launch");
}
