#include "vw_worker_config.h"

#include <stdio.h>
#include <string.h>

// Parse a 64-char hex string into a 32-byte token. Returns true on success.
static bool vw_token_from_hex(const char* hex, uint8_t out[VW_AUTH_TOKEN_BYTES]) {
  if (strlen(hex) != VW_AUTH_TOKEN_BYTES * 2) return false;  // must be exactly 64 hex chars
  for (size_t i = 0; i < VW_AUTH_TOKEN_BYTES; i++) {
    unsigned hi, lo;
    char c1 = hex[i * 2], c2 = hex[i * 2 + 1];
    if (c1 >= '0' && c1 <= '9')
      hi = (unsigned)(c1 - '0');
    else if (c1 >= 'a' && c1 <= 'f')
      hi = (unsigned)(c1 - 'a' + 10);
    else if (c1 >= 'A' && c1 <= 'F')
      hi = (unsigned)(c1 - 'A' + 10);
    else
      return false;
    if (c2 >= '0' && c2 <= '9')
      lo = (unsigned)(c2 - '0');
    else if (c2 >= 'a' && c2 <= 'f')
      lo = (unsigned)(c2 - 'a' + 10);
    else if (c2 >= 'A' && c2 <= 'F')
      lo = (unsigned)(c2 - 'A' + 10);
    else
      return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

bool vw_worker_config_init_defaults(vw_worker_config_t* config) {
  if (!config) {
    return false;
  }
  memset(config, 0, sizeof(vw_worker_config_t));
  strncpy(config->model_path, "models/ggml-tiny.en.bin", sizeof(config->model_path) - 1);
  strncpy(config->language, "en", sizeof(config->language) - 1);
  config->sample_rate = 16000;
  return true;
}

int vw_worker_config_parse_args(vw_worker_config_t* config, int argc, char** argv) {
  if (!config) {
    return 2;
  }
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--token") == 0 && i + 1 < argc) {
      if (!vw_token_from_hex(argv[++i], config->auth_token)) {
        fprintf(stderr, "bad --token: expected 64 hex chars\n");
        return 2;
      }
    } else if (strcmp(argv[i], "--pipe") == 0 && i + 1 < argc) {
      snprintf(config->pipe_name, sizeof(config->pipe_name), "%s", argv[++i]);
    } else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
      snprintf(config->model_path, sizeof(config->model_path), "%s", argv[++i]);
    } else {
      fprintf(stderr, "unknown option: %s\n", argv[i]);
      return 2;
    }
  }
  return 0;
}
