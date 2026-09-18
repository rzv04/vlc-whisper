// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "vw_translate.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#define VW_TRANSLATE_USER_AGENT \
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"
#define VW_TRANSLATE_RPC_JSON_BYTES (VW_TRANSLATE_MAX_TEXT_BYTES * 8U + 512U)
#define VW_TRANSLATE_RPC_BODY_BYTES 65536U

static _Thread_local uint8_t g_http_failure_cause = VW_TRANSLATE_FAILURE_TRANSPORT;
static _Thread_local uint16_t g_http_provider_status = 0;

static void vw_translate_http_failure_reset(void) {
  g_http_failure_cause = VW_TRANSLATE_FAILURE_TRANSPORT;
  g_http_provider_status = 0;
}

static int64_t get_monotonic_us(void) {
#ifdef _WIN32
  LARGE_INTEGER freq;
  LARGE_INTEGER counter;
  if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0 || !QueryPerformanceCounter(&counter)) return 0;
  // Quotient/remainder conversion avoids signed overflow from counter * 1,000,000 on long-running systems.
  int64_t seconds = counter.QuadPart / freq.QuadPart;
  int64_t remainder = counter.QuadPart % freq.QuadPart;
  return seconds * 1000000LL + (remainder * 1000000LL) / freq.QuadPart;
#else
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
#endif
}

static uint32_t elapsed_to_u32(int64_t started_us) {
  int64_t elapsed = get_monotonic_us() - started_us;
  if (elapsed <= 0) return 0;
  if ((uint64_t)elapsed > UINT32_MAX) return UINT32_MAX;
  return (uint32_t)elapsed;
}

static uint32_t remaining_timeout_ms(int64_t deadline_us) {
  int64_t remaining_us = deadline_us - get_monotonic_us();
  if (remaining_us <= 0) return 0;
  uint64_t rounded_ms = ((uint64_t)remaining_us + 999U) / 1000U;
  if (rounded_ms > UINT32_MAX) return UINT32_MAX;
  return (uint32_t)(rounded_ms ? rounded_ms : 1U);
}

bool vw_url_encode(const char* src, char* dst, size_t dst_size) {
  if (!src || !dst || dst_size == 0) return false;

  size_t dst_idx = 0;
  for (size_t i = 0; src[i] != '\0'; i++) {
    unsigned char c = (unsigned char)src[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      if (dst_idx + 1 >= dst_size) return false;
      dst[dst_idx++] = (char)c;
    } else {
      if (dst_idx + 3 >= dst_size) return false;
      static const char hex[] = "0123456789ABCDEF";
      dst[dst_idx++] = '%';
      dst[dst_idx++] = hex[c >> 4];
      dst[dst_idx++] = hex[c & 0x0F];
    }
  }
  dst[dst_idx] = '\0';
  return true;
}

static bool append_utf8_scalar(uint32_t code, char* dst, size_t dst_size, size_t* dst_idx) {
  if (!dst || !dst_idx || code == 0 || code > 0x10FFFFU || (code >= 0xD800U && code <= 0xDFFFU)) return false;
  size_t need = code <= 0x7FU ? 1U : (code <= 0x7FFU ? 2U : (code <= 0xFFFFU ? 3U : 4U));
  if (*dst_idx + need >= dst_size) return false;
  if (need == 1U) {
    dst[(*dst_idx)++] = (char)code;
  } else if (need == 2U) {
    dst[(*dst_idx)++] = (char)(0xC0U | (code >> 6));
    dst[(*dst_idx)++] = (char)(0x80U | (code & 0x3FU));
  } else if (need == 3U) {
    dst[(*dst_idx)++] = (char)(0xE0U | (code >> 12));
    dst[(*dst_idx)++] = (char)(0x80U | ((code >> 6) & 0x3FU));
    dst[(*dst_idx)++] = (char)(0x80U | (code & 0x3FU));
  } else {
    dst[(*dst_idx)++] = (char)(0xF0U | (code >> 18));
    dst[(*dst_idx)++] = (char)(0x80U | ((code >> 12) & 0x3FU));
    dst[(*dst_idx)++] = (char)(0x80U | ((code >> 6) & 0x3FU));
    dst[(*dst_idx)++] = (char)(0x80U | (code & 0x3FU));
  }
  return true;
}

bool vw_html_unescape(const char* src, char* dst, size_t dst_size) {
  if (!src || !dst || dst_size == 0) return false;

  size_t dst_idx = 0;
  bool in_tag = false;
  for (size_t i = 0; src[i] != '\0'; i++) {
    if (src[i] == '<') {
      in_tag = true;
      continue;
    }
    if (src[i] == '>') {
      in_tag = false;
      continue;
    }
    if (in_tag) continue;

    if (src[i] == '&') {
      const struct {
        const char* entity;
        char value;
      } named[] = {{"&quot;", '"'}, {"&#39;", '\''}, {"&#039;", '\''}, {"&amp;", '&'},
                   {"&lt;", '<'},   {"&gt;", '>'},   {"&nbsp;", ' '}};
      bool matched = false;
      for (size_t n = 0; n < sizeof(named) / sizeof(named[0]); n++) {
        size_t len = strlen(named[n].entity);
        if (strncmp(src + i, named[n].entity, len) == 0) {
          if (dst_idx + 1 >= dst_size) return false;
          dst[dst_idx++] = named[n].value;
          i += len - 1;
          matched = true;
          break;
        }
      }
      if (matched) continue;

      if (src[i + 1] == '#') {
        size_t end_idx = i + 2;
        while (src[end_idx] && src[end_idx] != ';' && end_idx - i < 12U) end_idx++;
        if (src[end_idx] == ';') {
          bool hex = src[i + 2] == 'x' || src[i + 2] == 'X';
          size_t digits = i + (hex ? 3U : 2U);
          size_t digit_count = end_idx - digits;
          if (digit_count > 0 && digit_count < 9U) {
            char number[10];
            memcpy(number, src + digits, digit_count);
            number[digit_count] = '\0';
            char* parse_end = NULL;
            unsigned long code = strtoul(number, &parse_end, hex ? 16 : 10);
            if (parse_end && *parse_end == '\0' && code <= 0x10FFFFUL &&
                append_utf8_scalar((uint32_t)code, dst, dst_size, &dst_idx)) {
              i = end_idx;
              continue;
            }
          }
        }
      }
    }

    if (dst_idx + 1 >= dst_size) return false;
    dst[dst_idx++] = src[i];
  }
  dst[dst_idx] = '\0';
  return true;
}

static bool parse_hex4(const char* src, uint32_t* out) {
  if (!src || !out) return false;
  uint32_t value = 0;
  for (int i = 0; i < 4; i++) {
    unsigned char c = (unsigned char)src[i];
    uint32_t digit;
    if (c >= '0' && c <= '9') {
      digit = (uint32_t)(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      digit = (uint32_t)(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      digit = (uint32_t)(c - 'A' + 10);
    } else {
      return false;
    }
    value = (value << 4) | digit;
  }
  *out = value;
  return true;
}

static bool json_unescape_string(const char* src, size_t src_len, char* dst, size_t dst_size, size_t* out_len) {
  if (!src || !dst || dst_size == 0) return false;
  size_t dst_idx = 0;
  for (size_t i = 0; i < src_len; i++) {
    unsigned char raw = (unsigned char)src[i];
    if (raw < 0x20U) return false;
    if (src[i] != '\\') {
      if (dst_idx + 1 >= dst_size) return false;
      dst[dst_idx++] = src[i];
      continue;
    }
    if (++i >= src_len) return false;
    char c = src[i];
    char decoded = '\0';
    if (c == '"' || c == '\\' || c == '/') {
      decoded = c;
    } else if (c == 'b') {
      decoded = '\b';
    } else if (c == 'f') {
      decoded = '\f';
    } else if (c == 'n') {
      decoded = '\n';
    } else if (c == 'r') {
      decoded = '\r';
    } else if (c == 't') {
      decoded = '\t';
    } else if (c == 'u') {
      if (i + 4 >= src_len) return false;
      uint32_t code = 0;
      if (!parse_hex4(src + i + 1, &code)) return false;
      i += 4;
      if (code >= 0xD800U && code <= 0xDBFFU) {
        if (i + 6 >= src_len || src[i + 1] != '\\' || src[i + 2] != 'u') return false;
        uint32_t low = 0;
        if (!parse_hex4(src + i + 3, &low) || low < 0xDC00U || low > 0xDFFFU) return false;
        code = 0x10000U + ((code - 0xD800U) << 10) + (low - 0xDC00U);
        i += 6;
      } else if (code >= 0xDC00U && code <= 0xDFFFU) {
        return false;
      }
      if (!append_utf8_scalar(code, dst, dst_size, &dst_idx)) return false;
      continue;
    } else {
      return false;
    }
    if (dst_idx + 1 >= dst_size) return false;
    dst[dst_idx++] = decoded;
  }
  dst[dst_idx] = '\0';
  if (out_len) *out_len = dst_idx;
  return true;
}

static const char* json_string_end(const char* start) {
  if (!start) return NULL;
  for (const char* p = start; *p; p++) {
    if (*p != '"') continue;
    size_t slashes = 0;
    const char* q = p;
    while (q > start && q[-1] == '\\') {
      slashes++;
      q--;
    }
    if ((slashes & 1U) == 0) return p;
  }
  return NULL;
}

static bool json_escape_string(const char* src, char* dst, size_t dst_size) {
  if (!src || !dst || dst_size == 0) return false;
  size_t out = 0;
  static const char hex[] = "0123456789ABCDEF";
  for (size_t i = 0; src[i]; i++) {
    unsigned char c = (unsigned char)src[i];
    const char* two = NULL;
    switch (c) {
      case '"':
        two = "\\\"";
        break;
      case '\\':
        two = "\\\\";
        break;
      case '\b':
        two = "\\b";
        break;
      case '\f':
        two = "\\f";
        break;
      case '\n':
        two = "\\n";
        break;
      case '\r':
        two = "\\r";
        break;
      case '\t':
        two = "\\t";
        break;
      default:
        break;
    }
    if (two) {
      if (out + 2 >= dst_size) return false;
      dst[out++] = two[0];
      dst[out++] = two[1];
    } else if (c < 0x20U) {
      if (out + 6 >= dst_size) return false;
      dst[out++] = '\\';
      dst[out++] = 'u';
      dst[out++] = '0';
      dst[out++] = '0';
      dst[out++] = hex[c >> 4];
      dst[out++] = hex[c & 0x0F];
    } else {
      if (out + 1 >= dst_size) return false;
      dst[out++] = (char)c;
    }
  }
  dst[out] = '\0';
  return true;
}

bool vw_translate_parse_mobile_response(const char* raw, char* out, size_t out_size) {
  if (!raw || !out || out_size == 0) return false;
  const char* start = strstr(raw, "class=\"result-container\"");
  if (!start) start = strstr(raw, "class='result-container'");
  if (!start) return false;
  start = strchr(start, '>');
  if (!start) return false;
  start++;
  const char* end = strstr(start, "</div>");
  if (!end) return false;

  size_t len = (size_t)(end - start);
  char* temp = (char*)malloc(len + 1);
  if (!temp) return false;
  memcpy(temp, start, len);
  temp[len] = '\0';
  bool ok = vw_html_unescape(temp, out, out_size);
  free(temp);
  if (!ok) return false;
  size_t out_len = strlen(out);
  while (out_len > 0 && isspace((unsigned char)out[out_len - 1])) out[--out_len] = '\0';
  return out_len > 0;
}

static const char* skip_json_string(const char* quote) {
  if (!quote || *quote != '"') return NULL;
  const char* end = json_string_end(quote + 1);
  return end ? end + 1 : NULL;
}

static const char* skip_segment_array(const char* p) {
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  for (; p && *p; p++) {
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (*p == '\\') {
        escaped = true;
      } else if (*p == '"') {
        in_string = false;
      }
      continue;
    }
    if (*p == '"') {
      in_string = true;
    } else if (*p == '[') {
      depth++;
    } else if (*p == ']') {
      if (depth == 0) return p + 1;
      depth--;
    }
  }
  return NULL;
}

static const char* skip_json_value(const char* p) {
  while (*p && isspace((unsigned char)*p)) p++;
  if (*p == '"') {
    return skip_json_string(p);
  }
  if (*p == '[' || *p == '{') {
    char open = *p;
    char close = (*p == '[') ? ']' : '}';
    bool in_str = false;
    bool esc = false;
    int depth = 0;
    for (; p && *p; p++) {
      if (in_str) {
        if (esc) {
          esc = false;
        } else if (*p == '\\') {
          esc = true;
        } else if (*p == '"') {
          in_str = false;
        }
        continue;
      }
      if (*p == '"') {
        in_str = true;
      } else if (*p == open) {
        depth++;
      } else if (*p == close) {
        depth--;
        if (depth == 0) return p + 1;
      }
    }
    return NULL;
  }
  // primitive (null, true, false, number)
  while (*p && *p != ',' && *p != ']' && *p != '}' && !isspace((unsigned char)*p)) {
    p++;
  }
  return p;
}

static const char* get_json_array_element(const char* arr, size_t target_index) {
  while (*arr && isspace((unsigned char)*arr)) arr++;
  if (*arr != '[') return NULL;
  arr++;
  for (size_t i = 0;; i++) {
    while (*arr && isspace((unsigned char)*arr)) arr++;
    if (*arr == ']' || !*arr) return NULL;
    if (i == target_index) return arr;
    arr = skip_json_value(arr);
    if (!arr) return NULL;
    while (*arr && isspace((unsigned char)*arr)) arr++;
    if (*arr == ',') {
      arr++;
    } else if (*arr == ']') {
      return NULL;
    }
  }
}

static bool parse_segments_list(const char* p, char* out, size_t out_size) {
  while (*p && isspace((unsigned char)*p)) p++;
  if (*p != '[') return false;
  p++;
  size_t out_idx = 0;
  for (;;) {
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '[') break;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '"') return false;
    const char* str_start = p + 1;
    const char* str_end = json_string_end(str_start);
    if (!str_end) return false;
    char chunk[VW_TRANSLATE_MAX_TEXT_BYTES + 1];
    size_t chunk_len = 0;
    if (!json_unescape_string(str_start, (size_t)(str_end - str_start), chunk, sizeof(chunk), &chunk_len)) return false;
    if (out_idx + chunk_len >= out_size) return false;
    memcpy(out + out_idx, chunk, chunk_len);
    out_idx += chunk_len;
    out[out_idx] = '\0';

    const char* after = skip_segment_array(str_end + 1);
    if (!after) return false;
    p = after;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == ',') {
      p++;
      continue;
    }
    if (*p == ']') {
      p++;
      break;
    }
    break;
  }
  return out_idx > 0;
}

bool vw_translate_parse_gtx_response(const char* raw, char* out, size_t out_size) {
  if (!raw || !out || out_size == 0) return false;
  out[0] = '\0';
  const char* p = raw;
  while (*p && isspace((unsigned char)*p)) p++;
  if (*p != '[') return false;
  p++;
  while (*p && isspace((unsigned char)*p)) p++;
  return parse_segments_list(p, out, out_size);
}

bool vw_translate_parse_rpc_response(const char* raw, char* out, size_t out_size) {
  if (!raw || !out || out_size == 0) return false;
  out[0] = '\0';
  const char* marker = strstr(raw, "\"wrb.fr\"");
  if (!marker) return false;
  const char* p = strchr(marker, ',');
  if (!p) return false;
  p++;
  while (*p && isspace((unsigned char)*p)) p++;

  // The next field is normally the RPC id; skip it when present.
  if (*p == '"') {
    p = skip_json_string(p);
    if (!p) return false;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != ',') return false;
    p++;
  }
  while (*p && isspace((unsigned char)*p)) p++;
  if (*p != '"') return false;
  const char* payload_start = p + 1;
  const char* payload_end = json_string_end(payload_start);
  if (!payload_end) return false;
  size_t payload_len = (size_t)(payload_end - payload_start);
  char* unescaped = (char*)malloc(payload_len + 1);
  if (!unescaped) return false;
  bool decoded = json_unescape_string(payload_start, payload_len, unescaped, payload_len + 1, NULL);
  bool ok = false;
  if (decoded) {
    // 1. Canonical MkEWBc shape: result[1][0][0][5] contains the segment array
    const char* elem1 = get_json_array_element(unescaped, 1);
    if (elem1) {
      const char* elem1_0 = get_json_array_element(elem1, 0);
      if (elem1_0) {
        const char* elem1_0_0 = get_json_array_element(elem1_0, 0);
        if (elem1_0_0) {
          const char* elem1_0_0_5 = get_json_array_element(elem1_0_0, 5);
          if (elem1_0_0_5) {
            ok = parse_segments_list(elem1_0_0_5, out, out_size);
          }
        }
      }
    }
    // 2. Pattern fallback: find "null,null,null,null,null,["
    if (!ok) {
      const char* nulls = strstr(unescaped, "null,null,null,null,null,");
      if (nulls) {
        const char* segs = strchr(nulls, '[');
        if (segs) ok = parse_segments_list(segs, out, out_size);
      }
    }
    // 3. Fallback for GTX-style or direct segment array
    if (!ok) {
      ok = vw_translate_parse_gtx_response(unescaped, out, out_size);
    }
  }
  free(unescaped);
  return ok && out[0] != '\0';
}

static bool build_rpc_body(const char* text, const char* src_lang, const char* dst_lang, char* out, size_t out_size) {
  if (!text || !src_lang || !dst_lang || !out || out_size == 0) return false;
  char* escaped_text = NULL;
  char* inner = NULL;
  char* escaped_inner = NULL;
  char* rpc = NULL;
  char* encoded = NULL;
  char escaped_src[128];
  char escaped_dst[128];
  bool ok = false;
  escaped_text = (char*)malloc(VW_TRANSLATE_MAX_TEXT_BYTES * 6U + 1U);
  inner = (char*)malloc(VW_TRANSLATE_RPC_JSON_BYTES);
  escaped_inner = (char*)malloc(VW_TRANSLATE_RPC_JSON_BYTES * 2U);
  rpc = (char*)malloc(VW_TRANSLATE_RPC_JSON_BYTES * 2U + 128U);
  encoded = (char*)malloc(VW_TRANSLATE_RPC_BODY_BYTES - 16U);
  if (!escaped_text || !inner || !escaped_inner || !rpc || !encoded) goto done;
  if (!json_escape_string(text, escaped_text, VW_TRANSLATE_MAX_TEXT_BYTES * 6U + 1U) ||
      !json_escape_string(src_lang, escaped_src, sizeof(escaped_src)) ||
      !json_escape_string(dst_lang, escaped_dst, sizeof(escaped_dst))) {
    goto done;
  }

  int inner_len = snprintf(inner, VW_TRANSLATE_RPC_JSON_BYTES, "[[\"%s\",\"%s\",\"%s\",true],[null]]", escaped_text,
                           escaped_src, escaped_dst);
  if (inner_len < 0 || (size_t)inner_len >= VW_TRANSLATE_RPC_JSON_BYTES) goto done;

  if (!json_escape_string(inner, escaped_inner, VW_TRANSLATE_RPC_JSON_BYTES * 2U)) goto done;
  int rpc_len =
      snprintf(rpc, VW_TRANSLATE_RPC_JSON_BYTES * 2U + 128U, "[[[\"MkEWBc\",\"%s\",null,\"generic\"]]]", escaped_inner);
  if (rpc_len < 0 || (size_t)rpc_len >= VW_TRANSLATE_RPC_JSON_BYTES * 2U + 128U) goto done;

  if (!vw_url_encode(rpc, encoded, VW_TRANSLATE_RPC_BODY_BYTES - 16U)) goto done;
  int body_len = snprintf(out, out_size, "f.req=%s", encoded);
  ok = body_len >= 0 && (size_t)body_len < out_size;
done:
  free(encoded);
  free(rpc);
  free(escaped_inner);
  free(inner);
  free(escaped_text);
  return ok;
}

#ifdef VW_TRANSLATE_TESTING
static vw_translate_test_http_hook_t g_test_http_hook = NULL;
static void* g_test_http_user_data = NULL;
static vw_translate_test_http_diagnostic_hook_t g_test_http_diagnostic_hook = NULL;
static void* g_test_http_diagnostic_user_data = NULL;

void vw_translate_set_test_http_hook(vw_translate_test_http_hook_t hook, void* user_data) {
  g_test_http_hook = hook;
  g_test_http_user_data = user_data;
}

void vw_translate_set_test_http_diagnostic_hook(vw_translate_test_http_diagnostic_hook_t hook, void* user_data) {
  g_test_http_diagnostic_hook = hook;
  g_test_http_diagnostic_user_data = user_data;
}

bool vw_translate_build_rpc_body_for_test(const char* text, const char* src_lang, const char* dst_lang, char* out,
                                          size_t out_size) {
  return build_rpc_body(text, src_lang, dst_lang, out, out_size);
}
#endif

#ifdef _WIN32
static bool set_winhttp_remaining_timeouts(HINTERNET session, int64_t deadline_us, unsigned divisor) {
  uint32_t remaining = remaining_timeout_ms(deadline_us);
  if (remaining == 0) return false;
  uint32_t phase = remaining / (divisor ? divisor : 1U);
  if (phase == 0) phase = 1;
  return WinHttpSetTimeouts(session, (int)phase, (int)phase, (int)phase, (int)phase) != FALSE;
}

static bool win32_http_request(const char* host, const char* path, const char* body, const char* content_type,
                               char* out_buf, size_t buf_size, int64_t deadline_us) {
  if (!host || !path || !out_buf || buf_size < 2) {
    g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
    return false;
  }
  if (remaining_timeout_ms(deadline_us) == 0) {
    g_http_failure_cause = VW_TRANSLATE_FAILURE_DEADLINE;
    return false;
  }
  bool ok = false;
  HINTERNET session = WinHttpOpen(L"VLC-Whisper/0.1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  HINTERNET connect = NULL;
  HINTERNET request = NULL;
  WCHAR* whost = NULL;
  WCHAR* wpath = NULL;
  if (!session || !set_winhttp_remaining_timeouts(session, deadline_us, 4U)) goto done;

  int host_len = MultiByteToWideChar(CP_UTF8, 0, host, -1, NULL, 0);
  if (host_len <= 0) {
    g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
    goto done;
  }
  whost = (WCHAR*)malloc((size_t)host_len * sizeof(WCHAR));
  if (!whost || !MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, host_len)) {
    g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
    goto done;
  }
  connect = WinHttpConnect(session, whost, INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!connect || remaining_timeout_ms(deadline_us) == 0) goto done;

  int path_len = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
  if (path_len <= 0) {
    g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
    goto done;
  }
  wpath = (WCHAR*)malloc((size_t)path_len * sizeof(WCHAR));
  if (!wpath || !MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, path_len)) {
    g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
    goto done;
  }
  request = WinHttpOpenRequest(connect, body ? L"POST" : L"GET", wpath, NULL, WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!request) goto done;

  WCHAR wheaders[256] = {0};
  if (content_type) {
    int written = swprintf(wheaders, sizeof(wheaders) / sizeof(wheaders[0]), L"Content-Type: %hs\r\n", content_type);
    if (written <= 0 || (size_t)written >= sizeof(wheaders) / sizeof(wheaders[0])) {
      g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
      goto done;
    }
  }
  if (!set_winhttp_remaining_timeouts(session, deadline_us, 3U)) goto done;
  LPCWSTR headers = wheaders[0] ? wheaders : WINHTTP_NO_ADDITIONAL_HEADERS;
  DWORD headers_len = wheaders[0] ? (DWORD)wcslen(wheaders) : 0;
  LPVOID req_data = body ? (LPVOID)body : WINHTTP_NO_REQUEST_DATA;
  DWORD body_len = body ? (DWORD)strlen(body) : 0;
  if (!WinHttpSendRequest(request, headers, headers_len, req_data, body_len, body_len, 0)) goto done;
  if (!set_winhttp_remaining_timeouts(session, deadline_us, 2U) || !WinHttpReceiveResponse(request, NULL)) goto done;

  DWORD status_code = 0;
  DWORD status_size = sizeof(status_code);
  if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                           &status_code, &status_size, WINHTTP_NO_HEADER_INDEX)) {
    goto done;
  }
  if (status_code < 200 || status_code >= 300) {
    g_http_failure_cause = VW_TRANSLATE_FAILURE_PROVIDER;
    g_http_provider_status = status_code <= UINT16_MAX ? (uint16_t)status_code : 0;
    goto done;
  }

  size_t total_read = 0;
  for (;;) {
    if (remaining_timeout_ms(deadline_us) == 0 || !set_winhttp_remaining_timeouts(session, deadline_us, 1U)) goto done;
    if (total_read + 1 >= buf_size) {
      char extra = '\0';
      DWORD extra_read = 0;
      if (!WinHttpReadData(request, &extra, 1, &extra_read) || extra_read != 0) goto done;
      break;
    }
    DWORD bytes_read = 0;
    DWORD request_bytes = (DWORD)(buf_size - 1U - total_read);
    if (!WinHttpReadData(request, out_buf + total_read, request_bytes, &bytes_read)) goto done;
    if (bytes_read == 0) break;
    total_read += bytes_read;
  }
  out_buf[total_read] = '\0';
  ok = total_read > 0 && get_monotonic_us() <= deadline_us;

done:
  free(wpath);
  free(whost);
  if (request) WinHttpCloseHandle(request);
  if (connect) WinHttpCloseHandle(connect);
  if (session) WinHttpCloseHandle(session);
  if (!ok && g_http_failure_cause == VW_TRANSLATE_FAILURE_TRANSPORT && remaining_timeout_ms(deadline_us) == 0)
    g_http_failure_cause = VW_TRANSLATE_FAILURE_DEADLINE;
  return ok;
}
#else
static bool set_cloexec(int fd) {
  int flags = fcntl(fd, F_GETFD);
  return flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

static bool set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool posix_http_request(const char* base_url, const char* post_body, const char* query_text,
                               const char* content_type, char* out_buf, size_t buf_size, int64_t deadline_us) {
  if (!base_url || !out_buf || buf_size < 2) {
    g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
    return false;
  }
  uint32_t timeout_ms = remaining_timeout_ms(deadline_us);
  if (timeout_ms == 0) {
    g_http_failure_cause = VW_TRANSLATE_FAILURE_DEADLINE;
    return false;
  }

  struct sigaction sa_ign;
  struct sigaction sa_old;
  memset(&sa_ign, 0, sizeof(sa_ign));
  sa_ign.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sa_ign, &sa_old);

  int pipe_out[2] = {-1, -1};
  if (pipe(pipe_out) != 0) {
    g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
    sigaction(SIGPIPE, &sa_old, NULL);
    return false;
  }
  if (!set_cloexec(pipe_out[0]) || !set_cloexec(pipe_out[1])) {
    g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
    close(pipe_out[0]);
    close(pipe_out[1]);
    sigaction(SIGPIPE, &sa_old, NULL);
    return false;
  }

  const char* input_payload = post_body ? post_body : query_text;
  int pipe_in[2] = {-1, -1};
  if (input_payload) {
    if (pipe(pipe_in) != 0) {
      g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
      close(pipe_out[0]);
      close(pipe_out[1]);
      sigaction(SIGPIPE, &sa_old, NULL);
      return false;
    }
    if (!set_cloexec(pipe_in[0]) || !set_cloexec(pipe_in[1])) {
      g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
      close(pipe_in[0]);
      close(pipe_in[1]);
      close(pipe_out[0]);
      close(pipe_out[1]);
      sigaction(SIGPIPE, &sa_old, NULL);
      return false;
    }
  }

  char timeout_sec_str[16];
  snprintf(timeout_sec_str, sizeof(timeout_sec_str), "%.3f", (double)timeout_ms / 1000.0);
  const char* argv[26];
  int argc = 0;
  argv[argc++] = VW_CURL_EXECUTABLE;
  argv[argc++] = "--disable";
  argv[argc++] = "-f";
  argv[argc++] = "-sS";
  argv[argc++] = "--proto";
  argv[argc++] = "=https";
  argv[argc++] = "-m";
  argv[argc++] = timeout_sec_str;
  argv[argc++] = "-A";
  argv[argc++] = VW_TRANSLATE_USER_AGENT;
  char ct_header[256];
  if (content_type) {
    int written = snprintf(ct_header, sizeof(ct_header), "Content-Type: %s", content_type);
    if (written < 0 || (size_t)written >= sizeof(ct_header)) {
      g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
      close(pipe_out[0]);
      close(pipe_out[1]);
      if (input_payload) {
        close(pipe_in[0]);
        close(pipe_in[1]);
      }
      sigaction(SIGPIPE, &sa_old, NULL);
      return false;
    }
    argv[argc++] = "-H";
    argv[argc++] = ct_header;
  }
  if (post_body) {
    argv[argc++] = "-X";
    argv[argc++] = "POST";
    argv[argc++] = "--data-binary";
    argv[argc++] = "@-";
  } else if (query_text) {
    argv[argc++] = "--get";
    argv[argc++] = "--data-urlencode";
    argv[argc++] = "q@-";
  }

  argv[argc++] = "-w";
  argv[argc++] = "\n%{response_code}";
  argv[argc++] = base_url;
  argv[argc] = NULL;

  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
    close(pipe_out[0]);
    close(pipe_out[1]);
    if (input_payload) {
      close(pipe_in[0]);
      close(pipe_in[1]);
    }
    sigaction(SIGPIPE, &sa_old, NULL);
    return false;
  }
  posix_spawn_file_actions_addclose(&actions, pipe_out[0]);
  posix_spawn_file_actions_adddup2(&actions, pipe_out[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&actions, pipe_out[1]);
  if (input_payload) {
    posix_spawn_file_actions_addclose(&actions, pipe_in[1]);
    posix_spawn_file_actions_adddup2(&actions, pipe_in[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipe_in[0]);
  }

  posix_spawnattr_t attr;
  bool attr_init = false;
  if (posix_spawnattr_init(&attr) == 0) {
    attr_init = true;
    sigset_t sigdef;
    sigemptyset(&sigdef);
    sigaddset(&sigdef, SIGPIPE);
    posix_spawnattr_setsigdefault(&attr, &sigdef);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF);
  }

  pid_t pid = 0;
  int spawn_status =
      posix_spawn(&pid, VW_CURL_EXECUTABLE, &actions, attr_init ? &attr : NULL, (char* const*)argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  if (attr_init) posix_spawnattr_destroy(&attr);

  close(pipe_out[1]);
  pipe_out[1] = -1;
  if (input_payload) {
    close(pipe_in[0]);
    pipe_in[0] = -1;
  }
  if (spawn_status != 0) {
    g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
    close(pipe_out[0]);
    pipe_out[0] = -1;
    if (input_payload) {
      close(pipe_in[1]);
      pipe_in[1] = -1;
    }
    sigaction(SIGPIPE, &sa_old, NULL);
    return false;
  }

  bool nonblocking_ok = set_nonblocking(pipe_out[0]);
  if (input_payload) nonblocking_ok = set_nonblocking(pipe_in[1]) && nonblocking_ok;
  if (!nonblocking_ok) {
    g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
    close(pipe_out[0]);
    pipe_out[0] = -1;
    if (input_payload) {
      close(pipe_in[1]);
      pipe_in[1] = -1;
    }
    kill(pid, SIGKILL);
    int reap_status = 0;
    pid_t reaped = 0;
    do {
      reaped = waitpid(pid, &reap_status, 0);
    } while (reaped < 0 && errno == EINTR);
    sigaction(SIGPIPE, &sa_old, NULL);
    return false;
  }

  size_t payload_len = input_payload ? strlen(input_payload) : 0;
  size_t written = 0;
  bool in_open = input_payload != NULL;
  bool out_open = true;
  bool write_failed = false;
  bool overflow = false;
  size_t total_read = 0;

  if (in_open && payload_len == 0) {
    close(pipe_in[1]);
    pipe_in[1] = -1;
    in_open = false;
  }

  while (in_open || out_open) {
    int64_t now_us = get_monotonic_us();
    if (now_us > deadline_us) break;
    int64_t rem_us = deadline_us - now_us;
    int rem_ms = (int)(rem_us / 1000LL);
    if (rem_ms <= 0) break;
    if (rem_ms > 200) rem_ms = 200;

    struct pollfd pfds[2];
    nfds_t nfds = 0;
    int in_idx = -1;
    int out_idx = -1;

    if (in_open) {
      pfds[nfds].fd = pipe_in[1];
      pfds[nfds].events = POLLOUT;
      pfds[nfds].revents = 0;
      in_idx = (int)nfds++;
    }
    if (out_open) {
      pfds[nfds].fd = pipe_out[0];
      pfds[nfds].events = POLLIN;
      pfds[nfds].revents = 0;
      out_idx = (int)nfds++;
    }

    int prc = poll(pfds, nfds, rem_ms);
    if (prc < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (prc == 0) continue;

    if (out_idx >= 0 && (pfds[out_idx].revents & (POLLIN | POLLHUP | POLLERR))) {
      while (out_open) {
        char extra = '\0';
        void* target = total_read + 1U < buf_size ? (void*)(out_buf + total_read) : (void*)&extra;
        size_t capacity = total_read + 1U < buf_size ? buf_size - 1U - total_read : 1U;
        ssize_t n = read(pipe_out[0], target, capacity);
        if (n > 0) {
          if (total_read + 1U >= buf_size) {
            overflow = true;
          } else {
            total_read += (size_t)n;
          }
        } else if (n == 0) {
          close(pipe_out[0]);
          pipe_out[0] = -1;
          out_open = false;
          break;
        } else {
          if (errno == EINTR) continue;
          if (errno == EAGAIN || errno == EWOULDBLOCK) break;
          close(pipe_out[0]);
          pipe_out[0] = -1;
          out_open = false;
          break;
        }
      }
    }

    if (in_open && in_idx >= 0) {
      if (pfds[in_idx].revents & (POLLERR | POLLHUP)) {
        close(pipe_in[1]);
        pipe_in[1] = -1;
        in_open = false;
        if (written < payload_len) write_failed = true;
      } else if (pfds[in_idx].revents & POLLOUT) {
        while (in_open && written < payload_len) {
          ssize_t n = write(pipe_in[1], input_payload + written, payload_len - written);
          if (n > 0) {
            written += (size_t)n;
            if (written >= payload_len) {
              close(pipe_in[1]);
              pipe_in[1] = -1;
              in_open = false;
              break;
            }
          } else if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            write_failed = true;
            close(pipe_in[1]);
            pipe_in[1] = -1;
            in_open = false;
            break;
          }
        }
      }
    }
  }

  if (pipe_in[1] >= 0) close(pipe_in[1]);
  if (pipe_out[0] >= 0) close(pipe_out[0]);
  out_buf[total_read] = '\0';
  sigaction(SIGPIPE, &sa_old, NULL);

  int wait_status = 0;
  pid_t waited = 0;
  while (1) {
    waited = waitpid(pid, &wait_status, WNOHANG);
    if (waited == pid) break;
    if (waited < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (get_monotonic_us() > deadline_us + 100000LL) {
      kill(pid, SIGKILL);
      do {
        waited = waitpid(pid, &wait_status, 0);
      } while (waited < 0 && errno == EINTR);
      break;
    }
    struct timespec ts = {0, 5000000L};
    nanosleep(&ts, NULL);
  }

  uint16_t response_status = 0;
  bool response_status_valid = false;
  if (total_read >= 4 && out_buf[total_read - 4] == '\n' && isdigit((unsigned char)out_buf[total_read - 3]) &&
      isdigit((unsigned char)out_buf[total_read - 2]) && isdigit((unsigned char)out_buf[total_read - 1])) {
    response_status = (uint16_t)((out_buf[total_read - 3] - '0') * 100 + (out_buf[total_read - 2] - '0') * 10 +
                                 (out_buf[total_read - 1] - '0'));
    total_read -= 4;
    out_buf[total_read] = '\0';
    response_status_valid = true;
    g_http_provider_status = response_status;
  }

  bool ok = !overflow && !write_failed && total_read > 0 && response_status_valid && response_status >= 200 &&
            response_status < 300 && waited == pid && WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 0 &&
            get_monotonic_us() <= deadline_us;
  if (!ok) {
    if (remaining_timeout_ms(deadline_us) == 0 ||
        (waited == pid && WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 28)) {
      g_http_failure_cause = VW_TRANSLATE_FAILURE_DEADLINE;
    } else if (response_status_valid && (response_status < 200 || response_status >= 300)) {
      g_http_failure_cause = VW_TRANSLATE_FAILURE_PROVIDER;
    } else if (waited == pid && WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 22) {
      g_http_failure_cause = VW_TRANSLATE_FAILURE_PROVIDER;
    } else {
      g_http_failure_cause = VW_TRANSLATE_FAILURE_TRANSPORT;
    }
  }
  return ok;
}
#endif

static bool http_request(const char* host, const char* path, const char* body, const char* query_text,
                         const char* content_type, char* out_buf, size_t buf_size, int64_t deadline_us) {
  vw_translate_http_failure_reset();
  uint32_t timeout_ms = remaining_timeout_ms(deadline_us);
  if (timeout_ms == 0) {
    g_http_failure_cause = VW_TRANSLATE_FAILURE_DEADLINE;
    return false;
  }
#ifdef VW_TRANSLATE_TESTING
  if (g_test_http_diagnostic_hook) {
    uint16_t status = 0;
    vw_translate_test_http_outcome_t outcome = g_test_http_diagnostic_hook(
        host, path, body, content_type, out_buf, buf_size, timeout_ms, &status, g_test_http_diagnostic_user_data);
    g_http_provider_status = status;
    if (outcome == VW_TRANSLATE_TEST_HTTP_OK) return true;
    if (outcome == VW_TRANSLATE_TEST_HTTP_PROVIDER) g_http_failure_cause = VW_TRANSLATE_FAILURE_PROVIDER;
    if (outcome == VW_TRANSLATE_TEST_HTTP_TRANSPORT) g_http_failure_cause = VW_TRANSLATE_FAILURE_TRANSPORT;
    if (outcome == VW_TRANSLATE_TEST_HTTP_DEADLINE) g_http_failure_cause = VW_TRANSLATE_FAILURE_DEADLINE;
    return false;
  }
  if (g_test_http_hook) {
    bool ok = g_test_http_hook(host, path, body, content_type, out_buf, buf_size, timeout_ms, g_test_http_user_data);
    if (!ok && remaining_timeout_ms(deadline_us) == 0) g_http_failure_cause = VW_TRANSLATE_FAILURE_DEADLINE;
    return ok;
  }
#endif
#ifdef _WIN32
  if (query_text) {
    char win_path[VW_TRANSLATE_MAX_URL_BYTES];
    char encoded_text[VW_TRANSLATE_MAX_TEXT_BYTES * 3U + 1U];
    if (!vw_url_encode(query_text, encoded_text, sizeof(encoded_text))) {
      g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
      return false;
    }
    const char* sep = strchr(path, '?') ? "&" : "?";
    int win_written = snprintf(win_path, sizeof(win_path), "%s%sq=%s", path, sep, encoded_text);
    if (win_written < 0 || (size_t)win_written >= sizeof(win_path)) {
      g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
      return false;
    }
    return win32_http_request(host, win_path, body, content_type, out_buf, buf_size, deadline_us);
  }
  return win32_http_request(host, path, body, content_type, out_buf, buf_size, deadline_us);
#else
  char full_url[VW_TRANSLATE_MAX_URL_BYTES];
  int written = snprintf(full_url, sizeof(full_url), "https://%s%s", host, path);
  if (written < 0 || (size_t)written >= sizeof(full_url)) {
    g_http_failure_cause = VW_TRANSLATE_FAILURE_LOCAL;
    return false;
  }
  return posix_http_request(full_url, body, query_text, content_type, out_buf, buf_size, deadline_us);
#endif
}

static bool is_valid_lang_tag(const char* lang, bool allow_auto) {
  if (!lang || !lang[0]) return false;
  if (allow_auto && strcmp(lang, "auto") == 0) return true;
  size_t len = strlen(lang);
  if (len < 2 || len > 15) return false;
  for (size_t i = 0; i < len; i++) {
    char c = lang[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return true;
}

static void vw_translate_set_failure(vw_translate_failure_t* failure, uint8_t cause, uint8_t tier, uint8_t mask,
                                     uint16_t status) {
  if (!failure) return;
  failure->cause = cause;
  failure->terminal_tier = tier;
  failure->attempted_tiers = mask;
  failure->provider_status = status;
}

bool vw_translate_text_detailed(const char* text, const char* src_lang, const char* dst_lang, char* out_text,
                                size_t out_size, uint8_t* out_tier, uint32_t* out_latency_us,
                                vw_translate_failure_t* out_failure) {
  vw_translate_failure_t failure = {0};
  if (out_failure) *out_failure = failure;
  if (!text || !out_text || out_size == 0) {
    vw_translate_set_failure(out_failure, VW_TRANSLATE_FAILURE_LOCAL, VW_TRANSLATE_TIER_NONE, 0, 0);
    return false;
  }
  out_text[0] = '\0';
  if (out_tier) *out_tier = VW_TRANSLATE_TIER_NONE;
  if (out_latency_us) *out_latency_us = 0;
  if (text[0] == '\0') return true;
  if (strlen(text) > VW_TRANSLATE_MAX_TEXT_BYTES) {
    vw_translate_set_failure(out_failure, VW_TRANSLATE_FAILURE_LOCAL, VW_TRANSLATE_TIER_NONE, 0, 0);
    return false;
  }

  const char* sl = (src_lang && is_valid_lang_tag(src_lang, true)) ? src_lang : "auto";
  const char* tl = (dst_lang && is_valid_lang_tag(dst_lang, false)) ? dst_lang : "en";
  int64_t started_us = get_monotonic_us();
  int64_t deadline_us = started_us + (int64_t)VW_TRANSLATE_TIMEOUT_MS * 1000LL;
  char* response = NULL;
  char* rpc_body = NULL;
  char* route_path = NULL;
  bool translated = false;
  uint8_t attempted = 0;

  char enc_sl[32];
  char enc_tl[32];
  if (!vw_url_encode(sl, enc_sl, sizeof(enc_sl)) || !vw_url_encode(tl, enc_tl, sizeof(enc_tl))) {
    failure.cause = VW_TRANSLATE_FAILURE_LOCAL;
    goto failed;
  }
  response = (char*)malloc(VW_TRANSLATE_MAX_RESPONSE_BYTES);
  rpc_body = (char*)malloc(VW_TRANSLATE_RPC_BODY_BYTES);
  route_path = (char*)malloc(VW_TRANSLATE_MAX_URL_BYTES);
  if (!response || !rpc_body || !route_path) {
    failure.cause = VW_TRANSLATE_FAILURE_LOCAL;
    goto failed;
  }

  attempted |= 0x01U;
  failure.terminal_tier = VW_TRANSLATE_TIER_WEB_RPC;
  if (!build_rpc_body(text, sl, tl, rpc_body, VW_TRANSLATE_RPC_BODY_BYTES)) {
    failure.cause = VW_TRANSLATE_FAILURE_LOCAL;
  } else {
    const char* rpc_path =
        "/_/TranslateWebserverUi/data/batchexecute?rpcids=MkEWBc&bl=boq_translate-webserver_20221005.09_p0&soc-app=1&"
        "soc-platform=1&soc-device=1&rt=c";
    bool http_ok = http_request("translate.google.com", rpc_path, rpc_body, NULL,
                                "application/x-www-form-urlencoded;charset=UTF-8", response,
                                VW_TRANSLATE_MAX_RESPONSE_BYTES, deadline_us);
    if (!http_ok) {
      failure.cause = g_http_failure_cause;
      failure.provider_status = g_http_provider_status;
    } else if (get_monotonic_us() > deadline_us) {
      failure.cause = VW_TRANSLATE_FAILURE_DEADLINE;
    } else if (!vw_translate_parse_rpc_response(response, out_text, out_size)) {
      failure.cause = VW_TRANSLATE_FAILURE_PARSE;
    } else if (get_monotonic_us() > deadline_us) {
      failure.cause = VW_TRANSLATE_FAILURE_DEADLINE;
    } else {
      if (out_tier) *out_tier = VW_TRANSLATE_TIER_WEB_RPC;
      translated = true;
      goto done;
    }
  }
  if (failure.cause == VW_TRANSLATE_FAILURE_DEADLINE || remaining_timeout_ms(deadline_us) == 0) {
    failure.cause = VW_TRANSLATE_FAILURE_DEADLINE;
    goto failed;
  }

  attempted |= 0x02U;
  failure.terminal_tier = VW_TRANSLATE_TIER_GTX;
  failure.provider_status = 0;
  int written = snprintf(route_path, VW_TRANSLATE_MAX_URL_BYTES, "/translate_a/single?client=gtx&sl=%s&tl=%s&dt=t",
                         enc_sl, enc_tl);
  if (written < 0 || (size_t)written >= VW_TRANSLATE_MAX_URL_BYTES) {
    failure.cause = VW_TRANSLATE_FAILURE_LOCAL;
  } else {
    bool http_ok = http_request("translate.googleapis.com", route_path, NULL, text, NULL, response,
                                VW_TRANSLATE_MAX_RESPONSE_BYTES, deadline_us);
    if (!http_ok) {
      failure.cause = g_http_failure_cause;
      failure.provider_status = g_http_provider_status;
    } else if (get_monotonic_us() > deadline_us) {
      failure.cause = VW_TRANSLATE_FAILURE_DEADLINE;
    } else if (!vw_translate_parse_gtx_response(response, out_text, out_size)) {
      failure.cause = VW_TRANSLATE_FAILURE_PARSE;
    } else if (get_monotonic_us() > deadline_us) {
      failure.cause = VW_TRANSLATE_FAILURE_DEADLINE;
    } else {
      if (out_tier) *out_tier = VW_TRANSLATE_TIER_GTX;
      translated = true;
      goto done;
    }
  }
  if (failure.cause == VW_TRANSLATE_FAILURE_DEADLINE || remaining_timeout_ms(deadline_us) == 0) {
    failure.cause = VW_TRANSLATE_FAILURE_DEADLINE;
    goto failed;
  }

  attempted |= 0x04U;
  failure.terminal_tier = VW_TRANSLATE_TIER_MOBILE_SCRAPE;
  failure.provider_status = 0;
  written = snprintf(route_path, VW_TRANSLATE_MAX_URL_BYTES, "/m?sl=%s&tl=%s", enc_sl, enc_tl);
  if (written < 0 || (size_t)written >= VW_TRANSLATE_MAX_URL_BYTES) {
    failure.cause = VW_TRANSLATE_FAILURE_LOCAL;
  } else {
    bool http_ok = http_request("translate.google.com", route_path, NULL, text, NULL, response,
                                VW_TRANSLATE_MAX_RESPONSE_BYTES, deadline_us);
    if (!http_ok) {
      failure.cause = g_http_failure_cause;
      failure.provider_status = g_http_provider_status;
    } else if (get_monotonic_us() > deadline_us) {
      failure.cause = VW_TRANSLATE_FAILURE_DEADLINE;
    } else if (!vw_translate_parse_mobile_response(response, out_text, out_size)) {
      failure.cause = VW_TRANSLATE_FAILURE_PARSE;
    } else if (get_monotonic_us() > deadline_us) {
      failure.cause = VW_TRANSLATE_FAILURE_DEADLINE;
    } else {
      if (out_tier) *out_tier = VW_TRANSLATE_TIER_MOBILE_SCRAPE;
      translated = true;
      goto done;
    }
  }

failed:
  failure.attempted_tiers = attempted;
done:
  free(route_path);
  free(rpc_body);
  free(response);
  if (!translated) {
    out_text[0] = '\0';
    if (out_tier) *out_tier = VW_TRANSLATE_TIER_NONE;
  } else {
    memset(&failure, 0, sizeof(failure));
  }
  if (out_latency_us) *out_latency_us = elapsed_to_u32(started_us);
  if (out_failure) *out_failure = failure;
  return translated;
}

bool vw_translate_text(const char* text, const char* src_lang, const char* dst_lang, char* out_text, size_t out_size,
                       uint8_t* out_tier, uint32_t* out_latency_us) {
  return vw_translate_text_detailed(text, src_lang, dst_lang, out_text, out_size, out_tier, out_latency_us, NULL);
}
