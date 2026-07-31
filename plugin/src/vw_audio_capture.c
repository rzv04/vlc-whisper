#include "vw_audio_capture.h"

#include <string.h>

#include "vw_queue.h"

bool vw_audio_capture_process_block(vw_audio_capture_t* cap, const vw_audio_input_t* input) {
  if (!cap || !cap->queue || !input || !input->pcm_data || input->frame_count == 0) {
    return false;
  }

  // Guard against division by zero
  if (input->sample_rate == 0) {
    return false;
  }

  // Calculate total output frames needed at 16kHz
  size_t output_frames = (input->frame_count * VW_AUDIO_TARGET_RATE) / input->sample_rate;
  if (output_frames == 0) {
    return true;  // Nothing to generate
  }

  const size_t max_out_frames_per_chunk = VW_AUDIO_CHUNK_MAX_PCM_BYTES / sizeof(int16_t);
  size_t output_frames_generated = 0;
  int64_t current_pts_us = input->pts_us;

  while (output_frames_generated < output_frames) {
    size_t chunk_frames = output_frames - output_frames_generated;
    if (chunk_frames > max_out_frames_per_chunk) {
      chunk_frames = max_out_frames_per_chunk;
    }

    vw_audio_chunk_t chunk;
    chunk.start_pts_us = current_pts_us;
    chunk.duration_us = ((int64_t)chunk_frames * 1000000LL) / VW_AUDIO_TARGET_RATE;
    chunk.sample_rate = VW_AUDIO_TARGET_RATE;
    chunk.channels = 1;
    chunk.bytes = (uint32_t)(chunk_frames * sizeof(int16_t));

    int16_t* out_pcm = (int16_t*)chunk.pcm_data;

    // Inline resampler (boxcar filter) and downmixer (average all channels)
    for (size_t i = 0; i < chunk_frames; ++i) {
      size_t global_out_idx = output_frames_generated + i;

      size_t in_start = (global_out_idx * input->sample_rate) / VW_AUDIO_TARGET_RATE;
      size_t in_end = ((global_out_idx + 1) * input->sample_rate) / VW_AUDIO_TARGET_RATE;
      if (in_end == in_start) {
        in_end = in_start + 1;  // Upsampling/nearest-neighbor fallback
      }
      if (in_end > input->frame_count) {
        in_end = input->frame_count;
      }

      float sum = 0.0f;
      size_t count = 0;

      for (size_t in_idx = in_start; in_idx < in_end; ++in_idx) {
        for (uint32_t c = 0; c < input->channels; ++c) {
          float sample = 0.0f;
          if (input->format == VW_AUDIO_FORMAT_FL32) {
            sample = ((const float*)input->pcm_data)[in_idx * input->channels + c];
          } else if (input->format == VW_AUDIO_FORMAT_S16) {
            sample = ((const int16_t*)input->pcm_data)[in_idx * input->channels + c] / 32768.0f;
          } else if (input->format == VW_AUDIO_FORMAT_S32) {
            sample = ((const int32_t*)input->pcm_data)[in_idx * input->channels + c] / 2147483648.0f;
          }
          sum += sample;
          count++;
        }
      }

      float avg = count > 0 ? (sum / count) : 0.0f;
      if (avg > 1.0f) avg = 1.0f;
      if (avg < -1.0f) avg = -1.0f;

      out_pcm[i] = (int16_t)(avg * 32767.0f);
    }

    vw_spsc_queue_push(cap->queue, &chunk);

    output_frames_generated += chunk_frames;
    current_pts_us += chunk.duration_us;
    cap->last_pts_us = current_pts_us;
    cap->total_samples_processed += chunk_frames;
  }

  return true;
}
