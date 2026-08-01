#ifndef VW_AUDIO_CAPTURE_H_
#define VW_AUDIO_CAPTURE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VW_AUDIO_CHUNK_MAX_PCM_BYTES 16384  // 16KB max chunk size for PCM audio data
#define VW_AUDIO_TARGET_RATE \
  16000  // 16KHz like whisper.cpp expects; whisper.h already contains such a define, but we want to avoid including
         // whisper.h in the VLC plugin code

struct vw_spsc_queue;

// Audio capture state and configuration for VLC audio filter module
typedef struct vw_audio_capture {
  uint32_t target_sample_rate;  // 16000 Hz
  uint32_t target_channels;     // 1 (mono)
  uint32_t sample_remainder;    // Fractional sample remainder for exact PTS drift prevention
  int64_t last_pts_us;
  uint64_t total_samples_processed;
  struct vw_spsc_queue* queue;
} vw_audio_capture_t;

// Audio chunk structure representing a block of PCM audio data, processed from the VLC audio filter pipeline.
// NOTE: This structure uses a fixed-size inline uint8_t array for S16LE PCM data to strictly enforce zero
// heap allocations inside the realtime VLC callback (Rule 4), and to minimize IPC bandwidth.
// The worker process (vw_audio_buffer.h) will later heap-allocate and convert this S16LE data to float32 for
// whisper.cpp.
typedef struct vw_audio_chunk {
  int64_t start_pts_us;
  int64_t duration_us;
  uint32_t sample_rate;
  uint32_t channels;
  uint32_t bytes;                                  // actual PCM byte count
  uint8_t pcm_data[VW_AUDIO_CHUNK_MAX_PCM_BYTES];  // fixed-size inline buffer
} vw_audio_chunk_t;

// Supported raw audio formats provided by VLC
typedef enum {
  VW_AUDIO_FORMAT_S16,  // 16-bit signed integer
  VW_AUDIO_FORMAT_S32,  // 32-bit signed integer
  VW_AUDIO_FORMAT_FL32  // 32-bit float
} vw_audio_format_t;

// Encapsulates an incoming audio block's format and data
typedef struct {
  const void* pcm_data;      // Raw PCM samples
  size_t frame_count;        // Number of frames (one frame = one sample per channel)
  int64_t pts_us;            // Presentation timestamp
  vw_audio_format_t format;  // Sample format (S16 or FL32)
  uint32_t sample_rate;      // Original sample rate (e.g. 48000)
  uint32_t channels;         // Number of channels (e.g. 2 for stereo)
} vw_audio_input_t;

// Extracts incoming PCM audio blocks from the VLC pipeline, downmixes/resamples to 16kHz Mono S16,
// computes precise durations, and chunks them to fit limits.
// Strictly non-blocking and zero-allocation (Rule 4) to protect VLC callbacks. Pushes to SPSC queue and safely
// ignores overflow.
bool vw_audio_capture_process_block(vw_audio_capture_t* cap, const vw_audio_input_t* input);

#endif  // VW_AUDIO_CAPTURE_H_
