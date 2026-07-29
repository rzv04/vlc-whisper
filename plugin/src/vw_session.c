#include "vw_session.h"

#include <stdlib.h>

vw_session_t *vw_session_create(void) {
  vw_session_t *session = (vw_session_t *)calloc(1, sizeof(vw_session_t));
  if (session) {
    session->state = VW_SESSION_STATE_IDLE;
  }
  return session;
}

void vw_session_destroy(vw_session_t *session) {
  if (session) {
    free(session);
  }
}

bool vw_session_start(vw_session_t *session) {
  if (!session) {
    return false;
  }
  session->state = VW_SESSION_STATE_STARTING;
  return true;
}

void vw_session_handle_discontinuity(vw_session_t *session) {
  if (session) {
    session->state = VW_SESSION_STATE_FAILED;
  }
}
