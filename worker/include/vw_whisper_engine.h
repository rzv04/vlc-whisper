#ifndef VW_WHISPER_ENGINE_H_
#define VW_WHISPER_ENGINE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vw_worker_config.h"  // vw_worker_backend_t (backend selection, step 17a)

typedef struct vw_whisper_engine {
  struct whisper_context* ctx;  // Opaque whisper.cpp context
  char* last_text;              // Concatenated UTF-8 output of last transcribe run
  size_t last_text_bytes;       // Capacity of last_text buffer
} vw_whisper_engine_t;

// Initializes whisper.cpp engine instance from the specified model file path (ADR-015: model-once lifetime).
// backend selects inference: AUTO/GPU set use_gpu=true (whisper picks the first GPU/IGPU device and falls
// back to CPU at runtime when none exists), CPU forces use_gpu=false. gpu_device is the GPU/IGPU ordinal.
// Runs a silent warmup inference pass on load. Returns NULL if model file is missing or invalid.
vw_whisper_engine_t* vw_whisper_engine_init(const char* model_path, vw_worker_backend_t backend, int gpu_device);

// Safely destroys whisper.cpp engine instance and frees associated model memory.
void vw_whisper_engine_free(vw_whisper_engine_t* engine);

// Runs whisper.cpp transcription on normalized float32 PCM samples at 16kHz.
bool vw_whisper_engine_transcribe_pcm(vw_whisper_engine_t* engine, const float* pcm32, size_t sample_count);

// Returns pointer to concatenated UTF-8 text from the last transcribe run, or "" if empty/NULL.
const char* vw_whisper_engine_get_text(const vw_whisper_engine_t* engine);

#endif  // VW_WHISPER_ENGINE_H_
