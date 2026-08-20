#include "vw_worker_config.h"

#include <stdio.h>
#include <stdlib.h>
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

// Probes for ggml-silero-vad.bin next to model_path or in standard search directories
static void vw_worker_config_autodiscover_vad(vw_worker_config_t* config) {
  if (config->vad_model_path[0] != '\0') {
    return;  // Explicitly set via --vad-model CLI flag
  }

  // 1. Check in the same directory as config->model_path
  char dir_cand[VW_PATH_MAX_BYTES];
  const char* last_slash = strrchr(config->model_path, '/');
  const char* last_bslash = strrchr(config->model_path, '\\');
  const char* slash = (last_slash > last_bslash) ? last_slash : last_bslash;
  if (slash != NULL) {
    size_t dir_len = (size_t)(slash - config->model_path) + 1;
    if (dir_len + strlen("ggml-silero-vad.bin") < sizeof(dir_cand)) {
      memcpy(dir_cand, config->model_path, dir_len);
      dir_cand[dir_len] = '\0';
      strcat(dir_cand, "ggml-silero-vad.bin");
      FILE* f = fopen(dir_cand, "rb");
      if (f != NULL) {
        fclose(f);
        strncpy(config->vad_model_path, dir_cand, sizeof(config->vad_model_path) - 1);
        return;
      }
    }
  }

  // 2. Standard candidate paths relative to CWD / binary
  static const char* const k_vad_candidates[] = {
      "models/ggml-silero-vad.bin",       "ggml-silero-vad.bin",           "../../../models/ggml-silero-vad.bin",
      "../../models/ggml-silero-vad.bin", "../models/ggml-silero-vad.bin", NULL};
  for (int i = 0; k_vad_candidates[i] != NULL; i++) {
    FILE* f = fopen(k_vad_candidates[i], "rb");
    if (f != NULL) {
      fclose(f);
      strncpy(config->vad_model_path, k_vad_candidates[i], sizeof(config->vad_model_path) - 1);
      return;
    }
  }
}

bool vw_worker_config_init_defaults(vw_worker_config_t* config) {
  if (!config) {
    return false;
  }
  memset(config, 0, sizeof(vw_worker_config_t));
  strncpy(config->model_path, "models/ggml-tiny.en.bin", sizeof(config->model_path) - 1);
  strncpy(config->language, "en", sizeof(config->language) - 1);
  config->sample_rate = 16000;
  config->backend = VW_WORKER_BACKEND_AUTO;
  config->gpu_device = 0;
  config->vad_model_path[0] = '\0';
  return true;
}

int vw_worker_config_parse_args(vw_worker_config_t* config, int argc, char** argv) {
  if (!config) {
    return 2;
  }
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--token") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing value for --token\n");
        return 2;
      }
      if (!vw_token_from_hex(argv[++i], config->auth_token)) {
        fprintf(stderr, "bad --token: expected 64 hex chars\n");
        return 2;
      }
    } else if (strcmp(argv[i], "--pipe") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing value for --pipe\n");
        return 2;
      }
      snprintf(config->pipe_name, sizeof(config->pipe_name), "%s", argv[++i]);
    } else if (strcmp(argv[i], "--model") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing value for --model\n");
        return 2;
      }
      snprintf(config->model_path, sizeof(config->model_path), "%s", argv[++i]);
    } else if (strcmp(argv[i], "--vad-model") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing value for --vad-model\n");
        return 2;
      }
      snprintf(config->vad_model_path, sizeof(config->vad_model_path), "%s", argv[++i]);
    } else if (strcmp(argv[i], "--log-file") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing value for --log-file\n");
        return 2;
      }
      snprintf(config->log_file, sizeof(config->log_file), "%s", argv[++i]);
    } else if (strcmp(argv[i], "--backend") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing value for --backend\n");
        return 2;
      }
      const char* b = argv[++i];
      if (strcmp(b, "auto") == 0) {
        config->backend = VW_WORKER_BACKEND_AUTO;
      } else if (strcmp(b, "gpu") == 0) {
        config->backend = VW_WORKER_BACKEND_GPU;
      } else if (strcmp(b, "cpu") == 0) {
        config->backend = VW_WORKER_BACKEND_CPU;
      } else {
        fprintf(stderr, "bad --backend: expected auto|gpu|cpu, got '%s'\n", b);
        return 2;
      }
    } else if (strcmp(argv[i], "--gpu-device") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing value for --gpu-device\n");
        return 2;
      }
      char* end = NULL;
      long id = strtol(argv[++i], &end, 10);
      if (end == argv[i] || *end != '\0' || id < 0 || id > 65535) {
        fprintf(stderr, "bad --gpu-device: expected a non-negative integer\n");
        return 2;
      }
      config->gpu_device = (int)id;
    } else {
      fprintf(stderr, "unknown option: %s\n", argv[i]);
      return 2;
    }
  }

  // Auto-discover VAD model if not explicitly specified via CLI
  vw_worker_config_autodiscover_vad(config);

  return 0;
}
