#ifndef VW_TRANSLATE_PIPE_POLICY_H_
#define VW_TRANSLATE_PIPE_POLICY_H_

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdbool.h>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#endif

// Creates a translation subprocess pipe with close-on-exec originals while preserving blocking status for child
// endpoints; parent endpoints are made nonblocking only after the child has been spawned.
static inline int vw_translate_make_cloexec_pipe(int pipefd[2]) {
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
    if (fd_flags < 0 || fcntl(pipefd[i], F_SETFD, fd_flags | FD_CLOEXEC) != 0) {
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
// Keeps SIGPIPE ignored for the translation process lifetime. The existing source passes saved-disposition pointers,
// but intercepted cleanup calls are intentionally idempotent and never restore SIG_DFL during concurrent requests.
static inline int vw_translate_keep_sigpipe_ignored(int signum, const void* action, void* old_action) {
  (void)action;
  (void)old_action;
  if (signum != SIGPIPE) {
    errno = EINVAL;
    return -1;
  }
  return signal(SIGPIPE, SIG_IGN) == SIG_ERR ? -1 : 0;
}

#ifdef VW_TRANSLATE_TESTING
// Injects deterministic F_SETFL failure for selected parent nonblocking setup calls while forwarding every other
// fcntl operation unchanged to the operating system during translation transport regression tests.
static inline int vw_translate_test_fcntl(int fd, int cmd, ...) {
  bool has_arg = cmd == F_SETFD || cmd == F_SETFL;
  int arg = 0;
  if (has_arg) {
    va_list args;
    va_start(args, cmd);
    arg = va_arg(args, int);
    va_end(args);
  }
  if (cmd == F_SETFL && (arg & O_NONBLOCK) != 0) {
    static unsigned nonblocking_calls = 0;
    nonblocking_calls++;
    const char* fail_call = getenv("VW_TEST_TRANSLATE_FAIL_NONBLOCKING_CALL");
    if (fail_call && fail_call[0]) {
      if (strcmp(fail_call, "all") == 0) {
        errno = EIO;
        return -1;
      }
      char* end = NULL;
      long target = strtol(fail_call, &end, 10);
      if (end && *end == '\0' && target > 0 && nonblocking_calls == (unsigned)target) {
        errno = EIO;
        return -1;
      }
    }
  }
  if (has_arg) return fcntl(fd, cmd, arg);
  return fcntl(fd, cmd);
}

// Redirects test-only curl process creation to a deterministic executable supplied through the environment while
// preserving production spawn attributes, file actions, arguments, and inherited environment semantics.
static inline int vw_translate_test_posix_spawn(pid_t* pid, const char* path,
                                                const posix_spawn_file_actions_t* actions,
                                                const posix_spawnattr_t* attr, char* const argv[],
                                                char* const envp[]) {
  const char* override = getenv("VW_TEST_TRANSLATE_EXECUTABLE");
  const char* executable = override && override[0] ? override : path;
  return posix_spawn(pid, executable, actions, attr, argv, envp);
}
#endif
#endif

#ifdef VW_TRANSLATE_PIPE_POLICY_OVERRIDE
#ifndef _WIN32
// Namespace the legacy source-local helper tokens before vw_translate.c is parsed, so the resulting project-authored
// C symbols obey the repository-wide vw_ prefix invariant without changing their internal semantics.
#define set_cloexec vw_translate_set_cloexec
#define set_nonblocking vw_translate_set_nonblocking
#define pipe(pipefd) vw_translate_make_cloexec_pipe((pipefd))
#define sigaction(signum, action, old_action) vw_translate_keep_sigpipe_ignored((signum), (action), (old_action))
#ifdef VW_TRANSLATE_TESTING
#define fcntl(...) vw_translate_test_fcntl(__VA_ARGS__)
#define posix_spawn(pid, path, actions, attr, argv, envp) \
  vw_translate_test_posix_spawn((pid), (path), (actions), (attr), (argv), (envp))
#endif
#endif
#endif

#endif  // VW_TRANSLATE_PIPE_POLICY_H_
