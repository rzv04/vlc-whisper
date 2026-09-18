#ifndef VW_WORKER_H_
#define VW_WORKER_H_

#include "vw_audio_buffer.h"
#include "vw_segment_builder.h"
#include "vw_vad.h"
#include "vw_whisper_engine.h"
#include "vw_worker_config.h"

#define VW_LOOKAHEAD_CHUNK_SAMPLES 32000     // 2s lookahead audio demux chunk (32,000 samples at 16kHz)
#define VW_LOOKAHEAD_BUFFER_SAMPLES 960000   // 60s lookahead audio ring buffer (960,000 samples at 16kHz)
#define VW_WHISPER_MAX_CHUNK_SAMPLES 480000  // 30s maximum Whisper attention window (480,000 samples at 16kHz)

#ifndef VW_SESSION_STATE_IDLE
#define VW_SESSION_STATE_IDLE 0U
#endif
#ifndef VW_SESSION_STATE_PLAYING
#define VW_SESSION_STATE_PLAYING 1U
#endif
#ifndef VW_SESSION_STATE_PAUSED
#define VW_SESSION_STATE_PAUSED 2U
#endif

// Runs the worker process loop, listening on the configured pipe/socket, validating authentication,
// ingesting IPC audio frames, executing whisper inference, and emitting caption segments over IPC.
int vw_worker_run(const vw_worker_config_t* config);

#endif  // VW_WORKER_H_
