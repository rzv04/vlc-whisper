// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

#ifndef VW_TRANSLATE_ASYNC_H_
#define VW_TRANSLATE_ASYNC_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

#define VW_TRANSLATE_ASYNC_QUEUE_CAPACITY 4U

typedef struct vw_translate_async vw_translate_async_t;

typedef struct vw_translate_async_result {
  uint64_t epoch;
  vw_caption_segment_t segment;
  char source_text[VW_MAX_TEXT_BYTES + 1U];
  char translated_text[VW_MAX_TEXT_BYTES + 1U];
  bool attempted;
  bool success;
} vw_translate_async_result_t;

// Creates one background translator with a maximum of four pending jobs. Network work happens only on that thread.
vw_translate_async_t* vw_translate_async_create(void);

// Stops the translation thread, waits for any bounded in-flight request, and releases all synchronization resources.
void vw_translate_async_destroy(vw_translate_async_t* async);

// Advances the playback/config epoch and immediately drops all queued/completed jobs from older epochs. An in-flight
// request cannot be force-cancelled portably, but its completion is discarded before it becomes observable.
void vw_translate_async_advance_epoch(vw_translate_async_t* async, uint64_t epoch);

// Copies an immutable finalized caption into the bounded queue. Returns false when translation is unavailable or the
// pending queue is saturated; callers should emit the source caption immediately in that case.
bool vw_translate_async_submit(vw_translate_async_t* async, const vw_caption_segment_t* segment, uint64_t epoch,
                               const char* source_lang, const char* target_lang);

// Returns true when at least one translated/fallback completion can be popped without blocking.
bool vw_translate_async_has_result(vw_translate_async_t* async);

// Pops one completion without blocking. Text pointers in out->segment are rebound to out-owned buffers.
bool vw_translate_async_try_pop(vw_translate_async_t* async, vw_translate_async_result_t* out);

#endif  // VW_TRANSLATE_ASYNC_H_
