#include "vw_audio_capture.h"



bool vw_audio_capture_on_pcm_block(vw_audio_capture_t *cap, const int16_t *pcm_samples, size_t sample_count, int64_t pts_us) {
  (void)cap;
  (void)pcm_samples;
  (void)sample_count;
  (void)pts_us;
  return true;
}
