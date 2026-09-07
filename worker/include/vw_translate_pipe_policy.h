#ifndef VW_TRANSLATE_PIPE_POLICY_H_
#define VW_TRANSLATE_PIPE_POLICY_H_

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
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

#endif  // VW_TRANSLATE_PIPE_POLICY_H_
