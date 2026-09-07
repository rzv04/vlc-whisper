#ifndef VW_TRANSLATE_PIPE_POLICY_H_
#define VW_TRANSLATE_PIPE_POLICY_H_

#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

// Creates a translation transport pipe with nonblocking and close-on-exec semantics established before use. Failure
// closes both descriptors so callers can never continue on an accidentally blocking pipe.
static inline int vw_translate_make_nonblocking_pipe(int pipefd[2]) {
  if (!pipefd) {
    errno = EINVAL;
    return -1;
  }
#ifdef __linux__
  if (pipe2(pipefd, O_NONBLOCK | O_CLOEXEC) == 0) return 0;
  if (errno != ENOSYS && errno != EINVAL) return -1;
#endif
  if (pipe(pipefd) != 0) return -1;

  int read_flags = fcntl(pipefd[0], F_GETFL, 0);
  int write_flags = fcntl(pipefd[1], F_GETFL, 0);
  int read_fd_flags = fcntl(pipefd[0], F_GETFD, 0);
  int write_fd_flags = fcntl(pipefd[1], F_GETFD, 0);
  if (read_flags < 0 || write_flags < 0 || read_fd_flags < 0 || write_fd_flags < 0 ||
      fcntl(pipefd[0], F_SETFL, read_flags | O_NONBLOCK) != 0 ||
      fcntl(pipefd[1], F_SETFL, write_flags | O_NONBLOCK) != 0 ||
      fcntl(pipefd[0], F_SETFD, read_fd_flags | FD_CLOEXEC) != 0 ||
      fcntl(pipefd[1], F_SETFD, write_fd_flags | FD_CLOEXEC) != 0) {
    int saved_errno = errno;
    close(pipefd[0]);
    close(pipefd[1]);
    pipefd[0] = -1;
    pipefd[1] = -1;
    errno = saved_errno;
    return -1;
  }
  return 0;
}

#ifdef VW_TRANSLATE_PIPE_POLICY_OVERRIDE
// Source-local libc substitution: vw_translate.c creates both curl pipes through the fail-closed helper above.
#define pipe(pipefd) vw_translate_make_nonblocking_pipe(pipefd)
#endif
#endif

#endif  // VW_TRANSLATE_PIPE_POLICY_H_
