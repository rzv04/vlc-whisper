// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

#include "vw_translate_async.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "vw_translate.h"

#define VW_TRANSLATE_ASYNC_RESULT_CAPACITY (VW_TRANSLATE_ASYNC_QUEUE_CAPACITY + 2U)

typedef struct vw_translate_job {
  uint64_t epoch;
  vw_caption_segment_t segment;
  char source_text[VW_MAX_TEXT_BYTES + 1U];
  char source_lang[16];
  char target_lang[16];
} vw_translate_job_t;

struct vw_translate_async {
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  bool thread_started;
  bool stopping;
  uint64_t min_epoch;

  vw_translate_job_t jobs[VW_TRANSLATE_ASYNC_QUEUE_CAPACITY];
  size_t job_head;
  size_t job_tail;
  size_t job_count;

  vw_translate_async_result_t results[VW_TRANSLATE_ASYNC_RESULT_CAPACITY];
  size_t result_head;
  size_t result_tail;
  size_t result_count;
};

static void vw_translate_async_bind_result_text(vw_translate_async_result_t* result) {
  if (!result) return;
  result->segment.text_utf8 = result->source_text;
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
    result.attempted = true;

    uint8_t tier = VW_TRANSLATE_TIER_NONE;
    uint32_t latency_us = 0;
    result.success = vw_translate_text(result.source_text, job.source_lang, job.target_lang, result.translated_text,
                                       sizeof(result.translated_text), &tier, &latency_us);
    result.segment.translation_latency_us = latency_us;
    result.segment.translation_tier = result.success ? tier : VW_TRANSLATE_TIER_NONE;
    vw_translate_async_bind_result_text(&result);

    pthread_mutex_lock(&async->mutex);
    if (!async->stopping && result.epoch >= async->min_epoch) {
      if (async->result_count == VW_TRANSLATE_ASYNC_RESULT_CAPACITY) {
        // This should be unreachable with one translator and <=4 pending jobs. Preserve newest playback state if a
        // stalled consumer nevertheless fills the completion ring.
        async->result_tail = (async->result_tail + 1U) % VW_TRANSLATE_ASYNC_RESULT_CAPACITY;
        async->result_count--;
      }
      async->results[async->result_head] = result;
      async->result_head = (async->result_head + 1U) % VW_TRANSLATE_ASYNC_RESULT_CAPACITY;
      async->result_count++;
    }
    pthread_mutex_unlock(&async->mutex);
  }
  return NULL;
}

vw_translate_async_t* vw_translate_async_create(void) {
  vw_translate_async_t* async = (vw_translate_async_t*)calloc(1, sizeof(*async));
  if (!async) return NULL;
  async->min_epoch = 1;
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

void vw_translate_async_advance_epoch(vw_translate_async_t* async, uint64_t epoch) {
  if (!async) return;
  pthread_mutex_lock(&async->mutex);
  async->min_epoch = epoch;
  async->job_head = 0;
  async->job_tail = 0;
  async->job_count = 0;
  async->result_head = 0;
  async->result_tail = 0;
  async->result_count = 0;
  pthread_mutex_unlock(&async->mutex);
}

bool vw_translate_async_submit(vw_translate_async_t* async, const vw_caption_segment_t* segment, uint64_t epoch,
                               const char* source_lang, const char* target_lang) {
  if (!async || !segment || !segment->text_utf8 || !segment->text_utf8[0]) return false;
  size_t text_len = strnlen(segment->text_utf8, VW_MAX_TEXT_BYTES + 1U);
  if (text_len == 0 || text_len > VW_MAX_TEXT_BYTES) return false;

  pthread_mutex_lock(&async->mutex);
  if (async->stopping || epoch < async->min_epoch || async->job_count >= VW_TRANSLATE_ASYNC_QUEUE_CAPACITY) {
    pthread_mutex_unlock(&async->mutex);
    return false;
  }
  vw_translate_job_t* job = &async->jobs[async->job_head];
  memset(job, 0, sizeof(*job));
  job->epoch = epoch;
  job->segment = *segment;
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
  bool has_result = async->result_count > 0;
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
  *out = async->results[async->result_tail];
  async->result_tail = (async->result_tail + 1U) % VW_TRANSLATE_ASYNC_RESULT_CAPACITY;
  async->result_count--;
  pthread_mutex_unlock(&async->mutex);
  vw_translate_async_bind_result_text(out);
  return true;
}
