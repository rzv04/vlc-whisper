#ifndef VW_SOURCE_DECODER_H_
#define VW_SOURCE_DECODER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque source decoder handle wrapping platform demuxers (Media Foundation on Windows, FFmpeg on Linux).
typedef struct vw_source_decoder vw_source_decoder_t;

// Stream metadata populated upon successfully opening a media file container.
typedef struct vw_source_decoder_info {
  int64_t duration_us;        // Media duration in microseconds (-1 if unknown or live)
  uint32_t sample_rate;       // Native audio track sample rate (Hz)
  uint32_t channels;          // Native audio channel count
  char container_format[32];  // Container format string descriptor
} vw_source_decoder_info_t;

// Opens a local media file URL or file path, configuring platform demuxers and resamplers to decode audio
// streams directly into 16kHz mono 16-bit PCM for ahead-of-time transcription.
vw_source_decoder_t* vw_source_decoder_open(const char* url, vw_source_decoder_info_t* info);

// Repositions the active media stream reader to the specified timestamp in microseconds, flushing internal decoder
// buffers and re-anchoring presentation timestamps without tearing down the underlying demuxer context.
bool vw_source_decoder_seek(vw_source_decoder_t* decoder, int64_t target_pts_us);

// Reads decoded and resampled 16kHz 16-bit mono PCM audio samples into the destination buffer, returning sample
// count read along with presentation timestamp in microseconds for the first sample.
size_t vw_source_decoder_read_s16le(vw_source_decoder_t* decoder, int16_t* out_pcm, size_t max_samples,
                                    int64_t* out_pts_us);

// Queries the total duration of the currently opened media file in microseconds, returning negative one if
// duration is indeterminate, stream is live, or decoder handle is invalid.
int64_t vw_source_decoder_get_duration_us(const vw_source_decoder_t* decoder);

// Closes the source decoder instance, freeing all internal decoder contexts, stream readers, buffers, and
// platform media resources safely without affecting global media subsystem lifetimes.
void vw_source_decoder_close(vw_source_decoder_t* decoder);

#ifdef __cplusplus
}
#endif

#endif  // VW_SOURCE_DECODER_H_
