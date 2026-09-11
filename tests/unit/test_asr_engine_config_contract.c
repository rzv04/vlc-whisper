#include <string.h>

#include "vw_test.h"
#include "vw_worker_config.h"

int main(void) {
  vw_worker_config_t config;

  vw_test_check_true("worker config initializes before ASR-engine parsing", vw_worker_config_init_defaults(&config));
  char* whisper_args[] = {"vlc-whisper-worker", "--asr-engine", "whisper", NULL};
  vw_test_check_true("explicit Whisper ASR engine is accepted",
                     vw_worker_config_parse_args(&config, 3, whisper_args) == 0);

  vw_test_check_true("worker config reinitializes before Nemotron parsing", vw_worker_config_init_defaults(&config));
  char* nemotron_args[] = {"vlc-whisper-worker", "--asr-engine", "nemotron", NULL};
  vw_test_check_true("known experimental Nemotron ASR engine identity is accepted",
                     vw_worker_config_parse_args(&config, 3, nemotron_args) == 0);

  vw_test_check_true("worker config reinitializes before unknown-engine parsing",
                     vw_worker_config_init_defaults(&config));
  char* unknown_args[] = {"vlc-whisper-worker", "--asr-engine", "not-an-engine", NULL};
  vw_test_check_true("unknown ASR engine identity is rejected",
                     vw_worker_config_parse_args(&config, 3, unknown_args) == 2);

  vw_test_check_true("worker config reinitializes before oversized-engine parsing",
                     vw_worker_config_init_defaults(&config));
  char oversized_engine[128];
  memset(oversized_engine, 'x', sizeof(oversized_engine));
  oversized_engine[sizeof(oversized_engine) - 1U] = '\0';
  char* oversized_args[] = {"vlc-whisper-worker", "--asr-engine", oversized_engine, NULL};
  vw_test_check_true("oversized ASR engine identity is rejected before truncation",
                     vw_worker_config_parse_args(&config, 3, oversized_args) == 2);

  return vw_test_finish("test_asr_engine_config_contract");
}
