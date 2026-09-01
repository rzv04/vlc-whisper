// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

#ifndef VW_TRANSLATE_ASYNC_H_
#define VW_TRANSLATE_ASYNC_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

#define VW_TRANSLATE_ASYNC_ACTIVE_BUDGET 4U
#define VW_TRANSLATE_ASYNC_QUEUE_CAPACITY 32U

typedef struct vw_translate_async vw_translate_async_t;

typedef struct vw_translate_async_result {
  uint64_t epoch;
  vw_caption_segment_t segment;
  char source_text[VW_MAX_TEXT_BYTES + 1U];
  char translated_text[VW_MAX_TEXT_BYTES + 1U];
  bool attempted;
  bool success;
} vw_translate_async_result_t;

typedef void (*vw_translate_async_delivery_fn)(const vw_translate_async_result_t* result, void* user_data);

// Creates one background translator instance with a fixed worker pool sized to the active network budget. Network
// activity occurs exclusively on those background threads without blocking the worker loop.
vw_translate_async_t* vw_translate_async_create(void);

// Stops all background translation threads, drains any in-flight requests, and releases all synchronization mutexes
// and conditional variables.
void vw_translate_async_destroy(vw_translate_async_t* async);

// Advances internal playback epoch and drops all queued and in-flight translation jobs. Called immediately upon seek,
// session reset, or config change.
void vw_translate_async_invalidate(vw_translate_async_t* async);

// Enqueues a finalized caption segment into FIFO translation pipeline. Saturated work degrades to source text; a full
// bounded pipeline rejects only the newest cue, preserving the chronological order of accepted cues.
bool vw_translate_async_submit(vw_translate_async_t* async, const vw_caption_segment_t* segment,
                               const char* source_lang, const char* target_lang);

// Checks whether the next chronological translated or degraded completion is available without exposing a later cue
// while an earlier translation is still in flight.
bool vw_translate_async_has_result(vw_translate_async_t* async);

// Pops the next ordered caption completion without blocking. Rebinds internal string pointers to caller-owned result
// buffers.
bool vw_translate_async_try_pop(vw_translate_async_t* async, vw_translate_async_result_t* out);

// Removes and delivers the next ordered completion without holding its mutex across delivery. The worker main loop
// serializes this call with epoch-invalidating controls; background completion threads remain safe and nonblocking.
bool vw_translate_async_try_deliver(vw_translate_async_t* async, vw_translate_async_delivery_fn deliver,
                                    void* user_data);

#endif  // VW_TRANSLATE_ASYNC_H_
