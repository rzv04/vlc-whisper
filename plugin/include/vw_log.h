#ifndef VW_LOG_H_
#define VW_LOG_H_

typedef enum vw_log_level {
  VW_LOG_LEVEL_DEBUG,
  VW_LOG_LEVEL_INFO,
  VW_LOG_LEVEL_WARN,
  VW_LOG_LEVEL_ERROR
} vw_log_level_t;

// Privacy-safe logger: NEVER logs PCM samples, transcripts, or sensitive paths
void vw_log_event(vw_log_level_t level, const char *event_id, const char *fmt, ...);

#endif // VW_LOG_H_
