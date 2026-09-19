#include "vw_settings_file.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#define VW_SETTINGS_JSON_MAX 32768
#define VW_SETTINGS_PATH_MAX 4096

static char* vw_settings_strdup(const char* value) {
  if (!value) return NULL;
  size_t n = strlen(value) + 1;
  char* copy = (char*)malloc(n);
  if (copy) memcpy(copy, value, n);
  return copy;
}

#ifdef _WIN32
static bool vw_settings_build_wpath(const wchar_t* suffix, wchar_t* out, size_t out_count) {
  if (!suffix || !out || out_count == 0) return false;
  wchar_t base[VW_SETTINGS_PATH_MAX];
  DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, (DWORD)(sizeof(base) / sizeof(base[0])));
  if (n == 0 || n >= sizeof(base) / sizeof(base[0])) {
    wchar_t home[VW_SETTINGS_PATH_MAX];
    n = GetEnvironmentVariableW(L"USERPROFILE", home, (DWORD)(sizeof(home) / sizeof(home[0])));
    if (n == 0 || n >= sizeof(home) / sizeof(home[0])) return false;
    if (swprintf(base, sizeof(base) / sizeof(base[0]), L"%ls\\AppData\\Local", home) < 0) return false;
  }
  int written = swprintf(out, out_count, L"%ls\\vlc-whisper\\%ls", base, suffix);
  return written > 0 && (size_t)written < out_count;
}

static bool vw_settings_ensure_dir(void) {
  wchar_t marker[VW_SETTINGS_PATH_MAX];
  if (!vw_settings_build_wpath(L".", marker, sizeof(marker) / sizeof(marker[0]))) return false;
  wchar_t* slash = wcsrchr(marker, L'\\');
  if (!slash) return false;
  *slash = L'\0';
  if (CreateDirectoryW(marker, NULL)) return true;
  return GetLastError() == ERROR_ALREADY_EXISTS;
}

static FILE* vw_settings_open(const wchar_t* suffix, const wchar_t* mode) {
  wchar_t path[VW_SETTINGS_PATH_MAX];
  if (!vw_settings_build_wpath(suffix, path, sizeof(path) / sizeof(path[0]))) return NULL;
  return _wfopen(path, mode);
}

static bool vw_settings_suffix_exists(const wchar_t* suffix) {
  wchar_t path[VW_SETTINGS_PATH_MAX];
  if (!vw_settings_build_wpath(suffix, path, sizeof(path) / sizeof(path[0]))) return false;
  DWORD attr = GetFileAttributesW(path);
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static void vw_settings_delete_suffix(const wchar_t* suffix) {
  wchar_t path[VW_SETTINGS_PATH_MAX];
  if (vw_settings_build_wpath(suffix, path, sizeof(path) / sizeof(path[0]))) DeleteFileW(path);
}

static bool vw_settings_utf8_file_exists(const char* path) {
  if (!path || !path[0]) return false;
  wchar_t wide[VW_SETTINGS_PATH_MAX];
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, (int)(sizeof(wide) / sizeof(wide[0])));
  if (n <= 0) return false;
  DWORD attr = GetFileAttributesW(wide);
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}
#else
static bool vw_settings_build_path(const char* suffix, char* out, size_t out_size) {
  if (!suffix || !out || out_size == 0) return false;
  const char* xdg = getenv("XDG_CONFIG_HOME");
  const char* home = getenv("HOME");
  int written = 0;
  if (xdg && xdg[0]) {
    written = snprintf(out, out_size, "%s/vlc-whisper/%s", xdg, suffix);
  } else if (home && home[0]) {
    written = snprintf(out, out_size, "%s/.config/vlc-whisper/%s", home, suffix);
  } else {
    return false;
  }
  return written > 0 && (size_t)written < out_size;
}

static bool vw_settings_ensure_dir(void) {
  char path[VW_SETTINGS_PATH_MAX];
  if (!vw_settings_build_path(".", path, sizeof(path))) return false;
  char* slash = strrchr(path, '/');
  if (!slash) return false;
  *slash = '\0';

  char parent[VW_SETTINGS_PATH_MAX];
  if (snprintf(parent, sizeof(parent), "%s", path) >= (int)sizeof(parent)) return false;
  slash = strrchr(parent, '/');
  if (slash) {
    *slash = '\0';
    if (mkdir(parent, 0700) != 0 && errno != EEXIST) return false;
  }
  if (mkdir(path, 0700) == 0) return true;
  return errno == EEXIST;
}

static FILE* vw_settings_open(const char* suffix, const char* mode) {
  char path[VW_SETTINGS_PATH_MAX];
  if (!vw_settings_build_path(suffix, path, sizeof(path))) return NULL;
  return fopen(path, mode);
}

static bool vw_settings_suffix_exists(const char* suffix) {
  char path[VW_SETTINGS_PATH_MAX];
  if (!vw_settings_build_path(suffix, path, sizeof(path))) return false;
  return access(path, F_OK) == 0;
}

static void vw_settings_delete_suffix(const char* suffix) {
  char path[VW_SETTINGS_PATH_MAX];
  if (vw_settings_build_path(suffix, path, sizeof(path))) unlink(path);
}

static bool vw_settings_utf8_file_exists(const char* path) {
  return path && path[0] && access(path, F_OK) == 0;
}
#endif

static bool vw_settings_read_named(const char* name, char* out, size_t out_size) {
  if (!name || !out || out_size < 2) return false;
#ifdef _WIN32
  wchar_t wide_name[128];
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide_name,
                          (int)(sizeof(wide_name) / sizeof(wide_name[0]))) <= 0)
    return false;
  FILE* f = vw_settings_open(wide_name, L"rb");
#else
  FILE* f = vw_settings_open(name, "rb");
#endif
  if (!f) return false;
  size_t n = fread(out, 1, out_size - 1, f);
  int extra = fgetc(f);
  bool ok = !ferror(f) && extra == EOF && memchr(out, '\0', n) == NULL;
  fclose(f);
  out[n] = '\0';
  return ok;
}

static bool vw_settings_write_named(const char* name, const char* value) {
  if (!name || !value || !vw_settings_ensure_dir()) return false;
#ifdef _WIN32
  wchar_t wide_name[128];
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide_name,
                          (int)(sizeof(wide_name) / sizeof(wide_name[0]))) <= 0)
    return false;
  FILE* f = vw_settings_open(wide_name, L"wb");
#else
  FILE* f = vw_settings_open(name, "wb");
#endif
  if (!f) return false;
  size_t len = strlen(value);
  bool wrote = fwrite(value, 1, len, f) == len;
  bool closed = fclose(f) == 0;
  return wrote && closed;
}

static bool vw_settings_named_exists(const char* name) {
#ifdef _WIN32
  wchar_t wide_name[128];
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide_name,
                          (int)(sizeof(wide_name) / sizeof(wide_name[0]))) <= 0)
    return false;
  return vw_settings_suffix_exists(wide_name);
#else
  return vw_settings_suffix_exists(name);
#endif
}

static void vw_settings_delete_named(const char* name) {
#ifdef _WIN32
  wchar_t wide_name[128];
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide_name,
                          (int)(sizeof(wide_name) / sizeof(wide_name[0]))) <= 0)
    return;
  vw_settings_delete_suffix(wide_name);
#else
  vw_settings_delete_suffix(name);
#endif
}


static const char* vw_json_skip_space(const char* p) {
  while (*p && isspace((unsigned char)*p)) p++;
  return p;
}

static bool vw_json_parse_value(const char** cursor, unsigned depth);

static bool vw_json_parse_string_value(const char** cursor) {
  const char* p = *cursor;
  if (*p++ != '"') return false;
  while (*p && *p != '"') {
    unsigned char c = (unsigned char)*p++;
    if (c < 0x20) return false;
    if (c != '\\') continue;
    char escape = *p++;
    if (!escape) return false;
    if (strchr("\"\\/bfnrt", escape)) continue;
    if (escape != 'u') return false;
    for (int i = 0; i < 4; ++i) {
      if (!isxdigit((unsigned char)*p++)) return false;
    }
  }
  if (*p++ != '"') return false;
  *cursor = p;
  return true;
}

static bool vw_json_parse_number(const char** cursor) {
  const char* p = *cursor;
  if (*p == '-') p++;
  if (*p == '0') {
    p++;
  } else {
    if (*p < '1' || *p > '9') return false;
    while (isdigit((unsigned char)*p)) p++;
  }
  if (*p == '.') {
    p++;
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) p++;
  }
  if (*p == 'e' || *p == 'E') {
    p++;
    if (*p == '+' || *p == '-') p++;
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) p++;
  }
  *cursor = p;
  return true;
}

static bool vw_json_parse_array(const char** cursor, unsigned depth) {
  const char* p = vw_json_skip_space(*cursor + 1);
  if (*p == ']') {
    *cursor = p + 1;
    return true;
  }
  for (;;) {
    if (!vw_json_parse_value(&p, depth + 1)) return false;
    p = vw_json_skip_space(p);
    if (*p == ']') {
      *cursor = p + 1;
      return true;
    }
    if (*p++ != ',') return false;
    p = vw_json_skip_space(p);
  }
}

static bool vw_json_parse_object(const char** cursor, unsigned depth) {
  const char* p = vw_json_skip_space(*cursor + 1);
  if (*p == '}') {
    *cursor = p + 1;
    return true;
  }
  for (;;) {
    if (!vw_json_parse_string_value(&p)) return false;
    p = vw_json_skip_space(p);
    if (*p++ != ':') return false;
    p = vw_json_skip_space(p);
    if (!vw_json_parse_value(&p, depth + 1)) return false;
    p = vw_json_skip_space(p);
    if (*p == '}') {
      *cursor = p + 1;
      return true;
    }
    if (*p++ != ',') return false;
    p = vw_json_skip_space(p);
  }
}

static bool vw_json_parse_value(const char** cursor, unsigned depth) {
  if (depth > 32) return false;
  const char* p = vw_json_skip_space(*cursor);
  if (*p == '"') {
    if (!vw_json_parse_string_value(&p)) return false;
  } else if (*p == '{') {
    if (!vw_json_parse_object(&p, depth)) return false;
  } else if (*p == '[') {
    if (!vw_json_parse_array(&p, depth)) return false;
  } else if (*p == '-' || isdigit((unsigned char)*p)) {
    if (!vw_json_parse_number(&p)) return false;
  } else if (strncmp(p, "true", 4) == 0) {
    p += 4;
  } else if (strncmp(p, "false", 5) == 0) {
    p += 5;
  } else if (strncmp(p, "null", 4) == 0) {
    p += 4;
  } else {
    return false;
  }
  *cursor = p;
  return true;
}

static bool vw_json_document_valid(const char* json) {
  if (!json) return false;
  const char* p = vw_json_skip_space(json);
  if (*p != '{' || !vw_json_parse_object(&p, 0)) return false;
  return *vw_json_skip_space(p) == '\0';
}

static const char* vw_json_value(const char* json, const char* key) {
  if (!json || !key) return NULL;
  char needle[128];
  int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
  if (n <= 0 || (size_t)n >= sizeof(needle)) return NULL;
  const char* p = strstr(json, needle);
  if (!p) return NULL;
  p += strlen(needle);
  while (*p && isspace((unsigned char)*p)) p++;
  if (*p++ != ':') return NULL;
  while (*p && isspace((unsigned char)*p)) p++;
  return p;
}

static bool vw_json_string(const char* json, const char* key, char* out, size_t out_size) {
  const char* p = vw_json_value(json, key);
  if (!p || *p++ != '"' || out_size == 0) return false;
  size_t used = 0;
  while (*p && *p != '"') {
    char c = *p++;
    if (c == '\\') {
      char esc = *p++;
      if (esc == '\\' || esc == '"' || esc == '/')
        c = esc;
      else if (esc == 'n')
        c = '\n';
      else if (esc == 'r')
        c = '\r';
      else if (esc == 't')
        c = '\t';
      else
        return false;
    }
    if (used + 1 >= out_size) return false;
    out[used++] = c;
  }
  if (*p != '"') return false;
  out[used] = '\0';
  return true;
}

static bool vw_json_int(const char* json, const char* key, int64_t* out) {
  const char* p = vw_json_value(json, key);
  if (!p || !out) return false;
  char* end = NULL;
  long long value = strtoll(p, &end, 10);
  if (end == p) return false;
  *out = (int64_t)value;
  return true;
}

static bool vw_json_bool(const char* json, const char* key, bool* out) {
  const char* p = vw_json_value(json, key);
  if (!p || !out) return false;
  if (strncmp(p, "true", 4) == 0) {
    *out = true;
    return true;
  }
  if (strncmp(p, "false", 5) == 0) {
    *out = false;
    return true;
  }
  return false;
}

static bool vw_one_of(const char* value, const char* const* values, size_t count) {
  if (!value) return false;
  for (size_t i = 0; i < count; ++i) {
    if (strcmp(value, values[i]) == 0) return true;
  }
  return false;
}

static const char* vw_basename(const char* path) {
  const char* base = path ? path : "";
  if (!path) return base;
  for (const char* p = path; *p; ++p) {
    if (*p == '/' || *p == '\\') base = p + 1;
  }
  return base;
}

static bool vw_valid_model_path(const char* path) {
  static const char* const files[] = {"ggml-tiny.en.bin", "ggml-tiny.bin", "ggml-base.en.bin", "ggml-base.bin",
                                      "ggml-small.bin", "ggml-medium.bin", "ggml-large-v3.bin"};
  const char* base = vw_basename(path);
  return vw_one_of(base, files, sizeof(files) / sizeof(files[0]));
}

static bool vw_valid_command(const char* value) {
  static const char* const values[] = {"abort", "tiny.en", "tiny", "base.en", "base", "small", "medium", "large"};
  return vw_one_of(value, values, sizeof(values) / sizeof(values[0]));
}

typedef enum {
  VW_SETTINGS_UNAVAILABLE = 0,
  VW_SETTINGS_VALID,
  VW_SETTINGS_INVALID,
} vw_settings_read_result_t;

static vw_settings_read_result_t vw_read_settings(char* json, size_t size) {
  if (!vw_settings_read_named("settings.json", json, size)) return VW_SETTINGS_UNAVAILABLE;
  return vw_json_document_valid(json) ? VW_SETTINGS_VALID : VW_SETTINGS_INVALID;
}

static bool vw_reset_pending(void) {
  return vw_settings_named_exists("reset-settings");
}

static const char* vw_default_string(const char* key) {
  if (strcmp(key, "whisper-backend") == 0) return "auto";
  if (strcmp(key, "model-path") == 0) return "models/ggml-tiny.bin";
  if (strcmp(key, "whisper-language") == 0) return "en";
  if (strcmp(key, "whisper-translate-from") == 0) return "auto";
  if (strcmp(key, "whisper-translate-to") == 0) return "en";
  return NULL;
}

static bool vw_default_int(const char* key, int64_t* value) {
  if (strcmp(key, "whisper-threads") == 0) {
    *value = 4;
    return true;
  }
  if (strcmp(key, "whisper-logging") == 0 || strcmp(key, "whisper-translate-enabled") == 0) {
    *value = 0;
    return true;
  }
  if (strcmp(key, "whisper-show-paused") == 0 || strcmp(key, "whisper-translate-mode") == 0) {
    *value = 1;
    return true;
  }
  return false;
}

char* vw_settings_override_psz(const char* key, char* fallback) {
  if (!key) return fallback;
  if (strcmp(key, "whisper-model-download") == 0) {
    char command[64];
    if (vw_settings_read_named("model-command", command, sizeof(command))) {
      command[strcspn(command, "\r\n")] = '\0';
      if (vw_valid_command(command)) {
        char* copy = vw_settings_strdup(command);
        if (!copy) return fallback;
        free(fallback);
        return copy;
      }
    }
    return fallback;
  }

  const char* default_value = vw_default_string(key);
  if (!default_value) return fallback;

  char selected[VW_SETTINGS_PATH_MAX];
  char json[VW_SETTINGS_JSON_MAX];
  vw_settings_read_result_t read_result = vw_read_settings(json, sizeof(json));
  bool has_json = read_result == VW_SETTINGS_VALID;
  if (has_json) {
    if (!vw_json_string(json, key, selected, sizeof(selected))) return fallback;
  } else if (read_result == VW_SETTINGS_INVALID || vw_reset_pending()) {
    snprintf(selected, sizeof(selected), "%s", default_value);
  } else {
    return fallback;
  }

  static const char* const backends[] = {"auto", "gpu", "cpu"};
  static const char* const languages[] = {"en", "ro", "tr", "de", "fr", "es"};
  static const char* const sources[] = {"auto", "en", "ro", "es", "fr", "de", "it", "pt",
                                        "ru",   "uk", "tr", "ja", "ko", "zh"};
  static const char* const targets[] = {"en", "ro", "es", "fr", "de", "it", "pt",
                                        "ru", "uk", "tr", "ja", "ko", "zh"};
  bool valid = false;
  if (strcmp(key, "whisper-backend") == 0)
    valid = vw_one_of(selected, backends, sizeof(backends) / sizeof(backends[0]));
  else if (strcmp(key, "model-path") == 0)
    valid = vw_valid_model_path(selected);
  else if (strcmp(key, "whisper-language") == 0)
    valid = vw_one_of(selected, languages, sizeof(languages) / sizeof(languages[0]));
  else if (strcmp(key, "whisper-translate-from") == 0)
    valid = vw_one_of(selected, sources, sizeof(sources) / sizeof(sources[0]));
  else if (strcmp(key, "whisper-translate-to") == 0)
    valid = vw_one_of(selected, targets, sizeof(targets) / sizeof(targets[0]));
  if (!valid) return fallback;

  if (has_json && strcmp(key, "model-path") == 0) {
    char active[VW_SETTINGS_PATH_MAX];
    if (vw_settings_read_named("model-path-active", active, sizeof(active))) {
      active[strcspn(active, "\r\n")] = '\0';
      if (strcmp(vw_basename(active), vw_basename(selected)) == 0 && vw_settings_utf8_file_exists(active)) {
        snprintf(selected, sizeof(selected), "%s", active);
      }
    }
  }

  char* copy = vw_settings_strdup(selected);
  if (!copy) return fallback;
  free(fallback);
  return copy;
}

int64_t vw_settings_override_int(const char* key, int64_t fallback) {
  if (!key) return fallback;
  int64_t default_value = 0;
  if (!vw_default_int(key, &default_value)) return fallback;

  char json[VW_SETTINGS_JSON_MAX];
  vw_settings_read_result_t read_result = vw_read_settings(json, sizeof(json));
  if (read_result != VW_SETTINGS_VALID)
    return read_result == VW_SETTINGS_INVALID || vw_reset_pending() ? default_value : fallback;

  if (strcmp(key, "whisper-threads") == 0 || strcmp(key, "whisper-translate-mode") == 0) {
    int64_t value = 0;
    if (!vw_json_int(json, key, &value)) return fallback;
    if (strcmp(key, "whisper-threads") == 0) return (value >= 1 && value <= 16) ? value : 4;
    return value == 0 ? 0 : 1;
  }

  bool value = false;
  if (!vw_json_bool(json, key, &value)) return fallback;
  if (value && strcmp(key, "whisper-translate-enabled") == 0) {
    char effective[8];
    if (vw_settings_read_named("translate-enabled-effective", effective, sizeof(effective))) {
      effective[strcspn(effective, "\r\n")] = '\0';
      if (strcmp(effective, "0") == 0) return 0;
    }
  }
  return value ? 1 : 0;
}

void vw_settings_ack_model_command(const char* consumed) {
  if (!consumed || !vw_valid_command(consumed)) return;
  char current[64];
  if (!vw_settings_read_named("model-command", current, sizeof(current))) return;
  current[strcspn(current, "\r\n")] = '\0';
  if (strcmp(current, consumed) == 0) vw_settings_delete_named("model-command");
}

void vw_settings_note_psz(const char* key, const char* value) {
  if (!key || !value) return;
  if (strcmp(key, "whisper-backend-active") == 0) {
    vw_settings_write_named("backend-active", value);
  } else if (strcmp(key, "whisper-model-status") == 0) {
    vw_settings_write_named("model-status", value);
  } else if (strcmp(key, "model-path") == 0 && vw_valid_model_path(value) && vw_settings_utf8_file_exists(value)) {
    vw_settings_write_named("model-path-active", value);
  }
}

void vw_settings_note_int(const char* key, int64_t value) {
  if (!key) return;
  if (strcmp(key, "whisper-translate-enabled") == 0) {
    vw_settings_write_named("translate-enabled-effective", value ? "1" : "0");
    return;
  }
  if (strcmp(key, "whisper-model-progress") != 0) return;
  char text[32];
  snprintf(text, sizeof(text), "%lld", (long long)value);
  vw_settings_write_named("model-progress", text);
}