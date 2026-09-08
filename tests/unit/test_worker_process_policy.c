#include <stdbool.h>

#ifndef _WIN32
#include <signal.h>
#endif

#include "vw_process_policy.h"
#include "vw_test.h"

int main(void) {
  vw_test_check_true("worker signal policy installs successfully", vw_process_install_worker_signal_policy());
#ifndef _WIN32
  vw_test_check_true("worker keeps SIGPIPE ignored for process lifetime", signal(SIGPIPE, SIG_IGN) == SIG_IGN);
#endif
  return vw_test_finish("test_worker_process_policy");
}
