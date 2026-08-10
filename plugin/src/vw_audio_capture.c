#include "vw_audio_capture.h"

#include <string.h>

#include "vw_queue.h"

bool vw_audio_capture_process_block(vw_audio_capture_t* cap, const vw_audio_input_t* input) {
  if (!cap || !cap->queue || !input || !input->pcm_data || input->frame_count == 0) {
    return false;
  }

  if (input->sample_rate == 0 || input->channels == 0) {
    return false;
  }

  // Calculate total output frames needed at 16kHz with fractional sample remainder accumulation
  uint64_t total_samples_acc = ((uint64_t)input->frame_count * VW_AUDIO_TARGET_RATE) + cap->sample_remainder;
  size_t output_frames = (size_t)(total_samples_acc / input->sample_rate);
  cap->sample_remainder = (uint32_t)(total_samples_acc % input->sample_rate);

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

    vw_audio_chunk_t chunk = {0};
    chunk.start_pts_us = current_pts_us;
    // Derive duration from the byte count so the worker's strict validation holds exactly:
    // vw_protocol_validate_payload requires pcm_bytes == trunc(duration_us * 32 / 1000).
    // duration_us = bytes * 1000 / 32 = bytes * 31.25 must be rounded UP, otherwise odd byte
    // counts (odd frame counts) truncate to 2f-1 and fail validation by one byte.
    chunk.duration_us = ((int64_t)chunk_frames * 125 + 1) / 2;  // ceil(frames * 62.5)
    chunk.sample_rate = VW_AUDIO_TARGET_RATE;
    chunk.channels = 1;
    chunk.bytes = (uint32_t)(chunk_frames * sizeof(int16_t));

    int16_t* out_pcm = (int16_t*)chunk.pcm_data;

    // Resample / downmix input block into 16kHz Mono int16_t chunk
    for (size_t i = 0; i < chunk_frames; i++) {
      // Linear mapping of target index i back into input sample index
      size_t in_idx = (size_t)(((double)(output_frames_generated + i) * input->sample_rate) / VW_AUDIO_TARGET_RATE);
      if (in_idx >= input->frame_count) {
        in_idx = input->frame_count - 1;
      }

      int32_t sum = 0;
      for (uint32_t ch = 0; ch < input->channels; ch++) {
        size_t sample_idx = in_idx * input->channels + ch;
        if (input->format == VW_AUDIO_FORMAT_FL32) {
          float sample = ((const float*)input->pcm_data)[sample_idx];
          if (sample > 1.0f) sample = 1.0f;
          if (sample < -1.0f) sample = -1.0f;
          sum += (int32_t)(sample * 32767.0f);
        } else if (input->format == VW_AUDIO_FORMAT_S16) {
          sum += ((const int16_t*)input->pcm_data)[sample_idx];
        } else if (input->format == VW_AUDIO_FORMAT_S32) {
          int32_t s32 = ((const int32_t*)input->pcm_data)[sample_idx];
          sum += (s32 >> 16);
        }
      }

      int32_t mono_sample = sum / (int32_t)input->channels;
      if (mono_sample > 32767) mono_sample = 32767;
      if (mono_sample < -32768) mono_sample = -32768;
      out_pcm[i] = (int16_t)mono_sample;
    }

    // Enqueue chunk into SPSC ring buffer (non-blocking push)
    vw_spsc_queue_push(cap->queue, &chunk);

    output_frames_generated += chunk_frames;
    current_pts_us += chunk.duration_us;
  }

  cap->last_pts_us = current_pts_us;
  cap->total_samples_processed += output_frames;

  return true;
}
