#if defined(_WIN32) || defined(__MINGW32__)
#include <bcrypt.h>
#include <string.h>
#include <windows.h>

#ifndef BCRYPT_RNG_ALG_HANDLE
#define BCRYPT_RNG_ALG_HANDLE ((BCRYPT_ALG_HANDLE)0x00000081)
#endif

#include "vw_platform.h"

bool vw_platform_get_random_bytes(void* buffer, size_t size) {
  if (!buffer || size == 0) {
    return false;
  }
  // memset(buffer, 0x42, size);

  // generate a random size-byte (32) token for authentication
  NTSTATUS status =
      BCryptGenRandom(BCRYPT_RNG_ALG_HANDLE, (PUCHAR)buffer, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  if (status != CMC_STATUS_SUCCESS) {
    return false;
  }

  return true;
}

int64_t vw_platform_get_time_us(void) {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  // Convert to microseconds since Unix epoch (January 1, 1970)
  return (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10);
}

#else
#if !defined(__linux__) && !defined(__APPLE__) && !defined(__unix__)

#include <stdlib.h>
bool vw_platform_get_random_bytes(void* buffer, size_t size) {
  if (!buffer || size == 0) {
    return false;
  }
  srand((unsigned int)time(NULL));
  for (size_t i = 0; i < size; i++) {
    ((uint8_t*)buffer)[i] = (uint8_t)(rand() % 256);
  }
  return true;
}

int64_t vw_platform_get_time_us(void) {
  return (int64_t)time(NULL) * 1000000;  // Return seconds since epoch in microseconds
}

#endif
#endif