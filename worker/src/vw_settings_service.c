#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "vw_settings_service.h"

#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#endif

#include "vw_model_download.h"
#include "vw_translate.h"

static void vw_service_sleep(void) {
#ifdef _WIN32
  Sleep(20);
#else
  struct timespec delay = {0, 20000000};
  nanosleep(&delay, NULL);
#endif
}

// No public listener: only the launching process possesses this input pipe. A broken pipe is cancellation.
static int vw_service_read(char* buffer, size_t size) {
#ifdef _WIN32
  HANDLE pipe = GetStdHandle(STD_INPUT_HANDLE);
  DWORD available = 0, received = 0;
  if (!PeekNamedPipe(pipe, NULL, 0, NULL, &available, NULL)) return GetLastError() == ERROR_BROKEN_PIPE ? -1 : -2;
  if (!available) return 0;
  if (!ReadFile(pipe, buffer, (DWORD)(available < size ? available : size), &received, NULL))
    return GetLastError() == ERROR_BROKEN_PIPE ? -1 : -2;
  if (!received) return -1;
  return (int)received;
#else
  struct pollfd input = {STDIN_FILENO, POLLIN, 0};
  int ready = poll(&input, 1, 0);
  if (ready < 0) return errno == EINTR ? 0 : -2;
  if (!ready) return 0;
  ssize_t count = read(STDIN_FILENO, buffer, size);
  if (count < 0 && errno == EINTR) return 0;
  return count > 0 ? (int)count : (count == 0 ? -1 : -2);
#endif
}

static bool vw_service_language(const char* language, bool source) {
  static const char* const languages[] = {"en", "ro", "es", "fr", "de", "it", "pt", "ru", "uk", "tr", "ja", "ko", "zh"};
  if (source && strcmp(language, "auto") == 0) return true;
  for (size_t i = 0; i < sizeof(languages) / sizeof(languages[0]); ++i)
    if (strcmp(language, languages[i]) == 0) return true;
  return false;
}

static int vw_service_translate(const char* source, const char* target) {
  if (!vw_service_language(source, true) || !vw_service_language(target, false)) return 2;
  char text[VW_TRANSLATE_MAX_TEXT_BYTES];
  size_t length = 0;
  bool ended = false;
  // Bound input collection as well as the translator's existing network deadline.
  for (unsigned i = 0; i < 250; ++i) {
    int count = vw_service_read(text + length, sizeof(text) - length);
    if (count == -2) return 4;
    if (count == -1) {
      ended = true;
      break;
    }
    if (count > 0) {
      if (memchr(text + length, '\0', (size_t)count)) return 2;
      length += (size_t)count;
      if (length == sizeof(text)) return 2;
    }
    vw_service_sleep();
  }
  if (!ended || !length) return 2;
  text[length] = '\0';
  char result[VW_TRANSLATE_MAX_RESPONSE_BYTES];
  uint8_t tier = 0;
  uint32_t latency = 0;
  vw_translate_failure_t failure = {0};
  if (!vw_translate_text_detailed(text, source, target, result, sizeof(result), &tier, &latency, &failure) ||
      !result[0])
    return 4;
  size_t bytes = strlen(result);
  return fwrite(result, 1, bytes, stdout) == bytes && fflush(stdout) == 0 ? 0 : 4;
}

static int vw_service_download(const char* model) {
  const vw_model_catalog_entry_t* entry = vw_model_catalog_find(model);
  if (!entry) return 2;
  char directory[4096];
  if (!vw_model_download_default_dir(directory, sizeof(directory))) return 4;
  vw_model_download_t* download = vw_model_download_start(entry, directory);
  if (!download) return 4;
  int result = 4;
  int last_stage = -1, last_pct = -1;
  for (;;) {
    vw_download_progress_t progress;
    if (!vw_model_download_poll(download, &progress)) break;
    if (progress.stage != last_stage || progress.pct != last_pct) {
      if (printf("%d %d\n", progress.stage, progress.pct) < 0 || fflush(stdout) != 0) break;
      last_stage = progress.stage;
      last_pct = progress.pct;
    }
    if (progress.stage == VW_MODEL_STAGE_DONE) {
      result = 0;
      break;
    }
    if (progress.stage == VW_MODEL_STAGE_FAILED) break;
    char command;
    if (vw_service_read(&command, 1) != 0) {
      result = 3;
      break;
    }
    vw_service_sleep();
  }
  // Free requests cancellation and joins before releasing the destination lock, including on broken parent pipes.
  vw_model_download_free(download);
  return result;
}

int vw_settings_service_run(int argc, char** argv) {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  if (argc == 3 && strcmp(argv[1], "--settings-download") == 0) return vw_service_download(argv[2]);
  if (argc == 4 && strcmp(argv[1], "--settings-translate") == 0) return vw_service_translate(argv[2], argv[3]);
  return 2;
}
