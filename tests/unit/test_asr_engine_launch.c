#include <string.h>

#include "vw_test.h"
#include "vw_worker_client.h"

static int spawn_calls;
static int engine_flag_calls;
static char engine_arg[32];

bool __wrap_vw_platform_spawn_process(const char* path, const char* const argv[], vw_process_t* process) {
  (void)path;
  (void)process;
  spawn_calls++;
  for (size_t i = 0; argv[i]; i++) {
    if (strcmp(argv[i], "--asr-engine") == 0 && argv[i + 1]) {
      engine_flag_calls++;
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
  vw_test_check_true("default Whisper launch omits the engine flag for legacy workers",
                     spawn_calls == 1 && engine_flag_calls == 0 && engine_arg[0] == '\0');
  vw_worker_client_launch_and_connect_engine("/worker", "/ipc", token, NULL, "nemotron", "cpu", "en", 4, -1, NULL,
                                             false);
  vw_test_check_true("nondefault Nemotron selection forwards exactly the requested engine",
                     spawn_calls == 2 && engine_flag_calls == 1 && strcmp(engine_arg, "nemotron") == 0);
  vw_worker_client_launch_and_connect_engine("/worker", "/ipc", token, NULL, "nemotron-invalid", "auto", "en", 4, -1,
                                             NULL, false);
  vw_test_check_true("unknown engine fails before worker spawn", spawn_calls == 2 && engine_flag_calls == 1);
  return vw_test_finish("test_asr_engine_launch");
}
