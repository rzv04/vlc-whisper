#ifndef VW_WORKER_CONFIG_H_
#define VW_WORKER_CONFIG_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct vw_worker_config {
  char model_path[256];
  char language[8];
  uint32_t sample_rate;
} vw_worker_config_t;

bool vw_worker_config_init_defaults(vw_worker_config_t *config);

#endif // VW_WORKER_CONFIG_H_
