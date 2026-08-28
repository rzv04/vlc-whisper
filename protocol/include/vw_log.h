#ifndef VW_LOG_H_
#define VW_LOG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum vw_log_level {
  VW_LOG_LEVEL_DEBUG = 0,
  VW_LOG_LEVEL_INFO = 1,
  VW_LOG_LEVEL_WARN = 2,
  VW_LOG_LEVEL_ERROR = 3
} vw_log_level_t;

// Log sink callback function signature for custom logging routes (e.g. VLC msg_* or stderr)
typedef void (*vw_log_sink_fn)(vw_log_level_t level, const char* event_id, const char* formatted_msg, void* user_data);

// Enables or suppresses all project diagnostic events atomically; logging is disabled by default, including file
// output.
void vw_log_set_enabled(bool enabled);

// Sets custom log sink callback (defaults to stderr unbuffered printing if NULL)
void vw_log_set_sink(vw_log_sink_fn sink, void* user_data);

// Sets an additional FILE* output that enabled log events are also written to; pass NULL to disable file output.
void vw_log_set_file(FILE* file);

// Privacy-safe variadic log function: NEVER logs PCM samples, transcript text, or secret tokens.
void vw_log_event(vw_log_level_t level, const char* event_id, const char* fmt, ...);

#endif  // VW_LOG_H_
