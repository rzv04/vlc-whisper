#ifndef VW_WORKER_CONFIG_H_
#define VW_WORKER_CONFIG_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

// Inference backend selection (step 17a). AUTO = use_gpu=true (whisper picks the first
// GPU/IGPU device and transparently falls back to CPU when none exists); GPU forces the same
// GPU-first path; CPU forces use_gpu=false (never consults GPU devices).
typedef enum vw_worker_backend {
  VW_WORKER_BACKEND_AUTO = 0,
  VW_WORKER_BACKEND_GPU,
  VW_WORKER_BACKEND_CPU,
} vw_worker_backend_t;

typedef struct vw_worker_config {
  char model_path[VW_PATH_MAX_BYTES];
  char language[8];
  uint32_t sample_rate;
  char pipe_name[256];
  char log_file[512];  // --log-file override; empty = default temp-dir log
  uint8_t auth_token[VW_AUTH_TOKEN_BYTES];
  vw_worker_backend_t backend;  // --backend auto|gpu|cpu (default AUTO)
  int gpu_device;               // --gpu-device <id>: ordinal into whisper's GPU/IGPU device list
} vw_worker_config_t;

bool vw_worker_config_init_defaults(vw_worker_config_t* config);

// Parse worker CLI args (--pipe, --token <64 hex>, --model, --log-file) into config.
// Returns 0 on success, or 2 on bad usage (malformed --token, unknown option, missing value).
int vw_worker_config_parse_args(vw_worker_config_t* config, int argc, char** argv);

#endif  // VW_WORKER_CONFIG_H_
