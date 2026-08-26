#define _POSIX_C_SOURCE 200809L
#include "vw_model_download.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "vw_sha256.h"

#ifndef _WIN32
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <direct.h>
#include <pthread.h>
#include <windows.h>
#include <winhttp.h>
#endif

#ifdef __STDC_NO_ATOMICS__
#include <stdbool.h>
#define atomic_bool bool
#define atomic_init(p, v) (*(p) = (v))
#define atomic_load(p) (*(p))
#define atomic_store(p, v) (*(p) = (v))
#else
#include <stdatomic.h>
#endif

struct vw_model_download {
  pthread_t thread;
  bool thread_started;
  pthread_mutex_t lock;
  vw_download_progress_t progress;
  vw_model_catalog_entry_t entry;
  char dest_dir[4096];
  char part_path[4096];
  char final_path[4096];
  atomic_bool abort_requested;
#ifndef _WIN32
  pid_t child_pid;
#else
  HINTERNET hSession;
  HINTERNET hConnect;
  HINTERNET hRequest;
#endif
};

static uint64_t vw_file_size(const char* path) {
  struct stat st;
  if (!path || stat(path, &st) != 0) return 0;
  return (uint64_t)st.st_size;
}

static void vw_path_join(char* out, size_t out_size, const char* dir, const char* file) {
  if (!out || out_size == 0) return;
  if (!dir || !dir[0]) {
    snprintf(out, out_size, "%s", file ? file : "");
    return;
  }
  size_t dir_len = strlen(dir);
  bool need_sep = dir_len > 0 && dir[dir_len - 1] != '/' && dir[dir_len - 1] != '\\';
#ifdef _WIN32
  if (need_sep)
    snprintf(out, out_size, "%s\\%s", dir, file ? file : "");
  else
    snprintf(out, out_size, "%s%s", dir, file ? file : "");
#else
  if (need_sep)
    snprintf(out, out_size, "%s/%s", dir, file ? file : "");
  else
    snprintf(out, out_size, "%s%s", dir, file ? file : "");
#endif
}

static bool vw_mkdir_p(const char* path) {
  if (!path || !path[0]) return false;
  char tmp[4096];
  snprintf(tmp, sizeof(tmp), "%s", path);
  size_t len = strlen(tmp);
  if (len == 0) return false;
#ifdef _WIN32
  for (size_t i = 1; i < len; i++) {
    if (tmp[i] == '/' || tmp[i] == '\\') {
      char saved = tmp[i];
      tmp[i] = '\0';
      _mkdir(tmp);
      tmp[i] = saved;
    }
  }
  _mkdir(tmp);
#else
  for (size_t i = 1; i < len; i++) {
    if (tmp[i] == '/') {
      char saved = tmp[i];
      tmp[i] = '\0';
      mkdir(tmp, 0755);
      tmp[i] = saved;
    }
  }
  mkdir(tmp, 0755);
#endif
  struct stat st;
  if (stat(path, &st) != 0) return false;
  return S_ISDIR(st.st_mode);
}

static bool vw_hex_equal_ci(const char* a, const char* b) {
  if (!a || !b) return false;
  size_t la = strlen(a);
  size_t lb = strlen(b);
  if (la != lb) return false;
  for (size_t i = 0; i < la; i++) {
    char ca = a[i];
    char cb = b[i];
    if (ca >= 'A' && ca <= 'F') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'F') cb = (char)(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  return true;
}

static bool vw_sha256_file(const char* path, uint8_t out[32]) {
  if (!path || !out) return false;
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  vw_sha256_context_t ctx;
  vw_sha256_init(&ctx);
  uint8_t buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    vw_sha256_update(&ctx, buf, n);
  }
  if (ferror(f)) {
    fclose(f);
    return false;
  }
  fclose(f);
  vw_sha256_final(&ctx, out);
  return true;
}

static bool vw_rename_atomic(const char* src, const char* dst) {
  if (!src || !dst) return false;
#ifndef _WIN32
  return rename(src, dst) == 0;
#else
  // Use MoveFileExA with replace existing for atomic rename on Windows.
  if (MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING)) return true;
  // Fallback to rename if MoveFileEx fails for cross-volume case.
  if (rename(src, dst) == 0) return true;
  return false;
#endif
}

uint8_t vw_model_download_pct(uint64_t bytes_done, uint64_t bytes_total) {
  if (bytes_total == 0) return 0;
  uint64_t pct = bytes_done * 100ULL / bytes_total;
  if (pct > 100) pct = 100;
  return (uint8_t)pct;
}

#ifndef _WIN32
static void vw_sleep_ms(unsigned int ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (long)(ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}

static bool vw_download_via_curl(vw_model_download_t* dl) {
  pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    // Child: exec curl -fsSL -o <part> <url>
    execlp("curl", "curl", "-fsSL", "--connect-timeout", "15", "--speed-limit", "10240", "--speed-time", "60", "-o",
           dl->part_path, dl->entry.url, (char*)NULL);
    // Also try /usr/bin/curl if execlp fails
    execl("/usr/bin/curl", "curl", "-fsSL", "--connect-timeout", "15", "--speed-limit", "10240", "--speed-time", "60",
          "-o", dl->part_path, dl->entry.url, (char*)NULL);
    _exit(127);
  }
  // Parent: track child pid for abort.
  pthread_mutex_lock(&dl->lock);
  dl->child_pid = pid;
  pthread_mutex_unlock(&dl->lock);

  int status = 0;
  while (1) {
    if (atomic_load(&dl->abort_requested)) {
      kill(pid, SIGTERM);
      // Give it a moment then SIGKILL if still alive.
      vw_sleep_ms(100);
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      pthread_mutex_lock(&dl->lock);
      dl->child_pid = 0;
      pthread_mutex_unlock(&dl->lock);
      return false;
    }
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) break;
    if (r < 0) {
      if (errno == EINTR) continue;
      break;
    }
    // Update progress every 500ms.
    uint64_t sz = vw_file_size(dl->part_path);
    if (sz > dl->entry.bytes) {
      kill(pid, SIGTERM);
      vw_sleep_ms(100);
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      pthread_mutex_lock(&dl->lock);
      dl->child_pid = 0;
      pthread_mutex_unlock(&dl->lock);
      return false;
    }
    uint8_t pct = vw_model_download_pct(sz, dl->entry.bytes);
    pthread_mutex_lock(&dl->lock);
    dl->progress.bytes_done = sz;
    dl->progress.pct = pct;
    pthread_mutex_unlock(&dl->lock);
    vw_sleep_ms(500);
  }
  pthread_mutex_lock(&dl->lock);
  dl->child_pid = 0;
  // Final size update.
  uint64_t sz = vw_file_size(dl->part_path);
  uint8_t pct = vw_model_download_pct(sz, dl->entry.bytes);
  dl->progress.bytes_done = sz;
  dl->progress.pct = pct;
  pthread_mutex_unlock(&dl->lock);

  if (atomic_load(&dl->abort_requested)) return false;
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
  return false;
}
#else
#ifdef _WIN32
// Closes whatever WinHTTP handles are currently stored in dl and clears the
// struct fields under the lock, so abort() and the download thread can never
// double-close the same handle regardless of interleaving.
static void vw_winhttp_close_stored(vw_model_download_t* dl) {
  if (!dl) return;
  pthread_mutex_lock(&dl->lock);
  HINTERNET r = dl->hRequest;
  HINTERNET c = dl->hConnect;
  HINTERNET s = dl->hSession;
  dl->hRequest = NULL;
  dl->hConnect = NULL;
  dl->hSession = NULL;
  pthread_mutex_unlock(&dl->lock);
  if (r) WinHttpCloseHandle(r);
  if (c) WinHttpCloseHandle(c);
  if (s) WinHttpCloseHandle(s);
}
#endif

static bool vw_download_via_winhttp(vw_model_download_t* dl) {
  // Parse URL into host and path for WinHTTP.
  const char* url = dl->entry.url;
  const char* p = strstr(url, "://");
  if (!p) return false;
  p += 3;
  const char* slash = strchr(p, '/');
  char host[256] = {0};
  char path[2048] = {0};
  if (slash) {
    size_t host_len = (size_t)(slash - p);
    if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
    memcpy(host, p, host_len);
    host[host_len] = '\0';
    snprintf(path, sizeof(path), "%s", slash);
  } else {
    snprintf(host, sizeof(host), "%s", p);
    snprintf(path, sizeof(path), "/");
  }
  // Convert to wide strings.
  wchar_t wHost[256];
  wchar_t wPath[2048];
  MultiByteToWideChar(CP_UTF8, 0, host, -1, wHost, 256);
  MultiByteToWideChar(CP_UTF8, 0, path, -1, wPath, 2048);

  HINTERNET hSession = WinHttpOpen(L"vlc-whisper/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
  WinHttpSetTimeouts(hSession, 10000, 10000, 30000, 30000);  // resolve/connect/send/receive; stalled socket aborts
  if (!hSession) return false;
  pthread_mutex_lock(&dl->lock);
  dl->hSession = hSession;
  pthread_mutex_unlock(&dl->lock);
  if (atomic_load(&dl->abort_requested)) {
    vw_winhttp_close_stored(dl);
    return false;
  }
  HINTERNET hConnect = WinHttpConnect(hSession, wHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!hConnect) {
    vw_winhttp_close_stored(dl);
    return false;
  }
  pthread_mutex_lock(&dl->lock);
  dl->hConnect = hConnect;
  pthread_mutex_unlock(&dl->lock);
  HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath, NULL, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!hRequest) {
    vw_winhttp_close_stored(dl);
    return false;
  }
  pthread_mutex_lock(&dl->lock);
  dl->hRequest = hRequest;
  pthread_mutex_unlock(&dl->lock);
  BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (!sent) {
    vw_winhttp_close_stored(dl);
    return false;
  }
  if (!WinHttpReceiveResponse(hRequest, NULL)) {
    vw_winhttp_close_stored(dl);
    return false;
  }
  FILE* out = fopen(dl->part_path, "wb");
  if (!out) {
    vw_winhttp_close_stored(dl);
    return false;
  }
  uint64_t total_written = 0;
  uint8_t buf[65536];
  DWORD bytes_read = 0;
  bool ok = true;
  while (WinHttpReadData(hRequest, buf, sizeof(buf), &bytes_read) && bytes_read > 0) {
    if (atomic_load(&dl->abort_requested)) {
      ok = false;
      break;
    }
    size_t written = fwrite(buf, 1, bytes_read, out);
    if (written != bytes_read) {
      ok = false;
      break;
    }
    total_written += bytes_read;
    if (total_written > dl->entry.bytes) {
      ok = false;
      break;
    }
    uint8_t pct = vw_model_download_pct(total_written, dl->entry.bytes);
    pthread_mutex_lock(&dl->lock);
    dl->progress.bytes_done = total_written;
    dl->progress.pct = pct;
    pthread_mutex_unlock(&dl->lock);
  }
  fclose(out);
  // Cleanup handles (single ownership point; abort() may have closed already).
  vw_winhttp_close_stored(dl);
  // Ensure final progress reflects written size.
  uint64_t sz = vw_file_size(dl->part_path);
  uint8_t pct = vw_model_download_pct(sz, dl->entry.bytes);
  dl->progress.bytes_done = sz;
  dl->progress.pct = pct;
  pthread_mutex_unlock(&dl->lock);
  if (atomic_load(&dl->abort_requested)) return false;
  return ok;
}
#endif

static void* vw_download_thread(void* arg) {
  vw_model_download_t* dl = (vw_model_download_t*)arg;
  pthread_mutex_lock(&dl->lock);
  dl->progress.stage = VW_MODEL_STAGE_DOWNLOADING;
  dl->progress.pct = 0;
  dl->progress.bytes_done = 0;
  dl->progress.bytes_total = dl->entry.bytes;
  pthread_mutex_unlock(&dl->lock);

  vw_mkdir_p(dl->dest_dir);

  for (int attempt = 0; attempt < 2; attempt++) {
    if (atomic_load(&dl->abort_requested)) break;
    bool ok = false;
#ifndef _WIN32
    ok = vw_download_via_curl(dl);
#else
    ok = vw_download_via_winhttp(dl);
#endif
    if (atomic_load(&dl->abort_requested)) {
      unlink(dl->part_path);
#ifdef _WIN32
      DeleteFileA(dl->part_path);
#endif
      pthread_mutex_lock(&dl->lock);
      dl->progress.stage = VW_MODEL_STAGE_ABORTING;
      dl->progress.bytes_done = 0;
      dl->progress.pct = 0;
      pthread_mutex_unlock(&dl->lock);
      // Transition to IDLE after abort cleanup.
      pthread_mutex_lock(&dl->lock);
      dl->progress.stage = VW_MODEL_STAGE_IDLE;
      pthread_mutex_unlock(&dl->lock);
      return NULL;
    }
    if (!ok) {
      unlink(dl->part_path);
#ifdef _WIN32
      DeleteFileA(dl->part_path);
#endif
      // Distinguish size exceed already handled as failure.
      pthread_mutex_lock(&dl->lock);
      dl->progress.stage = VW_MODEL_STAGE_FAILED;
      dl->progress.pct = 0;
      pthread_mutex_unlock(&dl->lock);
      return NULL;
    }
    uint64_t sz = vw_file_size(dl->part_path);
    if (sz > dl->entry.bytes) {
      unlink(dl->part_path);
#ifdef _WIN32
      DeleteFileA(dl->part_path);
#endif
      pthread_mutex_lock(&dl->lock);
      dl->progress.stage = VW_MODEL_STAGE_FAILED;
      pthread_mutex_unlock(&dl->lock);
      return NULL;
    }
    pthread_mutex_lock(&dl->lock);
    dl->progress.stage = VW_MODEL_STAGE_VERIFYING;
    pthread_mutex_unlock(&dl->lock);

    uint8_t hash[32];
    bool hash_ok = vw_sha256_file(dl->part_path, hash);
    if (!hash_ok) {
      unlink(dl->part_path);
#ifdef _WIN32
      DeleteFileA(dl->part_path);
#endif
      if (attempt == 0) {
        pthread_mutex_lock(&dl->lock);
        dl->progress.stage = VW_MODEL_STAGE_DOWNLOADING;
        dl->progress.pct = 0;
        dl->progress.bytes_done = 0;
        pthread_mutex_unlock(&dl->lock);
        continue;
      }
      pthread_mutex_lock(&dl->lock);
      dl->progress.stage = VW_MODEL_STAGE_FAILED;
      pthread_mutex_unlock(&dl->lock);
      return NULL;
    }
    char hex[65];
    vw_sha256_to_hex(hash, hex);
    if (!vw_hex_equal_ci(hex, dl->entry.sha256_hex)) {
      unlink(dl->part_path);
#ifdef _WIN32
      DeleteFileA(dl->part_path);
#endif
      if (attempt == 0) {
        pthread_mutex_lock(&dl->lock);
        dl->progress.stage = VW_MODEL_STAGE_DOWNLOADING;
        dl->progress.pct = 0;
        dl->progress.bytes_done = 0;
        pthread_mutex_unlock(&dl->lock);
        continue;
      }
      pthread_mutex_lock(&dl->lock);
      dl->progress.stage = VW_MODEL_STAGE_FAILED;
      pthread_mutex_unlock(&dl->lock);
      return NULL;
    }
    // Hash matches — atomic rename.
    if (!vw_rename_atomic(dl->part_path, dl->final_path)) {
      unlink(dl->part_path);
#ifdef _WIN32
      DeleteFileA(dl->part_path);
#endif
      pthread_mutex_lock(&dl->lock);
      dl->progress.stage = VW_MODEL_STAGE_FAILED;
      pthread_mutex_unlock(&dl->lock);
      return NULL;
    }
    pthread_mutex_lock(&dl->lock);
    dl->progress.stage = VW_MODEL_STAGE_DONE;
    dl->progress.pct = 100;
    dl->progress.bytes_done = dl->entry.bytes;
    pthread_mutex_unlock(&dl->lock);
    return NULL;
  }
  pthread_mutex_lock(&dl->lock);
  if (dl->progress.stage != VW_MODEL_STAGE_DONE && dl->progress.stage != VW_MODEL_STAGE_IDLE &&
      dl->progress.stage != VW_MODEL_STAGE_ABORTING) {
    // If aborted earlier, stage already IDLE; otherwise mark FAILED.
    if (!atomic_load(&dl->abort_requested)) dl->progress.stage = VW_MODEL_STAGE_FAILED;
  }
  pthread_mutex_unlock(&dl->lock);
  return NULL;
}

vw_model_download_t* vw_model_download_start(const vw_model_catalog_entry_t* entry, const char* dest_dir) {
  if (!entry || !dest_dir || !dest_dir[0]) return NULL;
  if (!entry->id || !entry->filename || !entry->url || !entry->sha256_hex) return NULL;
  vw_model_download_t* dl = (vw_model_download_t*)calloc(1, sizeof(vw_model_download_t));
  if (!dl) return NULL;
  dl->entry = *entry;
  snprintf(dl->dest_dir, sizeof(dl->dest_dir), "%s", dest_dir);
  vw_path_join(dl->part_path, sizeof(dl->part_path), dest_dir, entry->filename);
  // Append .part suffix safely.
  size_t plen = strlen(dl->part_path);
  if (plen + 5 < sizeof(dl->part_path)) {
    memcpy(dl->part_path + plen, ".part", 5);
    dl->part_path[plen + 5] = '\0';
  }
  vw_path_join(dl->final_path, sizeof(dl->final_path), dest_dir, entry->filename);
  atomic_init(&dl->abort_requested, false);
#ifndef _WIN32
  dl->child_pid = 0;
#else
  dl->hSession = NULL;
  dl->hConnect = NULL;
  dl->hRequest = NULL;
#endif
  pthread_mutex_init(&dl->lock, NULL);
  // Initialize progress snapshot.
  dl->progress.stage = VW_MODEL_STAGE_IDLE;
  dl->progress.pct = 0;
  dl->progress.bytes_done = 0;
  dl->progress.bytes_total = entry->bytes;
  memset(dl->progress.model_id, 0, sizeof(dl->progress.model_id));
  snprintf(dl->progress.model_id, sizeof(dl->progress.model_id), "%s", entry->id);

  if (pthread_create(&dl->thread, NULL, vw_download_thread, dl) != 0) {
    pthread_mutex_destroy(&dl->lock);
    free(dl);
    return NULL;
  }
  dl->thread_started = true;
  return dl;
}

void vw_model_download_abort(vw_model_download_t* dl) {
  if (!dl) return;
  atomic_store(&dl->abort_requested, true);
#ifndef _WIN32
  pthread_mutex_lock(&dl->lock);
  pid_t pid = dl->child_pid;
  pthread_mutex_unlock(&dl->lock);
  if (pid > 0) kill(pid, SIGTERM);
#else
  pthread_mutex_lock(&dl->lock);
  HINTERNET hReq = dl->hRequest;
  dl->hRequest = NULL;  // ownership transferred here; thread cleanup skips it
  pthread_mutex_unlock(&dl->lock);
  if (hReq) WinHttpCloseHandle(hReq);
#endif
  pthread_mutex_lock(&dl->lock);
  if (dl->progress.stage != VW_MODEL_STAGE_DONE && dl->progress.stage != VW_MODEL_STAGE_FAILED &&
      dl->progress.stage != VW_MODEL_STAGE_IDLE) {
    dl->progress.stage = VW_MODEL_STAGE_ABORTING;
  }
  pthread_mutex_unlock(&dl->lock);
}

bool vw_model_download_poll(vw_model_download_t* dl, vw_download_progress_t* out) {
  if (!dl || !out) return false;
  pthread_mutex_lock(&dl->lock);
  *out = dl->progress;
  pthread_mutex_unlock(&dl->lock);
  return true;
}

void vw_model_download_free(vw_model_download_t* dl) {
  if (!dl) return;
  if (dl->thread_started) {
    pthread_join(dl->thread, NULL);
    dl->thread_started = false;
  }
#ifndef _WIN32
  if (dl->child_pid > 0) {
    kill(dl->child_pid, SIGTERM);
    int st = 0;
    waitpid(dl->child_pid, &st, 0);
    dl->child_pid = 0;
  }
#endif
  pthread_mutex_destroy(&dl->lock);
  free(dl);
}

bool vw_model_download_default_dir(char* out, size_t out_size) {
  if (!out || out_size == 0) return false;
#ifdef _WIN32
  const char* base = getenv("LOCALAPPDATA");
  char tmp[4096];
  if (base && base[0]) {
    snprintf(tmp, sizeof(tmp), "%s\\vlc-whisper\\models", base);
  } else {
    const char* home = getenv("USERPROFILE");
    if (home && home[0])
      snprintf(tmp, sizeof(tmp), "%s\\AppData\\Local\\vlc-whisper\\models", home);
    else
      snprintf(tmp, sizeof(tmp), ".\\vlc-whisper\\models");
  }
  snprintf(out, out_size, "%s", tmp);
#else
  const char* xdg = getenv("XDG_DATA_HOME");
  char tmp[4096];
  if (xdg && xdg[0]) {
    snprintf(tmp, sizeof(tmp), "%s/vlc-whisper/models", xdg);
  } else {
    const char* home = getenv("HOME");
    if (home && home[0])
      snprintf(tmp, sizeof(tmp), "%s/.local/share/vlc-whisper/models", home);
    else
      snprintf(tmp, sizeof(tmp), "/tmp/vlc-whisper/models");
  }
  snprintf(out, out_size, "%s", tmp);
#endif
  out[out_size - 1] = '\0';
  if (!vw_mkdir_p(out)) return false;
  return true;
}

void vw_model_download_cleanup_partial(const char* dest_dir) {
  if (!dest_dir || !dest_dir[0]) return;
#ifndef _WIN32
  DIR* d = opendir(dest_dir);
  if (!d) return;
  struct dirent* e;
  while ((e = readdir(d)) != NULL) {
    size_t n = strlen(e->d_name);
    if (n > 5 && strcmp(e->d_name + n - 5, ".part") == 0) {
      char full[4096];
      vw_path_join(full, sizeof(full), dest_dir, e->d_name);
      unlink(full);
    }
  }
  closedir(d);
#else
  char pattern[4096];
  snprintf(pattern, sizeof(pattern), "%s\\*.part", dest_dir);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    char full[4096];
    vw_path_join(full, sizeof(full), dest_dir, fd.cFileName);
    DeleteFileA(full);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
#endif
}
