// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

#include "vw_log.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(__MINGW32__)
#include <windows.h>
static SRWLOCK g_log_mutex = SRWLOCK_INIT;
static inline void vw_log_lock(void) { AcquireSRWLockExclusive(&g_log_mutex); }
static inline void vw_log_unlock(void) { ReleaseSRWLockExclusive(&g_log_mutex); }
#else
#include <pthread.h>
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static inline void vw_log_lock(void) { pthread_mutex_lock(&g_log_mutex); }
static inline void vw_log_unlock(void) { pthread_mutex_unlock(&g_log_mutex); }
#endif

#define VW_LOG_MAX_SINKS 16

typedef struct {
  vw_log_sink_fn sink;
  void* user_data;
} vw_log_sink_entry_t;

static vw_log_sink_entry_t g_log_sinks[VW_LOG_MAX_SINKS];
static size_t g_log_sink_count = 0;
static FILE* g_log_file = NULL;
static _Atomic bool g_log_enabled = ATOMIC_VAR_INIT(false);

void vw_log_set_enabled(bool enabled) { atomic_store(&g_log_enabled, enabled); }

void vw_log_set_sink(vw_log_sink_fn sink, void* user_data) {
  vw_log_lock();
  if (sink != NULL) {
    // Check if entry already registered (by user_data or matching sink/user_data)
    bool found = false;
    for (size_t i = 0; i < g_log_sink_count; ++i) {
      if ((user_data != NULL && g_log_sinks[i].user_data == user_data) ||
          (user_data == NULL && g_log_sinks[i].sink == sink && g_log_sinks[i].user_data == NULL)) {
        g_log_sinks[i].sink = sink;
        g_log_sinks[i].user_data = user_data;
        found = true;
        break;
      }
    }
    if (!found) {
      if (g_log_sink_count < VW_LOG_MAX_SINKS) {
        g_log_sinks[g_log_sink_count].sink = sink;
        g_log_sinks[g_log_sink_count].user_data = user_data;
        g_log_sink_count++;
      } else {
        g_log_sinks[VW_LOG_MAX_SINKS - 1].sink = sink;
        g_log_sinks[VW_LOG_MAX_SINKS - 1].user_data = user_data;
      }
    }
  } else {
    // Unregister sink instance safely
    if (user_data != NULL) {
      for (size_t i = 0; i < g_log_sink_count; ++i) {
        if (g_log_sinks[i].user_data == user_data) {
          for (size_t j = i; j + 1 < g_log_sink_count; ++j) {
            g_log_sinks[j] = g_log_sinks[j + 1];
          }
          g_log_sink_count--;
          g_log_sinks[g_log_sink_count].sink = NULL;
          g_log_sinks[g_log_sink_count].user_data = NULL;
          break;
        }
      }
    } else {
      // General unregister (e.g. vw_log_set_sink(NULL, NULL)):
      // Pop most recent instance reference so other active plugin instances are preserved
      if (g_log_sink_count > 0) {
        g_log_sink_count--;
        g_log_sinks[g_log_sink_count].sink = NULL;
        g_log_sinks[g_log_sink_count].user_data = NULL;
      }
    }
  }
  vw_log_unlock();
}

void vw_log_set_file(FILE* file) {
  vw_log_lock();
  if (g_log_file != NULL && g_log_file != file) {
    fflush(g_log_file);
  }
  g_log_file = file;
  vw_log_unlock();
}

void vw_log_flush(void) {
  vw_log_lock();
  if (g_log_file != NULL) {
    fflush(g_log_file);
  }
  fflush(stderr);
  vw_log_unlock();
}

static const char* vw_log_level_to_string(vw_log_level_t level) {
  switch (level) {
    case VW_LOG_LEVEL_DEBUG:
      return "DEBUG";
    case VW_LOG_LEVEL_INFO:
      return "INFO";
    case VW_LOG_LEVEL_WARN:
      return "WARN";
    case VW_LOG_LEVEL_ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
  }
}

static void vw_log_write(vw_log_level_t level, const char* event_id, const char* message_buf) {
  vw_log_lock();

  if (g_log_sink_count > 0) {
    for (size_t i = 0; i < g_log_sink_count; ++i) {
      if (g_log_sinks[i].sink != NULL) {
        g_log_sinks[i].sink(level, event_id, message_buf, g_log_sinks[i].user_data);
      }
    }
  } else {
    // Default fallback sink: print to stderr with tags
    fprintf(stderr, "[%s] [%s] %s\n", vw_log_level_to_string(level), event_id, message_buf);
    fflush(stderr);
  }

  // Optional additional FILE* output (e.g. the worker's opt-in temp log file).
  if (g_log_file != NULL) {
    fprintf(g_log_file, "[%s] [%s] %s\n", vw_log_level_to_string(level), event_id, message_buf);
    fflush(g_log_file);
  }

  vw_log_unlock();
}

void vw_log_event(vw_log_level_t level, const char* event_id, const char* fmt, ...) {
  if (!atomic_load(&g_log_enabled)) return;
  if (event_id == NULL) {
    event_id = "UNKNOWN_EVENT";
  }
  if (fmt == NULL) {
    fmt = "";
  }

  char message_buf[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(message_buf, sizeof(message_buf), fmt, args);
  va_end(args);

  vw_log_write(level, event_id, message_buf);
}
