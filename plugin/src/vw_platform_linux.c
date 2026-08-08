#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "vw_platform.h"

// PIDs of children that were SIGKILLed but did not become waitable within the
// termination grace period (e.g. stuck in D-state). They are reaped
// opportunistically at every platform process call once they become waitable.
// The registry is bounded (VW_MAX_UNREAPED_PIDS): reaching the bound requires
// 16 concurrent unkillable children, in which case the pid is dropped with no
// further cleanup path — pathological and intentionally unsupported.
#define VW_MAX_UNREAPED_PIDS 16
static pid_t vw_unreaped_pids[VW_MAX_UNREAPED_PIDS];
static size_t vw_unreaped_count = 0;

// Reap any previously-unkillable children that have since become waitable.
// Called from every process entry point; WNOHANG never blocks, and the plugin
// is single-threaded so no locking is required. ECHILD (reaped elsewhere, e.g.
// a SIGCHLD handler) also removes the entry.
static void vw_platform_reap_unreaped(void) {
  size_t i = 0;
  while (i < vw_unreaped_count) {
    int status;
    pid_t ret = waitpid(vw_unreaped_pids[i], &status, WNOHANG);
    if (ret == vw_unreaped_pids[i] || (ret == -1 && errno == ECHILD)) {
      vw_unreaped_pids[i] = vw_unreaped_pids[vw_unreaped_count - 1];
      vw_unreaped_count--;
    } else {
      i++;
    }
  }
}

bool vw_platform_get_random_bytes(void* buffer, size_t size) {
  if (!buffer || size == 0) {
    return false;
  }
  // Seed the PRNG once; reseeding per call would return identical bytes for
  // calls within the same second. Note: rand() is NOT a CSPRNG (MVP shortcut).
  static bool seeded = false;
  if (!seeded) {
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    seeded = true;
  }
  for (size_t i = 0; i < size; i++) {
    ((uint8_t*)buffer)[i] = (uint8_t)(rand() % 256);
  }
  return true;
}

int64_t vw_platform_get_time_us(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
    return (int64_t)ts.tv_sec * 1000000LL + (ts.tv_nsec / 1000LL);
  }
  return (int64_t)time(NULL) * 1000000LL;
}

int64_t vw_platform_get_monotonic_time_us(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
    return (int64_t)ts.tv_sec * 1000000LL + (ts.tv_nsec / 1000LL);
  }
  // clock_gettime(CLOCK_MONOTONIC) is always supported on Linux >= 2.6, so
  // this path is unreachable in practice. The wall-clock fallback degrades the
  // monotonic guarantee if it ever triggers (clock adjustments could move the
  // value backward); kept only to avoid returning garbage.
  return (int64_t)time(NULL) * 1000000LL;
}

bool vw_platform_spawn_process(const char* executable_path, const char* const argv[], vw_process_t* out_process) {
  vw_platform_reap_unreaped();
  if (!executable_path || !argv) {
    return false;
  }

  // Bare name (no directory) => PATH search. Never gate on CWD; use
  // posix_spawnp's PATH search semantics directly.
  if (!strchr(executable_path, '/')) {
    pid_t pid;
    int ret = posix_spawnp(&pid, executable_path, NULL, NULL, (char* const*)argv, NULL);
    if (ret == 0 && out_process) *out_process = pid;
    return ret == 0;
  }

  // Path-form name: validate presence at that location, then spawn.
  if (access(executable_path, F_OK) != 0) {
    return false;
  }
  pid_t pid;
  // NULL envp: child inherits the parent environment
  int ret = posix_spawn(&pid, executable_path, NULL, NULL, (char* const*)argv, NULL);
  if (ret == 0 && out_process) *out_process = pid;
  return ret == 0;
}

bool vw_platform_wait_process(vw_process_t process, uint32_t timeout_ms) {
  if (process <= 0) return false;  // waitpid(0) would reap any process-group child
  vw_platform_reap_unreaped();
  pid_t pid = process;
  uint32_t elapsed_ms = 0;
  uint32_t sleep_ms = 10;

  while (elapsed_ms <= timeout_ms) {
    int status;
    pid_t ret = waitpid(pid, &status, WNOHANG);
    if (ret == pid) {
      return true;
    } else if (ret == -1) {
      if (errno == ECHILD) return true;
    }

    if (elapsed_ms >= timeout_ms) break;
    vw_platform_sleep_ms(sleep_ms);
    elapsed_ms += sleep_ms;
  }
  return false;
}

void vw_platform_terminate_process(vw_process_t process) {
  if (process > 0) {
    pid_t pid = (pid_t)process;
    vw_platform_reap_unreaped();
    kill(pid, SIGKILL);
    // SIGKILL delivery is asynchronous: a single nonblocking waitpid can
    // observe the child before it becomes waitable and leave a zombie until
    // the parent exits. Wait (bounded) for the reap; a D-state child may not
    // die within the grace period. Never discard the pid in that case — keep
    // it registered so vw_platform_reap_unreaped reaps it once it becomes
    // waitable (a pending SIGKILL kills it as soon as it leaves D-state).
    if (!vw_platform_wait_process(process, 1000)) {
      if (vw_unreaped_count < VW_MAX_UNREAPED_PIDS) {
        vw_unreaped_pids[vw_unreaped_count++] = pid;
      }
    }
  }
}

void vw_platform_close_process(vw_process_t process) { (void)process; }

bool vw_platform_thread_create(vw_thread_t* thread, void* (*func)(void*), void* arg) {
  if (!thread || !func) return false;
  return pthread_create(thread, NULL, func, arg) == 0;
}

void vw_platform_thread_join(vw_thread_t thread) { pthread_join(thread, NULL); }

void vw_platform_sleep_ms(uint32_t ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}

#endif