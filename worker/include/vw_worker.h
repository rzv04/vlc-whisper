#ifndef VW_WORKER_H_
#define VW_WORKER_H_

#include "vw_audio_buffer.h"
#include "vw_segment_builder.h"
#include "vw_vad.h"
#include "vw_whisper_engine.h"
#include "vw_worker_config.h"

// Runs the worker process loop, listening on the configured pipe/socket, validating authentication,
// ingesting IPC audio frames, executing whisper inference, and emitting caption segments over IPC.
int vw_worker_run(const vw_worker_config_t* config);

#endif  // VW_WORKER_H_
