#ifndef VW_PLATFORM_H_
#define VW_PLATFORM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Platform abstraction layer for random bytes and time functions

// Platform process handle type
#if defined(_WIN32)
typedef void* vw_process_t;
#define VW_WEAK
#else
typedef int vw_process_t;
#define VW_WEAK __attribute__((weak))
#endif

// Generates cryptographically secure random bytes and fills the provided buffer with them.
bool vw_platform_get_random_bytes(void* buffer, size_t size);

// Returns the current time in microseconds since the Unix epoch (January 1, 1970).
int64_t vw_platform_get_time_us(void);

// Returns high-resolution monotonic time in microseconds for accurate deadline & timeout measurements unaffected by
// wall-clock changes.
int64_t vw_platform_get_monotonic_time_us(void);

// Spawns a new process with the given executable path and arguments. Returns true on success, false on failure.
// If out_process is not NULL, the handle/PID of the spawned process is stored in it.
// `argv` must be NULL-terminated (like main/execve); argv[0] is the program name and is not passed as an argument.
bool vw_platform_spawn_process(const char* executable_path, const char* const argv[], vw_process_t* out_process);

// Waits for the specified process to terminate, up to timeout_ms milliseconds.
// Returns true if the process terminated within the timeout, false if it timed out or an error occurred.
bool vw_platform_wait_process(vw_process_t process, uint32_t timeout_ms);

// Forcefully terminates the specified process if it fails to exit gracefully within its deadline.
// Consumes the handle/pid: on Win32 the process handle is closed, on POSIX the child is reaped, so
// vw_platform_close_process MUST NOT be called afterwards for the same process. If the child cannot be
// reaped within the grace period (e.g. D-state), the pid is retained in a bounded registry and reaped on
// a later platform process call instead of being abandoned.
void vw_platform_terminate_process(vw_process_t process);

// Closes and releases any OS process handle resources associated with the process handle.
// Only valid for a process that has not been consumed by vw_platform_terminate_process.
void vw_platform_close_process(vw_process_t process);

// Platform thread handle type
#if defined(_WIN32)
typedef void* vw_thread_t;
#else
#include <pthread.h>
typedef pthread_t vw_thread_t;
#endif

// Creates and starts a new background thread executing `func(arg)`. Returns true on success.
bool vw_platform_thread_create(vw_thread_t* thread, void* (*func)(void*), void* arg);

// Waits for the specified background thread to terminate.
void vw_platform_thread_join(vw_thread_t thread);

// Causes the calling thread to sleep for the specified duration in milliseconds.
void vw_platform_sleep_ms(uint32_t ms);

#endif  // VW_PLATFORM_H_
