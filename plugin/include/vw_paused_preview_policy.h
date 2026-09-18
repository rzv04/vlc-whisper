#ifndef VW_PAUSED_PREVIEW_POLICY_H_
#define VW_PAUSED_PREVIEW_POLICY_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

// Sender-owned, session-scoped state for the one-shot caption rendered after a seek while local playback is paused.
typedef struct vw_paused_preview_state {
  bool pending;
} vw_paused_preview_state_t;

static inline void vw_paused_preview_state_init(vw_paused_preview_state_t* state) {
  if (state) state->pending = false;
}

// Resets preview eligibility whenever the worker/source session identity changes.
static inline void vw_paused_preview_state_reset_session(vw_paused_preview_state_t* state) {
  if (state) state->pending = false;
}

// A pause transition may request source look-ahead only from a position sampled on that same transition.
static inline bool vw_paused_preview_capture_target(bool source_preview, int64_t sampled_position_us,
                                                    int64_t* out_target_us) {
  if (!source_preview || sampled_position_us < 0 || !out_target_us) return false;
  *out_target_us = sampled_position_us;
  return true;
}

static inline void vw_paused_preview_arm(vw_paused_preview_state_t* state) {
  if (state) state->pending = true;
}

// Consumes the one-shot preview token so only the first post-seek cue can replace the held caption.
static inline bool vw_paused_preview_take(vw_paused_preview_state_t* state) {
  if (!state || !state->pending) return false;
  state->pending = false;
  return true;
}

// Source look-ahead must remain active during a paused preview seek; live/non-preview pauses still propagate PAUSED.
static inline uint8_t vw_paused_preview_seek_flags(bool paused, bool preview_after_paused_seek) {
  uint8_t flags = VW_POSITION_FLAG_SEEK;
  if (paused && !preview_after_paused_seek) flags |= VW_POSITION_FLAG_PAUSED;
  return flags;
}

#endif  // VW_PAUSED_PREVIEW_POLICY_H_
