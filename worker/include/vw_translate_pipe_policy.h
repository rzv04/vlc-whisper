#ifndef VW_TRANSLATE_PIPE_POLICY_H_
#define VW_TRANSLATE_PIPE_POLICY_H_

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#endif

// Creates a translation subprocess pipe whose descriptors are nonblocking and close-on-exec before either endpoint is
// returned, failing closed and closing both descriptors when any required descriptor policy cannot be installed.
static inline int vw_translate_make_nonblocking_pipe(int pipefd[2]) {
#ifdef _WIN32
  (void)pipefd;
  return -1;
#else
  if (!pipefd) {
    errno = EINVAL;
    return -1;
  }
  pipefd[0] = -1;
  pipefd[1] = -1;
  if (pipe(pipefd) != 0) return -1;
  for (int i = 0; i < 2; i++) {
    int fd_flags = fcntl(pipefd[i], F_GETFD, 0);
    int status_flags = fcntl(pipefd[i], F_GETFL, 0);
    if (fd_flags < 0 || status_flags < 0 || fcntl(pipefd[i], F_SETFD, fd_flags | FD_CLOEXEC) != 0 ||
        fcntl(pipefd[i], F_SETFL, status_flags | O_NONBLOCK) != 0) {
      int saved_errno = errno;
      close(pipefd[0]);
      close(pipefd[1]);
      pipefd[0] = -1;
      pipefd[1] = -1;
      errno = saved_errno;
      return -1;
    }
  }
  return 0;
#endif
}

#ifndef _WIN32
// Keeps SIGPIPE ignored for the translation process lifetime while preserving the source's existing sigaction-shaped
// cleanup calls. Repeated concurrent calls are idempotent and never restore SIG_DFL underneath another request.
static inline int vw_translate_keep_sigpipe_ignored(int signum, const struct sigaction* action,
                                                    struct sigaction* old_action) {
  if (signum != SIGPIPE) return sigaction(signum, action, old_action);
  (void)action;
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) return -1;
  if (old_action) {
    memset(old_action, 0, sizeof(*old_action));
    old_action->sa_handler = SIG_IGN;
    if (sigemptyset(&old_action->sa_mask) != 0) return -1;
  }
  return 0;
}
#endif

#ifdef VW_TRANSLATE_PIPE_POLICY_OVERRIDE
#ifndef _WIN32
#define pipe(pipefd) vw_translate_make_nonblocking_pipe((pipefd))
#define sigaction(signum, action, old_action) vw_translate_keep_sigpipe_ignored((signum), (action), (old_action))
#endif
#endif

#endif  // VW_TRANSLATE_PIPE_POLICY_H_
