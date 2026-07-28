#ifndef VW_AUDIO_BUFFER_H_
#define VW_AUDIO_BUFFER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Expected WAVE/RIFF format: PCM 16-bit mono at 16kHz
// Offset  Size  Value / meaning
// 0       4     "RIFF"
// 4       4     file_size - 8               (uint32 little-endian)
// 8       4     "WAVE"

// 12      4     "fmt "
// 16      4     16                          (fmt chunk size)
// 20      2     1                           (PCM integer format)
// 22      2     1                           (one channel / mono)
// 24      4     16000                       (sample rate)
// 28      4     32000                       (bytes per second)
// 32      2     2                           (bytes per sample frame)
// 34      2     16                          (bits per sample)

// 36      4     "data"
// 40      4     number_of_audio_bytes
// 44      ...   PCM samples: int16 little-endian

/////////////////////////////////////////////////////
// Raw structures defining a PCM audio file (RIFF/WAVE format) for reading and processing audio data
/////////////////////////////////////////////////////

// Structure holding normalized 32-bit floating point PCM audio samples
typedef struct {
  float* samples;        // Heap-allocated array of normalized float [-1.0f, +1.0f] samples
  size_t count;          // Total number of audio frames/samples
  uint32_t sample_rate;  // Audio sample rate in Hz (normalized to 16000)
  uint16_t channels;     // Audio channel count (normalized to 1 mono)
} vw_audio_pcm32_t;

// Standard RIFF file header (12 bytes)
typedef struct __attribute__((packed)) {
  char chunk_id[4];     // Must contain "RIFF" ASCII tag
  uint32_t chunk_size;  // Total file size in bytes minus 8
  char format[4];       // Must contain "WAVE" ASCII tag
} vw_riff_header_t;

// Sub-chunk 'fmt ' header defining audio stream dimensions
typedef struct __attribute__((packed)) {
  char subchunk_id[4];       // Must contain "fmt " ASCII tag
  uint32_t subchunk_size;    // Size of format chunk payload (16 bytes for standard PCM)
  uint16_t audio_format;     // Audio format tag: 1 = PCM integer, 3 = IEEE float
  uint16_t num_channels;     // Channel count (1 = mono, 2 = stereo)
  uint32_t sample_rate;      // Audio sampling frequency in Hz (e.g. 16000, 44100, 48000)
  uint32_t byte_rate;        // Byte rate = sample_rate * num_channels * bits_per_sample / 8
  uint16_t block_align;      // Block alignment = num_channels * bits_per_sample / 8
  uint16_t bits_per_sample;  // Bit depth per sample (16 for int16, 32 for float)
} vw_fmt_chunk_t;

// Generic sub-chunk header for stepping past metadata/list chunks
typedef struct __attribute__((packed)) {
  char subchunk_id[4];     // Sub-chunk identifier tag (e.g. "data", "LIST", "JUNK")
  uint32_t subchunk_size;  // Size of sub-chunk payload in bytes
} vw_chunk_header_t;

typedef struct vw_audio_buffer {
  size_t max_samples;
} vw_audio_buffer_t;

vw_audio_buffer_t* vw_audio_buffer_create(size_t max_samples);
void vw_audio_buffer_free(vw_audio_buffer_t* buf);
bool vw_audio_buffer_append_s16le(vw_audio_buffer_t* buf, const int16_t* pcm16, size_t sample_count, int64_t pts_us);

#endif  // VW_AUDIO_BUFFER_H_
