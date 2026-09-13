// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#else
#include <direct.h>
#include <io.h>
#include <windows.h>
#endif

#include "vw_test.h"
#include "vw_worker_log_policy.h"

static bool file_exists(const char* path) {
#ifdef _WIN32
  return _access(path, 0) == 0;
#else
  struct stat st;
  return stat(path, &st) == 0;
#endif
}

static void create_file_with_mtime(const char* path, int mtime_offset) {
  FILE* f = fopen(path, "w");
  if (f) {
    fputs("log line\n", f);
    fclose(f);
  }
#ifndef _WIN32
  struct utimbuf tb;
  tb.actime = 1700000000 + mtime_offset;
  tb.modtime = 1700000000 + mtime_offset;
  utime(path, &tb);
#else
  HANDLE h = CreateFileA(path, FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h != INVALID_HANDLE_VALUE) {
    FILETIME ft;
    // 100-nanosecond intervals since Jan 1, 1601; offset each file
    ULARGE_INTEGER uli;
    uli.QuadPart = 133000000000000000ULL + ((ULONGLONG)mtime_offset * 10000000ULL);
    ft.dwLowDateTime = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;
    SetFileTime(h, NULL, NULL, &ft);
    CloseHandle(h);
  }
#endif
}

int main(void) {
  char temp_dir[512];
#ifdef _WIN32
  const char* base = getenv("TEMP");
  if (!base) base = "C:\\Windows\\Temp";
  snprintf(temp_dir, sizeof(temp_dir), "%s\\vw_test_log_prune_%lu", base, (unsigned long)GetCurrentProcessId());
  _mkdir(temp_dir);
#else
  snprintf(temp_dir, sizeof(temp_dir), "/tmp/vw_test_log_prune_%ld", (long)getpid());
  mkdir(temp_dir, 0700);
#endif

  // 1. Create 6 log files with strictly increasing timestamps
  char f1[576], f2[576], f3[576], f4[576], f5[576], f6[576];
  char f_other[576], f_txt[576];
#ifdef _WIN32
  const char sep = '\\';
#else
  const char sep = '/';
#endif
  snprintf(f1, sizeof(f1), "%s%cvlc-whisper-worker-101.log", temp_dir, sep);
  snprintf(f2, sizeof(f2), "%s%cvlc-whisper-worker-102.log", temp_dir, sep);
  snprintf(f3, sizeof(f3), "%s%cvlc-whisper-worker-103.log", temp_dir, sep);
  snprintf(f4, sizeof(f4), "%s%cvlc-whisper-worker-104.log", temp_dir, sep);
  snprintf(f5, sizeof(f5), "%s%cvlc-whisper-worker-105.log", temp_dir, sep);
  snprintf(f6, sizeof(f6), "%s%cvlc-whisper-worker-106.log", temp_dir, sep);
  snprintf(f_other, sizeof(f_other), "%s%cother-worker-log.log", temp_dir, sep);
  snprintf(f_txt, sizeof(f_txt), "%s%cvlc-whisper-worker-notes.txt", temp_dir, sep);

  create_file_with_mtime(f1, 10);
  create_file_with_mtime(f2, 20);
  create_file_with_mtime(f3, 30);
  create_file_with_mtime(f4, 40);
  create_file_with_mtime(f5, 50);
  create_file_with_mtime(f6, 60);
  create_file_with_mtime(f_other, 5);
  create_file_with_mtime(f_txt, 70);

  // 2. Prune keeping at most 3 newest logs
  vw_worker_prune_default_logs(temp_dir, 3);

  // 3. Verify oldest 3 were deleted, newest 3 retained, and non-matching files preserved
  vw_test_check_false("oldest worker log 101 pruned", file_exists(f1));
  vw_test_check_false("older worker log 102 pruned", file_exists(f2));
  vw_test_check_false("older worker log 103 pruned", file_exists(f3));
  vw_test_check_true("new worker log 104 retained", file_exists(f4));
  vw_test_check_true("new worker log 105 retained", file_exists(f5));
  vw_test_check_true("newest worker log 106 retained", file_exists(f6));
  vw_test_check_true("non-matching other log retained", file_exists(f_other));
  vw_test_check_true("non-matching txt file retained", file_exists(f_txt));

  // Cleanup
#ifdef _WIN32
  DeleteFileA(f4);
  DeleteFileA(f5);
  DeleteFileA(f6);
  DeleteFileA(f_other);
  DeleteFileA(f_txt);
  _rmdir(temp_dir);
#else
  unlink(f4);
  unlink(f5);
  unlink(f6);
  unlink(f_other);
  unlink(f_txt);
  rmdir(temp_dir);
#endif

  return vw_test_finish("test_worker_log_pruning");
}
