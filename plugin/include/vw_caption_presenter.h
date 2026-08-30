#ifndef VW_CAPTION_PRESENTER_H_
#define VW_CAPTION_PRESENTER_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

// Minimum subtitle display duration floor (1.0 second = 1,000,000 microseconds) to eliminate unreadable sub-second
// flash cues.
#define VW_CAPTION_MIN_DISPLAY_DURATION_US 1000000LL
#define VW_MODEL_PROGRESS_DISPLAY_DURATION_US 2000000LL

#define VW_PRESENTER_MAX_TEXT_BYTES (VW_MAX_TEXT_BYTES + 1U)

typedef struct vw_caption_presenter {
  void* p_filter_ctx;
  void* p_held_vout;   // Retained vout reference ensuring lifetime safety and protecting against pointer address reuse.
  int spu_channel_id;  // Registered VLC SPU channel ID; -1 when unregistered or unavailable.
  bool spu_channel_registered;             // True if spu_channel_id >= 0 and successfully registered with current vout.
  void* p_model_progress_held_vout;        // Retained vout reference for the independent model-progress channel.
  int model_progress_channel_id;           // Dedicated model-download SPU channel; -1 when unavailable.
  bool model_progress_channel_registered;  // True when the model-progress channel belongs to the current vout.
  bool has_pending;                        // True if a pending caption cue is buffered for lookahead pacing.
  vw_caption_segment_t pending_segment;    // Buffered segment awaiting successor boundary for overlap clipping.
  char pending_text[VW_PRESENTER_MAX_TEXT_BYTES];             // Static buffer storing pending segment text.
  char pending_translated_text[VW_PRESENTER_MAX_TEXT_BYTES];  // Static buffer storing pending translated text.
} vw_caption_presenter_t;

// Renders fallback caption text via OSD on the active vout surface when SPU is unavailable,
// safely locating the target vout thread through the parent filter hierarchy without blocking.
bool vw_caption_presenter_display(void* p_filter, const char* text, int64_t duration_us);

// Renders a worker model-progress message on a dedicated wall-clock SPU channel, surviving pause/seek caption blanking
// without altering existing caption timing.
bool vw_caption_presenter_show_model_progress(vw_caption_presenter_t* presenter,
                                              const vw_msg_model_progress_t* progress);

// Flushes and releases only the dedicated model-download SPU channel, preserving normal caption presentation state
// during pause and seek operations without touching active caption cues.
void vw_caption_presenter_clear_model_progress(vw_caption_presenter_t* presenter);

// Buffers a timed cue and dispatches its predecessor, applying media-position lead only when media_timeline confirms
// both timestamps share VLC's media clock domain.
bool vw_caption_presenter_show_segment(vw_caption_presenter_t* presenter, const vw_caption_segment_t* segment,
                                       int64_t input_time_us, bool media_timeline);

// Flushes the pending cue, using input_time_us for future lead only when media_timeline identifies source-mode media
// timestamps; live system-date cues render immediately.
bool vw_caption_presenter_flush(vw_caption_presenter_t* presenter, int64_t input_time_us, bool media_timeline);

// Blanks currently displayed caption overlays by flushing both private SPU and OSD channels while preserving filter
// context, guaranteeing clean subtitle erasure across seek jumps before upcoming segments arrive.
void vw_caption_presenter_blank(vw_caption_presenter_t* presenter);

// Flushes active caption channels, releases any held video output references, and resets presenter filter context
// exclusively during module teardown to ensure leak-free destruction without disrupting ongoing video playback.
void vw_caption_presenter_clear(vw_caption_presenter_t* presenter);

#endif  // VW_CAPTION_PRESENTER_H_
