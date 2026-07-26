#include "vw_worker_config.h"

#include <string.h>

bool vw_worker_config_init_defaults(vw_worker_config_t *config) {
  if (!config) {
    return false;
  }
  memset(config, 0, sizeof(vw_worker_config_t));
  strncpy(config->model_path, "models/ggml-tiny.en.bin", sizeof(config->model_path) - 1);
  strncpy(config->language, "en", sizeof(config->language) - 1);
  config->sample_rate = 16000;
  return true;
}
