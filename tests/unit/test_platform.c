// Platform-agnostic unit tests for the vw_platform.h abstraction layer.
// The matching platform implementation (vw_platform_linux.c on POSIX,
// vw_platform_win32.c on Windows) is compiled into this test by CMake.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vw_platform.h"
#include "vw_test.h"

#if defined(_WIN32) || defined(__MINGW32__)
static const char* kSpawnOk = "cmd.exe";
static const char* kSpawnMissing = "Z:\\definitely_missing_vw.exe";
#else
static const char* kSpawnOk = "/bin/true";
static const char* kSpawnMissing = "/nonexistent/vw_missing_binary";
#endif

// A bare executable name (no directory) must resolve through PATH, not CWD.
// "true" is guaranteed present in PATH on POSIX CI runners.
#ifndef _WIN32
static const char* kSpawnBarePath = "true";
#endif

int main(void) {
  // --- vw_platform_get_random_bytes ---
  uint8_t buf[32];

  EXPECT(!vw_platform_get_random_bytes(NULL, sizeof(buf)));  // NULL buffer rejected
  EXPECT(!vw_platform_get_random_bytes(buf, 0));             // zero size rejected
  EXPECT(!vw_platform_get_random_bytes(NULL, 0));            // NULL buffer and zero size rejected

  EXPECT(vw_platform_get_random_bytes(buf, sizeof(buf)));  // fills buffer, returns true

  uint8_t buf2[32];
  EXPECT(vw_platform_get_random_bytes(buf2, sizeof(buf2)));
  EXPECT(memcmp(buf, buf2, sizeof(buf)) != 0);  // consecutive draws must differ

  // --- vw_platform_get_time_us ---
  int64_t t1 = vw_platform_get_time_us();
  EXPECT(t1 > 0);  // post-epoch

  int64_t t2 = vw_platform_get_time_us();
  EXPECT(t2 >= t1);  // non-decreasing

  // Sanity: within 5 seconds of the wall clock
  int64_t now_us = (int64_t)time(NULL) * 1000000;
  EXPECT(llabs(t1 - now_us) < 5000000);

  // --- vw_platform_spawn_process ---
  EXPECT(!vw_platform_spawn_process(NULL, NULL, NULL));

  const char* argv_ok[] = {kSpawnOk, NULL};
  EXPECT(vw_platform_spawn_process(kSpawnOk, argv_ok, NULL));

  vw_process_t proc = 0;
  EXPECT(vw_platform_spawn_process(kSpawnOk, argv_ok, &proc));
  EXPECT(vw_platform_wait_process(proc, 2000));

  // Failure paths: partial-NULL arguments must be rejected
  EXPECT(!vw_platform_spawn_process(NULL, argv_ok, NULL));   // NULL executable
  EXPECT(!vw_platform_spawn_process(kSpawnOk, NULL, NULL));  // NULL argv

  const char* argv_missing[] = {kSpawnMissing, NULL};
  EXPECT(!vw_platform_spawn_process(kSpawnMissing, argv_missing, NULL));  // non-existent executable

#ifndef _WIN32
  // Bare-name spawn must use PATH search (posix_spawnp), independent of CWD.
  const char* argv_bare[] = {kSpawnBarePath, NULL};
  EXPECT(vw_platform_spawn_process(kSpawnBarePath, argv_bare, NULL));
#endif

  return 0;
}
