// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.
//
// vw_sample_whisper_pcm.c - Sample application demonstrating standalone WAV audio reading,
// sample rate normalization (resampling to 16kHz), mono downmixing, Whisper engine setup,
// and microsecond PTS timestamped transcription output.

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

// Usage: ./sample_vw_sample_whisper_pcm <path_to_ggml_model> <path_to_wav_audio>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <whisper.h>

#include "vw_audio_buffer.h"

// Default file paths for model weights and test audio fixtures
#define VW_DEFAULT_MODEL_PATH "models/ggml-tiny.en.bin"
#define VW_DEFAULT_WAV_PATH "samples/audio/output.wav"

// Whisper model input invariants: 16kHz sample rate, 1 channel (mono)
#define VW_EXPECTED_SAMPLE_RATE WHISPER_SAMPLE_RATE
#define VW_EXPECTED_NUM_CHANNELS 1

// Reads a WAV file (16-bit signed PCM or 32-bit float PCM), downmixes multi-channel audio to mono,
// and resamples to 16000 Hz if necessary using linear interpolation.
static bool vw_read_wav_pcm32(const char* file_path, vw_audio_pcm32_t* out_pcm) {
  if (file_path == NULL || out_pcm == NULL) {
    return false;
  }

  // Open WAV binary file for reading
  FILE* file = fopen(file_path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Error: Failed to open WAV file '%s'\n", file_path);
    return false;
  }

  // 1. Read and validate main RIFF/WAVE header
  vw_riff_header_t riff;
  if (fread(&riff, 1, sizeof(riff), file) != sizeof(riff)) {
    fprintf(stderr, "Error: Failed to read RIFF header from '%s'\n", file_path);
    fclose(file);
    return false;
  }

  if (memcmp(riff.chunk_id, "RIFF", 4) != 0 || memcmp(riff.format, "WAVE", 4) != 0) {
    fprintf(stderr, "Error: '%s' is not a valid RIFF/WAVE file\n", file_path);
    fclose(file);
    return false;
  }

  vw_fmt_chunk_t fmt = {0};
  bool fmt_found = false;
  long data_offset = 0;
  uint32_t data_bytes = 0;

  // 2. Iterate WAV sub-chunks sequentially to locate 'fmt ' and 'data' chunks
  while (!feof(file)) {
    vw_chunk_header_t chunk;
    if (fread(&chunk, 1, sizeof(chunk), file) != sizeof(chunk)) {
      break;
    }

    if (memcmp(chunk.subchunk_id, "fmt ", 4) == 0) {
      // Parse format sub-chunk
      if (chunk.subchunk_size < 16) {
        fprintf(stderr, "Error: Invalid fmt chunk size in '%s'\n", file_path);
        fclose(file);
        return false;
      }
      memcpy(fmt.subchunk_id, chunk.subchunk_id, 4);
      fmt.subchunk_size = chunk.subchunk_size;
      if (fread(&fmt.audio_format, 1, sizeof(vw_fmt_chunk_t) - 8, file) != (sizeof(vw_fmt_chunk_t) - 8)) {
        fprintf(stderr, "Error: Failed to read fmt chunk payload in '%s'\n", file_path);
        fclose(file);
        return false;
      }
      // Skip extended format bytes if present
      if (chunk.subchunk_size > 16) {
        fseek(file, chunk.subchunk_size - 16, SEEK_CUR);
      }
      fmt_found = true;
    } else if (memcmp(chunk.subchunk_id, "data", 4) == 0) {
      // Located audio payload chunk
      data_offset = ftell(file);
      data_bytes = chunk.subchunk_size;
      break;
    } else {
      // Skip unrecognized metadata or padding chunks (LIST, INFO, etc.)
      fseek(file, chunk.subchunk_size, SEEK_CUR);
    }
  }

  if (!fmt_found || data_offset == 0) {
    fprintf(stderr, "Error: Could not locate fmt or data chunk in '%s'\n", file_path);
    fclose(file);
    return false;
  }

  // Warn if source sample rate differs from 16kHz
  if (fmt.sample_rate != VW_EXPECTED_SAMPLE_RATE) {
    fprintf(stderr, "Warning: WAV sample rate is %u Hz (Whisper expects %u Hz)\n", fmt.sample_rate,
            VW_EXPECTED_SAMPLE_RATE);
  }

  // Verify supported encoding (PCM 1 or IEEE Float 3)
  if (fmt.audio_format != 1 && fmt.audio_format != 3) {
    fprintf(stderr, "Error: Unsupported WAV audio format %u (only PCM 1 or float 3 supported)\n", fmt.audio_format);
    fclose(file);
    return false;
  }

  uint16_t bytes_per_sample = fmt.bits_per_sample / 8;
  if (bytes_per_sample == 0 || fmt.num_channels == 0) {
    fprintf(stderr, "Error: Invalid audio dimensions in '%s'\n", file_path);
    fclose(file);
    return false;
  }

  size_t total_frame_samples = data_bytes / bytes_per_sample;
  size_t frame_count = total_frame_samples / fmt.num_channels;

  // Allocate buffer for output float audio frames
  float* pcm_buffer = (float*)malloc(frame_count * sizeof(float));
  if (pcm_buffer == NULL) {
    fprintf(stderr, "Error: Failed to allocate PCM sample buffer\n");
    fclose(file);
    return false;
  }

  fseek(file, data_offset, SEEK_SET);

  // 3. Read raw PCM audio samples and downmix channels into mono float buffer
  if (fmt.audio_format == 1 && fmt.bits_per_sample == 16) {
    // Read 16-bit signed integer PCM
    int16_t* raw_buf = (int16_t*)malloc(data_bytes);
    if (raw_buf == NULL) {
      free(pcm_buffer);
      fclose(file);
      return false;
    }
    size_t read_bytes = fread(raw_buf, 1, data_bytes, file);
    size_t read_samples = read_bytes / sizeof(int16_t);
    size_t actual_frames = read_samples / fmt.num_channels;

    // Convert int16 samples to float [-1.0f, +1.0f] and average across channels
    for (size_t i = 0; i < actual_frames; ++i) {
      float sum = 0.0f;
      for (uint16_t ch = 0; ch < fmt.num_channels; ++ch) {
        sum += (float)raw_buf[i * fmt.num_channels + ch] / 32768.0f;
      }
      pcm_buffer[i] = sum / (float)fmt.num_channels;
    }
    free(raw_buf);
    frame_count = actual_frames;
  } else if (fmt.audio_format == 3 && fmt.bits_per_sample == 32) {
    // Read 32-bit IEEE float PCM
    float* raw_buf = (float*)malloc(data_bytes);
    if (raw_buf == NULL) {
      free(pcm_buffer);
      fclose(file);
      return false;
    }

    size_t read_bytes = fread(raw_buf, 1, data_bytes, file);
    size_t read_samples = read_bytes / sizeof(float);
    size_t actual_frames = read_samples / fmt.num_channels;

    // Average float channels into mono output
    for (size_t i = 0; i < actual_frames; ++i) {
      float sum = 0.0f;
      for (uint16_t ch = 0; ch < fmt.num_channels; ++ch) {
        sum += raw_buf[i * fmt.num_channels + ch];
      }
      pcm_buffer[i] = sum / (float)fmt.num_channels;
    }
    free(raw_buf);
    frame_count = actual_frames;
  } else {
    fprintf(stderr, "Error: Unsupported bit depth %u in '%s'\n", fmt.bits_per_sample, file_path);
    free(pcm_buffer);
    fclose(file);
    return false;
  }

  fclose(file);

  // 4. Perform linear interpolation resampling to 16000 Hz if source rate differs
  if (fmt.sample_rate != VW_EXPECTED_SAMPLE_RATE && frame_count > 1) {
    printf("[vw_sample] Resampling from %u Hz to %u Hz...\n", fmt.sample_rate, VW_EXPECTED_SAMPLE_RATE);
    size_t target_count = (size_t)((double)frame_count * (double)VW_EXPECTED_SAMPLE_RATE / (double)fmt.sample_rate);
    if (target_count > 0) {
      float* resampled = (float*)malloc(target_count * sizeof(float));
      if (resampled != NULL) {
        double ratio = (double)(frame_count - 1) / (target_count > 1 ? (double)(target_count - 1) : 1.0);
        for (size_t i = 0; i < target_count; ++i) {
          double src_idx = (double)i * ratio;
          size_t idx0 = (size_t)src_idx;
          size_t idx1 = (idx0 + 1 < frame_count) ? idx0 + 1 : idx0;
          double frac = src_idx - (double)idx0;
          resampled[i] = (float)((1.0 - frac) * pcm_buffer[idx0] + frac * pcm_buffer[idx1]);
        }
        free(pcm_buffer);
        pcm_buffer = resampled;
        frame_count = target_count;
      }
    }
  }

  // Populate output struct
  out_pcm->samples = pcm_buffer;
  out_pcm->count = frame_count;
  out_pcm->sample_rate = VW_EXPECTED_SAMPLE_RATE;
  out_pcm->channels = 1;

  return true;
}

// Formats microsecond media PTS (int64_t pts_us) into HH:MM:SS.mmm display string
static void vw_format_pts_us(int64_t pts_us, char* out_buf, size_t buf_size) {
  if (pts_us < 0) {
    pts_us = 0;
  }
  int64_t total_ms = pts_us / 1000;
  int64_t ms = total_ms % 1000;
  int64_t total_sec = total_ms / 1000;
  int64_t sec = total_sec % 60;
  int64_t total_min = total_sec / 60;
  int64_t min = total_min % 60;
  int64_t hours = total_min / 60;

  snprintf(out_buf, buf_size, "%02ld:%02ld:%02ld.%03ld", (long)hours, (long)min, (long)sec, (long)ms);
}

int main(int argc, char** argv) {
  // Ensure unbuffered stdout/stderr for immediate console output
  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  // Accept optional command-line arguments for model path and input WAV file path
  const char* model_path = (argc > 1 && argv[1][0] != '\0') ? argv[1] : VW_DEFAULT_MODEL_PATH;
  const char* wav_path = (argc > 2 && argv[2][0] != '\0') ? argv[2] : VW_DEFAULT_WAV_PATH;

  printf("[vw_sample] Loading model: %s\n", model_path);
  printf("[vw_sample] Reading WAV audio: %s\n", wav_path);

  // Read and normalize audio samples from WAV file
  vw_audio_pcm32_t pcm = {0};
  if (!vw_read_wav_pcm32(wav_path, &pcm)) {
    fprintf(stderr, "[vw_sample] Error: Failed to load WAV audio from '%s'\n", wav_path);
    return 1;
  }

  printf("[vw_sample] Audio loaded: %zu samples (%u Hz)\n", pcm.count, pcm.sample_rate);

  // Initialize Whisper context from GGML binary model
  struct whisper_context_params cparams = whisper_context_default_params();
  struct whisper_context* ctx = whisper_init_from_file_with_params(model_path, cparams);
  if (ctx == NULL) {
    fprintf(stderr, "[vw_sample] Error: Failed to initialize Whisper context from '%s'\n", model_path);
    free(pcm.samples);
    return 1;
  }

  // Configure inference parameters (greedy sampling, 4 threads, English language)
  struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  wparams.print_realtime = false;
  wparams.print_progress = false;
  wparams.print_timestamps = false;
  wparams.print_special = false;
  wparams.language = "en";
  wparams.n_threads = 4;

  printf("[vw_sample] Running Whisper transcription...\n");
  if (whisper_full(ctx, wparams, pcm.samples, (int)pcm.count) != 0) {
    fprintf(stderr, "[vw_sample] Error: Failed to run Whisper transcription\n");
    whisper_free(ctx);
    free(pcm.samples);
    return 1;
  }

  // Retrieve transcription text segments and convert timestamps to signed microsecond PTS (int64_t pts_us)
  int n_segments = whisper_full_n_segments(ctx);
  printf("[vw_sample] Transcription complete. Segments found: %d\n", n_segments);

  for (int i = 0; i < n_segments; ++i) {
    const char* text = whisper_full_get_segment_text(ctx, i);
    int64_t t0 = whisper_full_get_segment_t0(ctx, i);  // Timestamp in 10ms units
    int64_t t1 = whisper_full_get_segment_t1(ctx, i);

    // Convert 10ms units to microsecond media timestamps (pts_us)
    int64_t start_pts_us = t0 * 10000;
    int64_t end_pts_us = t1 * 10000;

    char start_str[32];
    char end_str[32];
    vw_format_pts_us(start_pts_us, start_str, sizeof(start_str));
    vw_format_pts_us(end_pts_us, end_str, sizeof(end_str));

    printf("[%s -> %s] (pts_us: %ld -> %ld) %s\n", start_str, end_str, (long)start_pts_us, (long)end_pts_us, text);
  }

  // Clean up allocated memory and Whisper context
  whisper_free(ctx);
  free(pcm.samples);
  return 0;
}
