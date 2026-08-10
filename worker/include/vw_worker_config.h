#ifndef VW_WORKER_CONFIG_H_
#define VW_WORKER_CONFIG_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

typedef struct vw_worker_config {
  char model_path[256];
  char language[8];
  uint32_t sample_rate;
  char pipe_name[256];
  char log_file[512];  // --log-file override; empty = default temp-dir log
  uint8_t auth_token[VW_AUTH_TOKEN_BYTES];
} vw_worker_config_t;

bool vw_worker_config_init_defaults(vw_worker_config_t* config);

// Parse worker CLI args (--pipe, --token <64 hex>, --model, --log-file) into config.
// Returns 0 on success, or 2 on bad usage (malformed --token, unknown option, missing value).
int vw_worker_config_parse_args(vw_worker_config_t* config, int argc, char** argv);

#endif  // VW_WORKER_CONFIG_H_
