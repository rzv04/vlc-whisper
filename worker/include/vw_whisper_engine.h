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
  const char* text_utf8;  // Borrowed pointer to UTF-8 text (valid until next transcribe or engine_free)
} vw_whisper_segment_t;

// Maximum bytes (including NUL) for a single Whisper token's text, matching whisper.cpp's token text buffer.
// Token text may carry a leading space and is copied into the caller's struct, not borrowed.
#define VW_WHISPER_MAX_TOKEN_BYTES 128

// Maximum number of tokens reported per transcribed segment; bounds the engine's per-segment token arrays.
#define VW_WHISPER_MAX_TOKENS_PER_SEGMENT 128

// A single Whisper token with its authentic spoken boundary scaled to microseconds.
// t0_us/t1_us are RELATIVE to the transcribed window start (window_pts_us + t0_us = media PTS).
typedef struct vw_whisper_token {
  char text[VW_WHISPER_MAX_TOKEN_BYTES];
  int64_t t0_us;
  int64_t t1_us;
} vw_whisper_token_t;

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

// Returns the number of discrete sub-segments detected during the last transcription run, returning zero if the
// engine is uninitialized or the audio window contained only silence.
int vw_whisper_engine_get_segment_count(const vw_whisper_engine_t* engine);

// Populates out_seg with microsecond timestamps and borrowed UTF-8 text for the segment at index, returning true on
// success or false if out of bounds.
bool vw_whisper_engine_get_segment(const vw_whisper_engine_t* engine, int index, vw_whisper_segment_t* out_seg);

// Returns the number of per-token entries in the segment at segment_index, or zero if the engine is
// uninitialized or the segment index is out of bounds. Used to bound per-segment token arrays.
int vw_whisper_engine_get_segment_token_count(const vw_whisper_engine_t* engine, int segment_index);

// Populates out_token with text and relative microsecond boundaries for the token at token_index within
// segment_index, returning true on success or false if the engine, segment, or token index is invalid.
bool vw_whisper_engine_get_segment_token(const vw_whisper_engine_t* engine, int segment_index, int token_index,
                                         vw_whisper_token_t* out_token);

#endif  // VW_WHISPER_ENGINE_H_
