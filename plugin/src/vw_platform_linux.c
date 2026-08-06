#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#define _POSIX_C_SOURCE 200809L
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "vw_platform.h"
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
  return (int64_t)time(NULL) * 1000000;  // Return seconds since epoch in microseconds
}

bool vw_platform_spawn_process(const char* executable_path, const char* const argv[]) {
  if (!executable_path || !argv) {
    return false;
  }

  // Bare name (no directory) => PATH search. Never gate on CWD; use
  // posix_spawnp's PATH search semantics directly.
  if (!strchr(executable_path, '/')) {
    pid_t pid;
    int ret = posix_spawnp(&pid, executable_path, NULL, NULL, (char* const*)argv, NULL);
    return ret == 0;
  }

  // Path-form name: validate presence at that location, then spawn.
  if (access(executable_path, F_OK) != 0) {
    return false;
  }
  pid_t pid;
  // NULL envp: child inherits the parent environment
  int ret = posix_spawn(&pid, executable_path, NULL, NULL, (char* const*)argv, NULL);
  return ret == 0;
}

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