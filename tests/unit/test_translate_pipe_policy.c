#include "vw_test.h"

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>

#include "vw_translate_pipe_policy.h"
#endif

int main(void) {
#ifndef _WIN32
  int pipefd[2] = {-1, -1};
  vw_test_check_true("translation pipe creation succeeds", vw_translate_make_nonblocking_pipe(pipefd) == 0);
  if (pipefd[0] >= 0 && pipefd[1] >= 0) {
    int read_flags = fcntl(pipefd[0], F_GETFL, 0);
    int write_flags = fcntl(pipefd[1], F_GETFL, 0);
    int read_fd_flags = fcntl(pipefd[0], F_GETFD, 0);
    int write_fd_flags = fcntl(pipefd[1], F_GETFD, 0);
    vw_test_check_true("translation read pipe is nonblocking", read_flags >= 0 && (read_flags & O_NONBLOCK) != 0);
    vw_test_check_true("translation write pipe is nonblocking", write_flags >= 0 && (write_flags & O_NONBLOCK) != 0);
    vw_test_check_true("translation read pipe is close-on-exec",
                       read_fd_flags >= 0 && (read_fd_flags & FD_CLOEXEC) != 0);
    vw_test_check_true("translation write pipe is close-on-exec",
                       write_fd_flags >= 0 && (write_fd_flags & FD_CLOEXEC) != 0);
    close(pipefd[0]);
    close(pipefd[1]);
  }
#else
  vw_test_check_true("Windows translation transport uses WinHTTP instead of POSIX pipes", true);
#endif
  return vw_test_finish("test_translate_pipe_policy");
}
