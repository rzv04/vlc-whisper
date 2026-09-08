#include "vw_test.h"

#ifndef _WIN32
#include <spawn.h>
#include <sys/wait.h>

#include "vw_translate_pipe_policy.h"

extern char** environ;
#endif

int main(int argc, char** argv) {
#ifndef _WIN32
  if (argc == 2 && strcmp(argv[1], "--pipe-child") == 0) {
    int in_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    int out_flags = fcntl(STDOUT_FILENO, F_GETFL, 0);
    return in_flags >= 0 && out_flags >= 0 && (in_flags & O_NONBLOCK) == 0 && (out_flags & O_NONBLOCK) == 0 ? 0 : 42;
  }

  int pipe_out[2] = {-1, -1};
  int pipe_in[2] = {-1, -1};
  vw_test_check_true("translation output pipe creation succeeds", vw_translate_make_cloexec_pipe(pipe_out) == 0);
  vw_test_check_true("translation input pipe creation succeeds", vw_translate_make_cloexec_pipe(pipe_in) == 0);

  if (pipe_out[0] >= 0 && pipe_out[1] >= 0 && pipe_in[0] >= 0 && pipe_in[1] >= 0) {
    int child_out_flags = fcntl(pipe_out[1], F_GETFL, 0);
    int child_in_flags = fcntl(pipe_in[0], F_GETFL, 0);
    int out_fd_flags = fcntl(pipe_out[1], F_GETFD, 0);
    int in_fd_flags = fcntl(pipe_in[0], F_GETFD, 0);
    vw_test_check_true("curl stdout pipe is blocking before spawn",
                       child_out_flags >= 0 && (child_out_flags & O_NONBLOCK) == 0);
    vw_test_check_true("curl stdin pipe is blocking before spawn",
                       child_in_flags >= 0 && (child_in_flags & O_NONBLOCK) == 0);
    vw_test_check_true("curl stdout original is close-on-exec", out_fd_flags >= 0 && (out_fd_flags & FD_CLOEXEC) != 0);
    vw_test_check_true("curl stdin original is close-on-exec", in_fd_flags >= 0 && (in_fd_flags & FD_CLOEXEC) != 0);

    posix_spawn_file_actions_t actions;
    int actions_status = posix_spawn_file_actions_init(&actions);
    vw_test_check_true("pipe seam spawn actions initialize", actions_status == 0);
    if (actions_status == 0) {
      posix_spawn_file_actions_addclose(&actions, pipe_out[0]);
      posix_spawn_file_actions_adddup2(&actions, pipe_out[1], STDOUT_FILENO);
      posix_spawn_file_actions_addclose(&actions, pipe_out[1]);
      posix_spawn_file_actions_addclose(&actions, pipe_in[1]);
      posix_spawn_file_actions_adddup2(&actions, pipe_in[0], STDIN_FILENO);
      posix_spawn_file_actions_addclose(&actions, pipe_in[0]);

      pid_t pid = 0;
      char* child_argv[] = {argv[0], "--pipe-child", NULL};
      int spawn_status = posix_spawn(&pid, argv[0], &actions, NULL, child_argv, environ);
      posix_spawn_file_actions_destroy(&actions);
      vw_test_check_true("pipe seam child spawns", spawn_status == 0);

      close(pipe_out[1]);
      pipe_out[1] = -1;
      close(pipe_in[0]);
      pipe_in[0] = -1;

      int read_flags = fcntl(pipe_out[0], F_GETFL, 0);
      int write_flags = fcntl(pipe_in[1], F_GETFL, 0);
      vw_test_check_true("parent output read endpoint becomes nonblocking",
                         read_flags >= 0 && fcntl(pipe_out[0], F_SETFL, read_flags | O_NONBLOCK) == 0);
      vw_test_check_true("parent input write endpoint becomes nonblocking",
                         write_flags >= 0 && fcntl(pipe_in[1], F_SETFL, write_flags | O_NONBLOCK) == 0);

      if (spawn_status == 0) {
        int status = 0;
        vw_test_check_true("pipe seam child wait succeeds", waitpid(pid, &status, 0) == pid);
        vw_test_check_true("spawned curl-side descriptors remain blocking",
                           WIFEXITED(status) && WEXITSTATUS(status) == 0);
      }
    }
  }

  if (pipe_out[0] >= 0) close(pipe_out[0]);
  if (pipe_out[1] >= 0) close(pipe_out[1]);
  if (pipe_in[0] >= 0) close(pipe_in[0]);
  if (pipe_in[1] >= 0) close(pipe_in[1]);
#else
  (void)argc;
  (void)argv;
  vw_test_check_true("Windows translation transport uses WinHTTP instead of POSIX pipes", true);
#endif
  return vw_test_finish("test_translate_pipe_policy");
}
