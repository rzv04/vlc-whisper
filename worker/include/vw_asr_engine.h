#ifndef VW_ASR_ENGINE_H_
#define VW_ASR_ENGINE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Backend selection shared by local ASR adapters. AUTO prefers an available GPU path, GPU requests GPU acceleration,
// and CPU forbids GPU use. The existing VW_WORKER_BACKEND_* constants remain stable for compatibility.
typedef enum vw_worker_backend {
  VW_WORKER_BACKEND_AUTO = 0,
  VW_WORKER_BACKEND_GPU,
  VW_WORKER_BACKEND_CPU,
} vw_worker_backend_t;

typedef enum vw_asr_engine_kind {
  VW_ASR_ENGINE_WHISPER = 0,
  VW_ASR_ENGINE_NEMOTRON,
} vw_asr_engine_kind_t;

typedef enum vw_asr_capability {
  VW_ASR_CAP_LOCAL = 1U << 0,
  VW_ASR_CAP_REMOTE = 1U << 1,
  VW_ASR_CAP_FINAL_ONLY = 1U << 2,
  VW_ASR_CAP_NATIVE_STREAMING = 1U << 3,
  VW_ASR_CAP_PARTIAL_RESULTS = 1U << 4,
  VW_ASR_CAP_SOURCE_LOOKAHEAD = 1U << 5,
  VW_ASR_CAP_CPU = 1U << 6,
  VW_ASR_CAP_VULKAN = 1U << 7,
  VW_ASR_CAP_REQUIRES_CREDENTIALS = 1U << 8,
} vw_asr_capability_t;

typedef struct vw_asr_engine_descriptor {
  vw_asr_engine_kind_t kind;
  const char* id;
  uint32_t capabilities;
  bool available;
} vw_asr_engine_descriptor_t;

typedef struct vw_asr_result {
  int64_t start_offset_us;
  int64_t end_offset_us;
  bool is_final;
  float no_speech_prob;
  const char* text_utf8;
} vw_asr_result_t;

typedef struct vw_asr_engine_config {
  vw_asr_engine_kind_t kind;
  const char* model_path;
  vw_worker_backend_t backend;
  int gpu_device;
  const char* language;
  int n_threads;
} vw_asr_engine_config_t;

typedef struct vw_asr_engine vw_asr_engine_t;

// Parses a stable user/configuration engine identifier into its enum value, rejecting unknown or empty identifiers
// instead of silently selecting another engine. Returns false without modifying output when parsing fails.
bool vw_asr_engine_kind_from_id(const char* id, vw_asr_engine_kind_t* out_kind);

// Returns immutable metadata describing one known ASR engine, including availability and capabilities, or NULL when the
// supplied enum value is outside the recognized engine set.
const vw_asr_engine_descriptor_t* vw_asr_engine_get_descriptor(vw_asr_engine_kind_t kind);

// Creates the selected available ASR adapter from normalized worker configuration, owning all engine-specific state
// until destruction. Returns NULL when configuration or the selected adapter cannot initialize.
vw_asr_engine_t* vw_asr_engine_create(const vw_asr_engine_config_t* config);

// Destroys the selected adapter and all engine-specific resources owned by the facade; accepting NULL keeps worker
// teardown paths simple and preserves existing fail-safe cleanup behavior.
void vw_asr_engine_free(vw_asr_engine_t* engine);

// Runs one complete PCM window through adapters that support windowed transcription. Native streaming adapters may
// reject this operation and instead expose streaming entry points in their implementation branch.
bool vw_asr_engine_transcribe_pcm(vw_asr_engine_t* engine, const float* pcm32, size_t sample_count);

// Returns the number of normalized results produced by the most recent successful window transcription, or zero for
// NULL engines and adapters that currently have no result available.
int vw_asr_engine_get_result_count(const vw_asr_engine_t* engine);

// Copies one normalized result using signed microsecond offsets and explicit finality while retaining adapter-owned text
// storage. Returns false for invalid indexes, unavailable results, or malformed adapter state.
bool vw_asr_engine_get_result(const vw_asr_engine_t* engine, int index, vw_asr_result_t* out_result);

// Returns cumulative inference time in microseconds from the adapter's authoritative metric producer, excluding
// downstream VAD, filtering, translation, IPC, and presentation work performed elsewhere.
uint64_t vw_asr_engine_get_total_inference_us(const vw_asr_engine_t* engine);

// Reports whether the selected local adapter actually initialized and uses GPU acceleration, so STATUS reflects runtime
// truth rather than only the user's requested backend setting.
bool vw_asr_engine_is_gpu_active(const vw_asr_engine_t* engine);

#endif  // VW_ASR_ENGINE_H_
