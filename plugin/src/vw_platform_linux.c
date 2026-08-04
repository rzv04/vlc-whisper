#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#include <spawn.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "vw_platform.h"
bool vw_platform_get_random_bytes(void* buffer, size_t size) {
  if (!buffer || size == 0) {
    return false;
  }
  // Seed the PRNG once; reseeding per call would return identical bytes for
  // calls within the same second. Note: rand() is not a CSPRNG (MVP shortcut).
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
  if (!executable_path || !argv || access(executable_path, F_OK) != 0) {
    return false;
  }

  pid_t pid;
  // NULL envp: child inherits the parent environment
  int ret = posix_spawn(&pid, executable_path, NULL, NULL, (char* const*)argv, NULL);
  return ret == 0;
}

#endif