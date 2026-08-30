#ifndef VW_WORKER_CONFIG_H_
#define VW_WORKER_CONFIG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vw_protocol_types.h"
#include "vw_whisper_engine.h"

typedef struct vw_worker_config {
  char model_path[VW_PATH_MAX_BYTES];
  char model_dir[VW_PATH_MAX_BYTES];  // Per-user model directory override (--model-dir), empty = default per-user dir
  char vad_model_path[VW_PATH_MAX_BYTES];  // Path to Silero VAD GGML model file (--vad-model or auto-discovered)
  char language[16];
  int n_threads;
  uint32_t sample_rate;
  char pipe_name[256];
  char log_file[512];    // --log-file override; empty = default temp-dir log
  bool logging_enabled;  // --enable-logging or --log-file; false by default
  uint8_t auth_token[VW_AUTH_TOKEN_BYTES];
  vw_worker_backend_t backend;  // --backend auto|gpu|cpu (default AUTO)
  int gpu_device;               // --gpu-device <id>: ordinal into whisper's GPU/IGPU device list
} vw_worker_config_t;

// Initializes worker configuration struct with default values (16kHz audio, tiny model, AUTO GPU backend).
// Returns true on success or false if config pointer is NULL.
bool vw_worker_config_init_defaults(vw_worker_config_t* config);

// Parses command-line arguments into the worker configuration structure and performs syntax validation. Returns 0 on
// success or 2 on error; model paths are resolved during worker initialization.
int vw_worker_config_parse_args(vw_worker_config_t* config, int argc, char** argv);

// Resolves relative model paths first against the per-user model directory and then the worker's adjacent install
// models directory, while preserving absolute and existing paths during worker initialization.
bool vw_worker_config_resolve_model_path(const vw_worker_config_t* config, char* out, size_t out_size);

// Resolves an explicit VAD path or discovers Silero beside the effective model, model directory, install models, then
// compatibility CWD paths. Returns true only when a usable VAD file is found or explicitly configured.
bool vw_worker_config_resolve_vad_model_path(const vw_worker_config_t* config, const char* effective_model_path,
                                             char* out, size_t out_size);

#endif  // VW_WORKER_CONFIG_H_
