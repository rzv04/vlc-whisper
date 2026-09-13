// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "vw_worker_log_policy.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#else
// clang-format off
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
// clang-format on
#endif

#include "vw_log.h"

static FILE* s_worker_log_file = NULL;
static char s_worker_log_path[1024] = {0};
static bool s_worker_default_log = false;

#ifdef _WIN32
#ifndef VW_PATH_MAX_BYTES
#define VW_PATH_MAX_BYTES 1024
#endif

static FILE* vw_worker_open_log_utf8(const char* path, bool exclusive) {
  if (!path) return NULL;
  int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
  if (chars <= 0) return NULL;
  wchar_t* wide_path = (wchar_t*)malloc((size_t)chars * sizeof(wchar_t));
  if (!wide_path) return NULL;
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide_path, chars) <= 0) {
    free(wide_path);
    return NULL;
  }
  FILE* file;
  if (exclusive) {
    int fd = _wopen(wide_path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
    file = fd >= 0 ? _fdopen(fd, "w") : NULL;
    if (!file && fd >= 0) _close(fd);
  } else {
    file = _wfopen(wide_path, L"w");
  }
  free(wide_path);
  return file;
}
#endif

const char* vw_worker_default_log_dir(void) {
#ifdef _WIN32
  static char utf8_dir[VW_PATH_MAX_BYTES];
  wchar_t wide_dir[VW_PATH_MAX_BYTES];
  DWORD chars = GetTempPathW((DWORD)(sizeof(wide_dir) / sizeof(wide_dir[0])), wide_dir);
  if (chars > 0 && chars < sizeof(wide_dir) / sizeof(wide_dir[0]) &&
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_dir, -1, utf8_dir, sizeof(utf8_dir), NULL, NULL) > 0) {
    return utf8_dir;
  }
  return "C:\\Windows\\Temp";
#else
  const char* dir = getenv("XDG_RUNTIME_DIR");
  if (dir && dir[0]) {
    return dir;
  }
  dir = getenv("TMPDIR");
  if (dir && dir[0]) {
    return dir;
  }
  return "/tmp";
#endif
}

#ifndef _WIN32
typedef struct {
  char path[1024];
  time_t mtime;
} vw_posix_log_entry_t;

static int vw_posix_log_cmp(const void* a, const void* b) {
  const vw_posix_log_entry_t* ea = (const vw_posix_log_entry_t*)a;
  const vw_posix_log_entry_t* eb = (const vw_posix_log_entry_t*)b;
  if (ea->mtime < eb->mtime) return -1;
  if (ea->mtime > eb->mtime) return 1;
  return strcmp(ea->path, eb->path);
}
#else
typedef struct {
  wchar_t path[VW_PATH_MAX_BYTES];
  FILETIME mtime;
} vw_win_log_entry_t;

static int vw_win_log_cmp(const void* a, const void* b) {
  const vw_win_log_entry_t* ea = (const vw_win_log_entry_t*)a;
  const vw_win_log_entry_t* eb = (const vw_win_log_entry_t*)b;
  LONG res = CompareFileTime(&ea->mtime, &eb->mtime);
  if (res != 0) return (int)res;
  return wcscmp(ea->path, eb->path);
}
#endif

void vw_worker_prune_default_logs(const char* dir, size_t max_keep) {
  if (!dir || !dir[0]) return;

#ifndef _WIN32
  DIR* d = opendir(dir);
  if (!d) return;

  vw_posix_log_entry_t entries[64];
  size_t count = 0;
  const char prefix[] = "vlc-whisper-worker-";
  const size_t prefix_len = sizeof(prefix) - 1;

  struct dirent* ent;
  while ((ent = readdir(d)) != NULL) {
    if (strncmp(ent->d_name, prefix, prefix_len) != 0) continue;
    size_t name_len = strlen(ent->d_name);
    if (name_len < 5 || strcmp(ent->d_name + name_len - 4, ".log") != 0) continue;

    char full_path[1024];
    int written = snprintf(full_path, sizeof(full_path), "%s/%s", dir, ent->d_name);
    if (written <= 0 || (size_t)written >= sizeof(full_path)) continue;

    struct stat st;
    if (lstat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
      if (count < sizeof(entries) / sizeof(entries[0])) {
        snprintf(entries[count].path, sizeof(entries[count].path), "%s", full_path);
        entries[count].mtime = st.st_mtime;
        count++;
      }
    }
  }
  closedir(d);

  if (count <= max_keep) return;

  qsort(entries, count, sizeof(entries[0]), vw_posix_log_cmp);
  size_t to_delete = count - max_keep;
  for (size_t i = 0; i < to_delete; i++) {
    unlink(entries[i].path);
  }
#else
  wchar_t wide_pattern[VW_PATH_MAX_BYTES];
  char pattern[VW_PATH_MAX_BYTES];
  int written = snprintf(pattern, sizeof(pattern), "%s\\vlc-whisper-worker-*.log", dir);
  if (written <= 0 || (size_t)written >= sizeof(pattern)) return;

  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pattern, -1, wide_pattern,
                          (int)(sizeof(wide_pattern) / sizeof(wide_pattern[0]))) <= 0) {
    return;
  }

  WIN32_FIND_DATAW fd;
  HANDLE hFind = FindFirstFileW(wide_pattern, &fd);
  if (hFind == INVALID_HANDLE_VALUE) return;

  vw_win_log_entry_t entries[64];
  size_t count = 0;

  wchar_t wide_dir[VW_PATH_MAX_BYTES];
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, dir, -1, wide_dir,
                          (int)(sizeof(wide_dir) / sizeof(wide_dir[0]))) <= 0) {
    FindClose(hFind);
    return;
  }

  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    if (count < sizeof(entries) / sizeof(entries[0])) {
      _snwprintf(entries[count].path, sizeof(entries[count].path) / sizeof(wchar_t), L"%ls\\%ls", wide_dir,
                 fd.cFileName);
      entries[count].mtime = fd.ftLastWriteTime;
      count++;
    }
  } while (FindNextFileW(hFind, &fd));
  FindClose(hFind);

  if (count <= max_keep) return;

  qsort(entries, count, sizeof(entries[0]), vw_win_log_cmp);
  size_t to_delete = count - max_keep;
  for (size_t i = 0; i < to_delete; i++) {
    DeleteFileW(entries[i].path);
  }
#endif
}

void vw_worker_setup_log_file(const vw_worker_config_t* config) {
  if (!config->logging_enabled) return;
  char path[1024];
  bool default_log = config->log_file[0] == '\0';
  if (!default_log) {
    snprintf(path, sizeof(path), "%s", config->log_file);
  } else {
    const char* dir = vw_worker_default_log_dir();
    // Bound the total default log files: prune older logs keeping at most 4, so this new one makes 5.
    vw_worker_prune_default_logs(dir, 4);
    snprintf(path, sizeof(path), "%s%cvlc-whisper-worker-%lu.log", dir,
#ifdef _WIN32
             '\\', (unsigned long)GetCurrentProcessId()
#else
             '/', (unsigned long)getpid()
#endif
    );
  }

#ifdef _WIN32
  FILE* f = vw_worker_open_log_utf8(path, default_log);
#else
  FILE* f;
  if (default_log) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    f = fd >= 0 ? fdopen(fd, "w") : NULL;
    if (!f && fd >= 0) close(fd);
  } else {
    f = fopen(path, "w");
  }
#endif

  if (f) {
    s_worker_log_file = f;
    s_worker_default_log = default_log;
    snprintf(s_worker_log_path, sizeof(s_worker_log_path), "%s", path);
    vw_log_set_file(f);
  } else {
    fprintf(stderr, "vw_log: failed to open log file '%s'; logging to stderr only\n", path);
  }
}

void vw_worker_teardown_log_file(void) {
  if (s_worker_log_file) {
    vw_log_flush();
    vw_log_set_file(NULL);
    fclose(s_worker_log_file);
    s_worker_log_file = NULL;

    // Remove empty 0-byte default log files on clean shutdown.
    if (s_worker_default_log && s_worker_log_path[0]) {
#ifdef _WIN32
      struct _stat st;
      if (_stat(s_worker_log_path, &st) == 0 && st.st_size == 0) {
        DeleteFileA(s_worker_log_path);
      }
#else
      struct stat st;
      if (stat(s_worker_log_path, &st) == 0 && st.st_size == 0) {
        unlink(s_worker_log_path);
      }
#endif
    }
    s_worker_log_path[0] = '\0';
    s_worker_default_log = false;
  }
}
