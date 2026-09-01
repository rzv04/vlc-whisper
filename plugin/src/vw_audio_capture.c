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

  uint32_t target_rate = cap->target_sample_rate ? cap->target_sample_rate : VW_AUDIO_TARGET_RATE;
  if (target_rate == 0) {
    return false;
  }

  // Callback-side reset requested by sender (discontinuity/seek). No cross-thread field writes.
  if (cap->reset_pending && atomic_load(cap->reset_pending)) {
    cap->resample_acc = 0;
    cap->sample_remainder = 0;
    cap->total_input_frames = 0;
    cap->resample_source_rate = 0;
    cap->last_pts_us = 0;
    atomic_store(cap->reset_pending, false);
  }

  // A source-rate change starts a new rational conversion phase.
  if (cap->resample_source_rate != input->sample_rate) {
    cap->resample_source_rate = input->sample_rate;
    cap->resample_acc = input->sample_rate > target_rate ? input->sample_rate - target_rate : 0;
  }

  uint64_t acc = cap->resample_acc;
  const size_t max_out_frames_per_chunk = VW_AUDIO_CHUNK_MAX_PCM_BYTES / sizeof(int16_t);
  size_t total_emitted = 0;
  int64_t current_pts_us = input->pts_us;

  vw_audio_chunk_t chunk = {0};
  chunk.start_pts_us = current_pts_us;
  chunk.sample_rate = target_rate;
  chunk.channels = 1;
  int16_t* out_pcm = (int16_t*)chunk.pcm_data;
  size_t chunk_frames = 0;

  for (size_t in_idx = 0; in_idx < input->frame_count; ++in_idx) {
    acc += target_rate;
    while (acc >= input->sample_rate) {
      acc -= input->sample_rate;

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
      out_pcm[chunk_frames++] = (int16_t)mono_sample;
      total_emitted++;

      if (chunk_frames == max_out_frames_per_chunk) {
        chunk.bytes = (uint32_t)(chunk_frames * sizeof(int16_t));
        chunk.duration_us = ((int64_t)chunk_frames * 1000000LL + target_rate - 1) / target_rate;
        vw_spsc_queue_push(cap->queue, &chunk);
        current_pts_us += chunk.duration_us;
        chunk.start_pts_us = current_pts_us;
        chunk.sample_rate = target_rate;
        chunk.channels = 1;
        chunk_frames = 0;
      }
    }
  }

  if (chunk_frames > 0) {
    chunk.bytes = (uint32_t)(chunk_frames * sizeof(int16_t));
    chunk.duration_us = ((int64_t)chunk_frames * 1000000LL + target_rate - 1) / target_rate;
    vw_spsc_queue_push(cap->queue, &chunk);
    current_pts_us += chunk.duration_us;
  }

  cap->resample_acc = (uint32_t)acc;
  cap->sample_remainder = (uint32_t)acc;
  cap->total_input_frames += input->frame_count;
  cap->last_pts_us = current_pts_us;
  cap->total_samples_processed += total_emitted;

  return true;
}
