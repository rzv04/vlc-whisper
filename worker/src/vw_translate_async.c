// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

#include "vw_translate_async.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_translate.h"

#define VW_TRANSLATE_ASYNC_RESULT_CAPACITY (VW_TRANSLATE_ASYNC_QUEUE_CAPACITY + 2U)

typedef struct vw_translate_job {
  uint64_t epoch;
  uint64_t ordinal;
  vw_caption_segment_t segment;
  char source_text[VW_MAX_TEXT_BYTES + 1U];
  char source_lang[16];
  char target_lang[16];
  bool skip_translation;
} vw_translate_job_t;

typedef struct vw_translate_async_completion {
  uint64_t ordinal;
  vw_translate_async_result_t result;
} vw_translate_async_completion_t;

struct vw_translate_async {
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  bool thread_started;
  bool stopping;
  uint64_t epoch;
  uint64_t next_submit_ordinal;
  uint64_t next_result_ordinal;

  vw_translate_job_t jobs[VW_TRANSLATE_ASYNC_QUEUE_CAPACITY];
  size_t job_head;
  size_t job_tail;
  size_t job_count;
  size_t inflight_count;
  size_t active_translation_count;

  vw_translate_async_completion_t results[VW_TRANSLATE_ASYNC_RESULT_CAPACITY];
  size_t result_head;
  size_t result_tail;
  size_t result_count;
};

static uint64_t vw_translate_async_next_ordinal(uint64_t ordinal) { return ordinal == UINT64_MAX ? 1U : ordinal + 1U; }

static void vw_translate_async_push_result_locked(vw_translate_async_t* async, uint64_t ordinal,
                                                  const vw_translate_async_result_t* result) {
  // Submission bounds jobs, in-flight work, and unconsumed completions to RESULT_CAPACITY. A completion replaces
  // one in-flight job, so this ring never needs to discard an earlier cue.
  if (async->result_count >= VW_TRANSLATE_ASYNC_RESULT_CAPACITY) return;
  async->results[async->result_head].ordinal = ordinal;
  async->results[async->result_head].result = *result;
  async->result_head = (async->result_head + 1U) % VW_TRANSLATE_ASYNC_RESULT_CAPACITY;
  async->result_count++;
}

static bool vw_translate_async_find_next_result_locked(const vw_translate_async_t* async, size_t* index_out) {
  size_t index = async->result_tail;
  for (size_t offset = 0; offset < async->result_count; offset++) {
    if (async->results[index].ordinal == async->next_result_ordinal) {
      *index_out = index;
      return true;
    }
    index = (index + 1U) % VW_TRANSLATE_ASYNC_RESULT_CAPACITY;
  }
  return false;
}

static void vw_translate_async_remove_result_locked(vw_translate_async_t* async, size_t completion_index) {
  size_t move_index = completion_index;
  size_t next_index = (move_index + 1U) % VW_TRANSLATE_ASYNC_RESULT_CAPACITY;
  while (next_index != async->result_head) {
    async->results[move_index] = async->results[next_index];
    move_index = next_index;
    next_index = (next_index + 1U) % VW_TRANSLATE_ASYNC_RESULT_CAPACITY;
  }
  async->result_head =
      (async->result_head + VW_TRANSLATE_ASYNC_RESULT_CAPACITY - 1U) % VW_TRANSLATE_ASYNC_RESULT_CAPACITY;
  async->result_count--;
  async->next_result_ordinal = vw_translate_async_next_ordinal(async->next_result_ordinal);
}

static void vw_translate_async_bind_result_text(vw_translate_async_result_t* result) {
  if (!result) return;
  result->segment.text_utf8 = result->source_text;
  result->segment.translation_attempted = result->attempted;
  if (result->success && result->translated_text[0]) {
    result->segment.translated_text_utf8 = result->translated_text;
    result->segment.translated_text_bytes = (uint16_t)strlen(result->translated_text);
  } else {
    result->segment.translated_text_utf8 = NULL;
    result->segment.translated_text_bytes = 0;
    result->segment.translation_tier = VW_TRANSLATE_TIER_NONE;
  }
}

static void* vw_translate_async_thread_main(void* opaque) {
  vw_translate_async_t* async = (vw_translate_async_t*)opaque;
  for (;;) {
    vw_translate_job_t job;
    pthread_mutex_lock(&async->mutex);
    while (!async->stopping && async->job_count == 0) pthread_cond_wait(&async->cond, &async->mutex);
    if (async->stopping && async->job_count == 0) {
      pthread_mutex_unlock(&async->mutex);
      break;
    }
    job = async->jobs[async->job_tail];
    async->job_tail = (async->job_tail + 1U) % VW_TRANSLATE_ASYNC_QUEUE_CAPACITY;
    async->job_count--;
    async->inflight_count++;
    pthread_mutex_unlock(&async->mutex);

    vw_translate_async_result_t result;
    memset(&result, 0, sizeof(result));
    result.epoch = job.epoch;
    result.segment = job.segment;
    memcpy(result.source_text, job.source_text, sizeof(result.source_text));
    result.segment.text_utf8 = result.source_text;
    result.segment.translated_text_utf8 = NULL;
    result.segment.translated_text_bytes = 0;
    result.segment.translation_tier = VW_TRANSLATE_TIER_NONE;
    result.segment.translation_latency_us = 0;
    result.attempted = !job.skip_translation;

    if (job.skip_translation) {
      result.success = false;
      vw_translate_async_bind_result_text(&result);
    } else {
      uint8_t tier = VW_TRANSLATE_TIER_NONE;
      uint32_t latency_us = 0;
      result.success = vw_translate_text(result.source_text, job.source_lang, job.target_lang, result.translated_text,
                                         sizeof(result.translated_text), &tier, &latency_us);
      result.segment.translation_latency_us = latency_us;
      result.segment.translation_tier = result.success ? tier : VW_TRANSLATE_TIER_NONE;
      vw_translate_async_bind_result_text(&result);
    }

    pthread_mutex_lock(&async->mutex);
    if (!job.skip_translation && job.epoch == async->epoch && async->active_translation_count > 0) {
      async->active_translation_count--;
    }
    if (!async->stopping && result.epoch == async->epoch) {
      vw_translate_async_push_result_locked(async, job.ordinal, &result);
    }
    async->inflight_count--;
    pthread_mutex_unlock(&async->mutex);
  }
  return NULL;
}

vw_translate_async_t* vw_translate_async_create(void) {
  vw_translate_async_t* async = (vw_translate_async_t*)calloc(1, sizeof(*async));
  if (!async) return NULL;
  async->epoch = 1;
  async->next_submit_ordinal = 1;
  async->next_result_ordinal = 1;
  if (pthread_mutex_init(&async->mutex, NULL) != 0) {
    free(async);
    return NULL;
  }
  if (pthread_cond_init(&async->cond, NULL) != 0) {
    pthread_mutex_destroy(&async->mutex);
    free(async);
    return NULL;
  }
  if (pthread_create(&async->thread, NULL, vw_translate_async_thread_main, async) != 0) {
    pthread_cond_destroy(&async->cond);
    pthread_mutex_destroy(&async->mutex);
    free(async);
    return NULL;
  }
  async->thread_started = true;
  return async;
}

void vw_translate_async_destroy(vw_translate_async_t* async) {
  if (!async) return;
  pthread_mutex_lock(&async->mutex);
  async->stopping = true;
  async->job_head = 0;
  async->job_tail = 0;
  async->job_count = 0;
  async->active_translation_count = 0;
  async->result_head = 0;
  async->result_tail = 0;
  async->result_count = 0;
  pthread_cond_broadcast(&async->cond);
  pthread_mutex_unlock(&async->mutex);
  if (async->thread_started) pthread_join(async->thread, NULL);
  pthread_cond_destroy(&async->cond);
  pthread_mutex_destroy(&async->mutex);
  free(async);
}

void vw_translate_async_invalidate(vw_translate_async_t* async) {
  if (!async) return;
  pthread_mutex_lock(&async->mutex);
  async->epoch = async->epoch == UINT64_MAX ? 1U : async->epoch + 1U;
  async->next_submit_ordinal = 1;
  async->next_result_ordinal = 1;
  async->job_head = 0;
  async->job_tail = 0;
  async->job_count = 0;
  async->result_head = 0;
  async->result_tail = 0;
  async->result_count = 0;
  async->active_translation_count = 0;
  pthread_mutex_unlock(&async->mutex);
}

bool vw_translate_async_submit(vw_translate_async_t* async, const vw_caption_segment_t* segment,
                               const char* source_lang, const char* target_lang) {
  if (!async || !segment || !segment->text_utf8 || !segment->text_utf8[0]) return false;
  size_t text_len = strlen(segment->text_utf8);
  if (text_len == 0 || text_len > VW_MAX_TEXT_BYTES) return false;

  pthread_mutex_lock(&async->mutex);
  if (async->stopping) {
    pthread_mutex_unlock(&async->mutex);
    return false;
  }

  // Bound every accepted cue, including completed cues not yet consumed and the one network request currently
  // in flight. Rejection drops only the newest input cue; it never exposes a newer caption ahead of older work.
  if (async->job_count + async->inflight_count + async->result_count >= VW_TRANSLATE_ASYNC_RESULT_CAPACITY) {
    pthread_mutex_unlock(&async->mutex);
    return false;
  }

  // When the active network budget is saturated, degrade this cue to source text (skip network translation)
  // while preserving exact chronological cue order in the FIFO stream.
  bool skip = (async->active_translation_count >= VW_TRANSLATE_ASYNC_ACTIVE_BUDGET);

  if (async->job_count >= VW_TRANSLATE_ASYNC_QUEUE_CAPACITY) {
    // Hard queue limit reached: complete the oldest pending unprocessed job as source text. Ordinal-aware popping
    // withholds it until any older in-flight cue has completed.
    vw_translate_job_t* oldest = &async->jobs[async->job_tail];
    async->job_tail = (async->job_tail + 1U) % VW_TRANSLATE_ASYNC_QUEUE_CAPACITY;
    async->job_count--;
    if (!oldest->skip_translation && async->active_translation_count > 0) {
      async->active_translation_count--;
    }

    vw_translate_async_result_t evicted;
    memset(&evicted, 0, sizeof(evicted));
    evicted.epoch = oldest->epoch;
    evicted.segment = oldest->segment;
    memcpy(evicted.source_text, oldest->source_text, sizeof(evicted.source_text));
    evicted.segment.text_utf8 = evicted.source_text;
    evicted.attempted = true;
    evicted.success = false;
    vw_translate_async_bind_result_text(&evicted);

    vw_translate_async_push_result_locked(async, oldest->ordinal, &evicted);
  }

  vw_translate_job_t* job = &async->jobs[async->job_head];
  memset(job, 0, sizeof(*job));
  job->epoch = async->epoch;
  job->ordinal = async->next_submit_ordinal;
  async->next_submit_ordinal = vw_translate_async_next_ordinal(async->next_submit_ordinal);
  job->segment = *segment;
  job->skip_translation = skip;
  if (!skip) async->active_translation_count++;
  memcpy(job->source_text, segment->text_utf8, text_len);
  job->source_text[text_len] = '\0';
  job->segment.text_utf8 = job->source_text;
  job->segment.text_bytes = (uint16_t)text_len;
  job->segment.translated_text_utf8 = NULL;
  job->segment.translated_text_bytes = 0;
  snprintf(job->source_lang, sizeof(job->source_lang), "%s", source_lang && source_lang[0] ? source_lang : "auto");
  snprintf(job->target_lang, sizeof(job->target_lang), "%s", target_lang && target_lang[0] ? target_lang : "en");
  async->job_head = (async->job_head + 1U) % VW_TRANSLATE_ASYNC_QUEUE_CAPACITY;
  async->job_count++;
  pthread_cond_signal(&async->cond);
  pthread_mutex_unlock(&async->mutex);
  return true;
}

bool vw_translate_async_has_result(vw_translate_async_t* async) {
  if (!async) return false;
  pthread_mutex_lock(&async->mutex);
  size_t index = 0;
  bool has_result = vw_translate_async_find_next_result_locked(async, &index);
  pthread_mutex_unlock(&async->mutex);
  return has_result;
}

bool vw_translate_async_try_pop(vw_translate_async_t* async, vw_translate_async_result_t* out) {
  if (!async || !out) return false;
  pthread_mutex_lock(&async->mutex);
  if (async->result_count == 0) {
    pthread_mutex_unlock(&async->mutex);
    return false;
  }
  size_t completion_index = 0;
  if (!vw_translate_async_find_next_result_locked(async, &completion_index)) {
    pthread_mutex_unlock(&async->mutex);
    return false;
  }

  *out = async->results[completion_index].result;
  vw_translate_async_remove_result_locked(async, completion_index);
  pthread_mutex_unlock(&async->mutex);
  vw_translate_async_bind_result_text(out);
  return true;
}

bool vw_translate_async_try_deliver(vw_translate_async_t* async, vw_translate_async_delivery_fn deliver,
                                    void* user_data) {
  if (!async || !deliver) return false;
  pthread_mutex_lock(&async->mutex);
  size_t completion_index = 0;
  if (!vw_translate_async_find_next_result_locked(async, &completion_index)) {
    pthread_mutex_unlock(&async->mutex);
    return false;
  }

  vw_translate_async_result_t result = async->results[completion_index].result;
  vw_translate_async_bind_result_text(&result);
  deliver(&result, user_data);
  vw_translate_async_remove_result_locked(async, completion_index);
  pthread_mutex_unlock(&async->mutex);
  return true;
}
