#ifndef VW_SESSION_H_
#define VW_SESSION_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol.h"

typedef enum vw_session_state {
  VW_SESSION_STATE_IDLE,
  VW_SESSION_STATE_STARTING,
  VW_SESSION_STATE_READY,
  VW_SESSION_STATE_PLAYING,
  VW_SESSION_STATE_PAUSED,
  VW_SESSION_STATE_STOPPING,
  VW_SESSION_STATE_FAILED
} vw_session_state_t;

typedef struct vw_session {
  vw_session_state_t state;
} vw_session_t;

vw_session_t *vw_session_create(void);
void vw_session_destroy(vw_session_t *session);
bool vw_session_start(vw_session_t *session);
void vw_session_handle_discontinuity(vw_session_t *session);

#endif  // VW_SESSION_H_
