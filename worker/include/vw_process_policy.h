#ifndef VW_PROCESS_POLICY_H_
#define VW_PROCESS_POLICY_H_

#include <stdbool.h>

#ifndef _WIN32
#include <signal.h>
#endif

// Installs process-wide worker signal policy before background threads start. POSIX workers ignore SIGPIPE permanently;
// spawned curl children explicitly restore their default SIGPIPE disposition before exec.
static inline bool vw_process_install_worker_signal_policy(void) {
#ifndef _WIN32
  struct sigaction action;
  action.sa_handler = SIG_IGN;
  action.sa_flags = 0;
  if (sigemptyset(&action.sa_mask) != 0) return false;
  return sigaction(SIGPIPE, &action, NULL) == 0;
#else
  return true;
#endif
}

#endif  // VW_PROCESS_POLICY_H_
