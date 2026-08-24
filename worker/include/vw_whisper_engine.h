#ifndef VW_WHISPER_ENGINE_H_
#define VW_WHISPER_ENGINE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Inference backend selection (step 17a). AUTO = use_gpu=true (whisper picks the first
// GPU/IGPU device and transparently falls back to CPU when none exists); GPU forces the same
// GPU-first path; CPU forces use_gpu=false (never consults GPU devices).
typedef enum vw_worker_backend {
  VW_WORKER_BACKEND_AUTO = 0,
  VW_WORKER_BACKEND_GPU,
  VW_WORKER_BACKEND_CPU,
} vw_worker_backend_t;

// Individual transcribed phrase/segment with relative microsecond offsets within the window.
typedef struct vw_whisper_segment {
  int64_t t0_us;          // Start offset in microseconds relative to window start
  int64_t t1_us;          // End offset in microseconds relative to window start
  float no_speech_prob;   // Silence probability [0.0, 1.0] from whisper acoustic decoder
  const char* text_utf8;  // Borrowed pointer to UTF-8 text (valid until next transcribe or engine_free)
} vw_whisper_segment_t;

typedef struct vw_whisper_engine {
  struct whisper_context* ctx;  // Opaque whisper.cpp context
  char* last_text;              // Concatenated UTF-8 output of last transcribe run
  size_t last_text_bytes;       // Capacity of last_text buffer
  char language[16];            // Concrete whisper language code (e.g. "en"), NUL-terminated
  int n_threads;                // CPU threads for inference (1..16, clamped)
  bool gpu_active;              // True when inference actually runs on a GPU/IGPU device (runtime truth,
                                // not the requested backend); false after CPU fallback or CPU-forced init
} vw_whisper_engine_t;

// Initializes whisper.cpp engine instance from the specified model file path (ADR-015: model-once lifetime).
// backend selects inference: AUTO/GPU set use_gpu=true (whisper picks the first GPU/IGPU device and falls
// back to CPU at runtime when none exists), CPU forces use_gpu=false. gpu_device is the GPU/IGPU ordinal.
// language selects SOT token (e.g. "en"); NULL or empty defaults to "en". n_threads clamped 1..16 (default 4).
// Runs a silent warmup inference pass on load. Returns NULL if model file is missing or invalid.
vw_whisper_engine_t* vw_whisper_engine_init(const char* model_path, vw_worker_backend_t backend, int gpu_device);

// Sets the language used by subsequent transcription calls without reinitializing the model; rejects NULL,
// empty, "auto", and values exceeding the fixed 16-byte buffer, returning false without side effects.
bool vw_whisper_engine_set_language(vw_whisper_engine_t* engine, const char* language);

// Sets the thread count used by subsequent transcription calls without reinitializing the model; clamps
// values into the supported inclusive range 1..16 and always returns true for a non-NULL engine.
bool vw_whisper_engine_set_n_threads(vw_whisper_engine_t* engine, int n_threads);

// Reports whether inference actually executes on a GPU/IGPU device for this engine instance: mirrors
// whisper.cpp's own device selection (requested ordinal must exist), so CPU runtime fallback reports
// false even when AUTO/GPU was requested. STATUS resolved_backend must read this, not the request.
bool vw_whisper_engine_is_gpu_active(const vw_whisper_engine_t* engine);

// Safely destroys whisper.cpp engine instance and frees associated model memory.
void vw_whisper_engine_free(vw_whisper_engine_t* engine);

// Runs whisper.cpp transcription on normalized float32 PCM samples at 16kHz.
bool vw_whisper_engine_transcribe_pcm(vw_whisper_engine_t* engine, const float* pcm32, size_t sample_count);

// Returns pointer to concatenated UTF-8 text from the last transcribe run, or "" if empty/NULL.
const char* vw_whisper_engine_get_text(const vw_whisper_engine_t* engine);

// Returns the number of discrete sub-segments detected during the last transcription run, returning zero if the
// engine is uninitialized or the audio window contained only silence.
int vw_whisper_engine_get_segment_count(const vw_whisper_engine_t* engine);

// Populates out_seg with microsecond timestamps and borrowed UTF-8 text for the segment at index, returning true on
// success or false if out of bounds.
bool vw_whisper_engine_get_segment(const vw_whisper_engine_t* engine, int index, vw_whisper_segment_t* out_seg);

#endif  // VW_WHISPER_ENGINE_H_
