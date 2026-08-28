// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include "vw_translate.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#define VW_TRANSLATE_USER_AGENT \
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"

static int64_t get_monotonic_us(void) {
#ifdef _WIN32
  static LARGE_INTEGER freq;
  static int init = 0;
  if (!init) {
    QueryPerformanceFrequency(&freq);
    init = 1;
  }
  LARGE_INTEGER counter;
  QueryPerformanceCounter(&counter);
  return (int64_t)((counter.QuadPart * 1000000LL) / freq.QuadPart);
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
#endif
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
      snprintf(&dst[dst_idx], dst_size - dst_idx, "%%%02X", (unsigned int)c);
      dst_idx += 3;
    }
  }
  dst[dst_idx] = '\0';
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
      if (strncmp(&src[i], "&quot;", 6) == 0) {
        if (dst_idx + 1 >= dst_size) return false;
        dst[dst_idx++] = '"';
        i += 5;
        continue;
      }
      if (strncmp(&src[i], "&#39;", 5) == 0 || strncmp(&src[i], "&#039;", 6) == 0) {
        if (dst_idx + 1 >= dst_size) return false;
        dst[dst_idx++] = '\'';
        i += (src[i + 2] == '0' ? 5 : 4);
        continue;
      }
      if (strncmp(&src[i], "&amp;", 5) == 0) {
        if (dst_idx + 1 >= dst_size) return false;
        dst[dst_idx++] = '&';
        i += 4;
        continue;
      }
      if (strncmp(&src[i], "&lt;", 4) == 0) {
        if (dst_idx + 1 >= dst_size) return false;
        dst[dst_idx++] = '<';
        i += 3;
        continue;
      }
      if (strncmp(&src[i], "&gt;", 4) == 0) {
        if (dst_idx + 1 >= dst_size) return false;
        dst[dst_idx++] = '>';
        i += 3;
        continue;
      }
      if (strncmp(&src[i], "&nbsp;", 6) == 0) {
        if (dst_idx + 1 >= dst_size) return false;
        dst[dst_idx++] = ' ';
        i += 5;
        continue;
      }
      if (src[i + 1] == '#') {
        size_t end_idx = i + 2;
        while (src[end_idx] && src[end_idx] != ';' && end_idx - i < 10) {
          end_idx++;
        }
        if (src[end_idx] == ';') {
          long code = 0;
          if (src[i + 2] == 'x' || src[i + 2] == 'X') {
            code = strtol(&src[i + 3], NULL, 16);
          } else {
            code = strtol(&src[i + 2], NULL, 10);
          }
          if (code > 0 && code < 128) {
            if (dst_idx + 1 >= dst_size) return false;
            dst[dst_idx++] = (char)code;
            i = end_idx;
            continue;
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

static size_t json_unescape_string(const char* src, size_t src_len, char* dst, size_t dst_size) {
  size_t dst_idx = 0;
  for (size_t i = 0; i < src_len && src[i] != '\0'; i++) {
    if (src[i] == '\\' && i + 1 < src_len) {
      i++;
      char c = src[i];
      if (c == '"' || c == '\\' || c == '/') {
        if (dst_idx + 1 >= dst_size) break;
        dst[dst_idx++] = c;
      } else if (c == 'n') {
        if (dst_idx + 1 >= dst_size) break;
        dst[dst_idx++] = '\n';
      } else if (c == 'r') {
        if (dst_idx + 1 >= dst_size) break;
        dst[dst_idx++] = '\r';
      } else if (c == 't') {
        if (dst_idx + 1 >= dst_size) break;
        dst[dst_idx++] = '\t';
      } else if (c == 'u' && i + 4 < src_len) {
        char hex[5] = {src[i + 1], src[i + 2], src[i + 3], src[i + 4], '\0'};
        long val = strtol(hex, NULL, 16);
        i += 4;
        if (val > 0 && val <= 0x7F) {
          if (dst_idx + 1 >= dst_size) break;
          dst[dst_idx++] = (char)val;
        } else if (val >= 0x80 && val <= 0x7FF) {
          if (dst_idx + 2 >= dst_size) break;
          dst[dst_idx++] = (char)(0xC0 | ((val >> 6) & 0x1F));
          dst[dst_idx++] = (char)(0x80 | (val & 0x3F));
        } else if (val >= 0x800 && val <= 0xFFFF) {
          if (dst_idx + 3 >= dst_size) break;
          dst[dst_idx++] = (char)(0xE0 | ((val >> 12) & 0x0F));
          dst[dst_idx++] = (char)(0x80 | ((val >> 6) & 0x3F));
          dst[dst_idx++] = (char)(0x80 | (val & 0x3F));
        }
      } else {
        if (dst_idx + 1 >= dst_size) break;
        dst[dst_idx++] = c;
      }
    } else {
      if (dst_idx + 1 >= dst_size) break;
      dst[dst_idx++] = src[i];
    }
  }
  if (dst_idx < dst_size) {
    dst[dst_idx] = '\0';
  } else if (dst_size > 0) {
    dst[dst_size - 1] = '\0';
  }
  return dst_idx;
}

bool vw_translate_parse_mobile_response(const char* raw, char* out, size_t out_size) {
  if (!raw || !out || out_size == 0) return false;

  const char* start = strstr(raw, "class=\"result-container\"");
  if (!start) {
    start = strstr(raw, "class='result-container'");
  }
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

  // Trim trailing whitespace
  if (ok) {
    size_t out_len = strlen(out);
    while (out_len > 0 && isspace((unsigned char)out[out_len - 1])) {
      out[--out_len] = '\0';
    }
  }
  return ok && out[0] != '\0';
}

bool vw_translate_parse_gtx_response(const char* raw, char* out, size_t out_size) {
  if (!raw || !out || out_size == 0) return false;

  // Expected start: [[["chunk 1","source 1",...],["chunk 2",...],...],...]
  const char* p = raw;
  while (*p && (*p == '[' || isspace((unsigned char)*p))) p++;
  if (*p != '"') return false;

  size_t out_idx = 0;
  out[0] = '\0';

  while (*p) {
    if (*p == '"') {
      p++;
      const char* str_start = p;
      while (*p && (*p != '"' || *(p - 1) == '\\')) p++;
      size_t str_len = (size_t)(p - str_start);
      if (*p == '"') p++;

      char unescaped[VW_TRANSLATE_MAX_TEXT_BYTES];
      size_t unesc_len = json_unescape_string(str_start, str_len, unescaped, sizeof(unescaped));

      if (out_idx + unesc_len < out_size) {
        memcpy(&out[out_idx], unescaped, unesc_len);
        out_idx += unesc_len;
        out[out_idx] = '\0';
      }

      // Skip the rest of this segment tuple until next subarray or end
      int bracket_depth = 0;
      while (*p) {
        if (*p == '[') bracket_depth++;
        if (*p == ']') {
          if (bracket_depth == 0) {
            p++;
            break;
          }
          bracket_depth--;
        }
        p++;
      }

      while (*p && isspace((unsigned char)*p)) p++;
      if (*p == ',') p++;
      while (*p && isspace((unsigned char)*p)) p++;
      if (*p == '[') {
        p++;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p != '"') break;
      } else {
        break;
      }
    } else {
      p++;
    }
  }

  return out_idx > 0;
}

bool vw_translate_parse_rpc_response(const char* raw, char* out, size_t out_size) {
  if (!raw || !out || out_size == 0) return false;

  const char* marker = strstr(raw, "\"wrb.fr\"");
  if (!marker) return false;

  // Locate the embedded JSON string following wrb.fr
  const char* p = strchr(marker, ',');
  if (!p) return false;
  p++;
  while (*p && (*p == ' ' || *p == '\t')) p++;

  // Next field might be RPC id or null, look for quoted result payload
  if (*p == '"') {
    // Skip RPC ID string
    p++;
    while (*p && (*p != '"' || *(p - 1) == '\\')) p++;
    if (*p == '"') p++;
    p = strchr(p, ',');
    if (!p) return false;
    p++;
  }

  while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
  if (*p != '"') return false;
  p++;

  const char* payload_start = p;
  while (*p && (*p != '"' || *(p - 1) == '\\')) p++;
  size_t payload_len = (size_t)(p - payload_start);

  char* unescaped = (char*)malloc(payload_len + 1);
  if (!unescaped) return false;

  json_unescape_string(payload_start, payload_len, unescaped, payload_len + 1);

  // Now parse the unescaped inner JSON structure
  // Typically contains [[null, null, ...], ...] or nested arrays with translated chunks
  bool ok = vw_translate_parse_gtx_response(unescaped, out, out_size);
  if (!ok) {
    // Search for translation segments inside the payload
    const char* seg = strstr(unescaped, "[[[");
    if (seg) {
      ok = vw_translate_parse_gtx_response(seg, out, out_size);
    }
  }

  free(unescaped);
  return ok && out[0] != '\0';
}

#ifdef _WIN32

static bool win32_http_request(const char* host, const char* path, const char* body, const char* content_type,
                               char* out_buf, size_t buf_size, uint32_t timeout_ms) {
  if (!host || !path || !out_buf || buf_size == 0) return false;

  HINTERNET hSession = WinHttpOpen(L"VLC-Whisper/0.3.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) return false;

  WinHttpSetTimeouts(hSession, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

  int host_len = MultiByteToWideChar(CP_UTF8, 0, host, -1, NULL, 0);
  WCHAR* whost = (WCHAR*)malloc(host_len * sizeof(WCHAR));
  if (!whost) {
    WinHttpCloseHandle(hSession);
    return false;
  }
  MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, host_len);

  HINTERNET hConnect = WinHttpConnect(hSession, whost, INTERNET_DEFAULT_HTTPS_PORT, 0);
  free(whost);
  if (!hConnect) {
    WinHttpCloseHandle(hSession);
    return false;
  }

  int path_len = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
  WCHAR* wpath = (WCHAR*)malloc(path_len * sizeof(WCHAR));
  if (!wpath) {
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return false;
  }
  MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, path_len);

  HINTERNET hRequest = WinHttpOpenRequest(hConnect, body ? L"POST" : L"GET", wpath, NULL, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  free(wpath);
  if (!hRequest) {
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return false;
  }

  WCHAR wheaders[256] = {0};
  if (content_type) {
    int ct_len = MultiByteToWideChar(CP_UTF8, 0, content_type, -1, NULL, 0);
    if (ct_len > 0 && ct_len < 200) {
      swprintf(wheaders, sizeof(wheaders) / sizeof(wheaders[0]), L"Content-Type: %hs\r\n", content_type);
    }
  }

  DWORD body_len = body ? (DWORD)strlen(body) : 0;
  BOOL sent = WinHttpSendRequest(hRequest, wheaders[0] ? wheaders : WINHTTP_NO_ADDITIONAL_HEADERS, (DWORD)-1L,
                                 (LPVOID)body, body_len, body_len, 0);

  if (!sent || !WinHttpReceiveResponse(hRequest, NULL)) {
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return false;
  }

  size_t total_read = 0;
  DWORD bytes_read = 0;
  while (total_read + 1 < buf_size) {
    if (!WinHttpReadData(hRequest, &out_buf[total_read], (DWORD)(buf_size - 1 - total_read), &bytes_read) ||
        bytes_read == 0) {
      break;
    }
    total_read += bytes_read;
  }
  out_buf[total_read] = '\0';

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return total_read > 0;
}

#else

static bool posix_http_request(const char* full_url, const char* body, const char* content_type, char* out_buf,
                               size_t buf_size, uint32_t timeout_ms) {
  if (!full_url || !out_buf || buf_size == 0) return false;

  int pipe_out[2];
  if (pipe(pipe_out) != 0) return false;

  char timeout_sec_str[16];
  snprintf(timeout_sec_str, sizeof(timeout_sec_str), "%.2f", (double)timeout_ms / 1000.0);

  const char* argv[16];
  int argc = 0;
  argv[argc++] = "curl";
  argv[argc++] = "-s";
  argv[argc++] = "-m";
  argv[argc++] = timeout_sec_str;
  argv[argc++] = "-A";
  argv[argc++] = VW_TRANSLATE_USER_AGENT;

  char ct_header[256];
  if (content_type) {
    snprintf(ct_header, sizeof(ct_header), "Content-Type: %s", content_type);
    argv[argc++] = "-H";
    argv[argc++] = ct_header;
  }

  if (body) {
    argv[argc++] = "-X";
    argv[argc++] = "POST";
    argv[argc++] = "--data";
    argv[argc++] = body;
  }

  argv[argc++] = full_url;
  argv[argc] = NULL;

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addclose(&actions, pipe_out[0]);
  posix_spawn_file_actions_adddup2(&actions, pipe_out[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&actions, pipe_out[1]);

  pid_t pid;
  int status = posix_spawnp(&pid, "curl", &actions, NULL, (char* const*)argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  close(pipe_out[1]);

  if (status != 0) {
    close(pipe_out[0]);
    return false;
  }

  size_t total_read = 0;
  ssize_t bytes_read = 0;
  while (total_read + 1 < buf_size &&
         (bytes_read = read(pipe_out[0], &out_buf[total_read], buf_size - 1 - total_read)) > 0) {
    total_read += (size_t)bytes_read;
  }
  out_buf[total_read] = '\0';
  close(pipe_out[0]);

  int wait_status;
  waitpid(pid, &wait_status, 0);

  return total_read > 0 && WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 0;
}

#endif

static bool http_request(const char* host, const char* path, const char* body, const char* content_type, char* out_buf,
                         size_t buf_size, uint32_t timeout_ms) {
#ifdef _WIN32
  return win32_http_request(host, path, body, content_type, out_buf, buf_size, timeout_ms);
#else
  char full_url[VW_TRANSLATE_MAX_URL_BYTES];
  snprintf(full_url, sizeof(full_url), "https://%s%s", host, path);
  return posix_http_request(full_url, body, content_type, out_buf, buf_size, timeout_ms);
#endif
}

bool vw_translate_text(const char* text, const char* src_lang, const char* dst_lang, char* out_text, size_t out_size,
                       uint8_t* out_tier, uint32_t* out_latency_us) {
  if (!text || !out_text || out_size == 0) return false;
  if (text[0] == '\0') {
    out_text[0] = '\0';
    if (out_tier) *out_tier = VW_TRANSLATE_TIER_NONE;
    if (out_latency_us) *out_latency_us = 0;
    return true;
  }

  const char* sl = (src_lang && src_lang[0] != '\0') ? src_lang : "auto";
  const char* tl = (dst_lang && dst_lang[0] != '\0') ? dst_lang : "en";

  int64_t t_start = get_monotonic_us();

  char encoded_text[VW_TRANSLATE_MAX_TEXT_BYTES * 3];
  if (!vw_url_encode(text, encoded_text, sizeof(encoded_text))) return false;

  char response[VW_TRANSLATE_MAX_RESPONSE_BYTES];

  // -------------------------------------------------------------------------
  // Tier 1: Web RPC MkEWBc batchexecute
  // -------------------------------------------------------------------------
  char inner_json[VW_TRANSLATE_MAX_TEXT_BYTES * 2];
  snprintf(inner_json, sizeof(inner_json), "[[\\\"%s\\\",\\\"%s\\\",\\\"%s\\\",true],[null]]", text, sl, tl);

  char rpc_req[VW_TRANSLATE_MAX_TEXT_BYTES * 3];
  snprintf(rpc_req, sizeof(rpc_req), "[[[\"MkEWBc\",\"%s\",null,\"generic\"]]]", inner_json);

  char encoded_req[VW_TRANSLATE_MAX_TEXT_BYTES * 4];
  if (vw_url_encode(rpc_req, encoded_req, sizeof(encoded_req))) {
    char body[VW_TRANSLATE_MAX_TEXT_BYTES * 4 + 16];
    snprintf(body, sizeof(body), "f.req=%s", encoded_req);

    const char* rpc_path =
        "/_/TranslateWebserverUi/data/batchexecute?rpcids=MkEWBc&bl=boq_translate-webserver_20221005.09_p0&soc-app=1&"
        "soc-platform=1&soc-device=1&rt=c";

    if (http_request("translate.google.com", rpc_path, body, "application/x-www-form-urlencoded;charset=UTF-8",
                     response, sizeof(response), VW_TRANSLATE_TIMEOUT_MS)) {
      if (vw_translate_parse_rpc_response(response, out_text, out_size)) {
        int64_t elapsed = get_monotonic_us() - t_start;
        if (out_tier) *out_tier = (uint8_t)VW_TRANSLATE_TIER_WEB_RPC;
        if (out_latency_us) *out_latency_us = (uint32_t)(elapsed > 0 ? elapsed : 0);
        return true;
      }
    }
  }

  // -------------------------------------------------------------------------
  // Tier 2: Legacy GTX single-query endpoint
  // -------------------------------------------------------------------------
  char gtx_path[VW_TRANSLATE_MAX_URL_BYTES];
  snprintf(gtx_path, sizeof(gtx_path), "/translate_a/single?client=gtx&sl=%s&tl=%s&dt=t&q=%s", sl, tl, encoded_text);

  if (http_request("translate.googleapis.com", gtx_path, NULL, NULL, response, sizeof(response),
                   VW_TRANSLATE_TIMEOUT_MS)) {
    if (vw_translate_parse_gtx_response(response, out_text, out_size)) {
      int64_t elapsed = get_monotonic_us() - t_start;
      if (out_tier) *out_tier = (uint8_t)VW_TRANSLATE_TIER_GTX;
      if (out_latency_us) *out_latency_us = (uint32_t)(elapsed > 0 ? elapsed : 0);
      return true;
    }
  }

  // -------------------------------------------------------------------------
  // Tier 3: Mobile web scrape endpoint
  // -------------------------------------------------------------------------
  char mobile_path[VW_TRANSLATE_MAX_URL_BYTES];
  snprintf(mobile_path, sizeof(mobile_path), "/m?sl=%s&tl=%s&q=%s", sl, tl, encoded_text);

  if (http_request("translate.google.com", mobile_path, NULL, NULL, response, sizeof(response),
                   VW_TRANSLATE_TIMEOUT_MS)) {
    if (vw_translate_parse_mobile_response(response, out_text, out_size)) {
      int64_t elapsed = get_monotonic_us() - t_start;
      if (out_tier) *out_tier = (uint8_t)VW_TRANSLATE_TIER_MOBILE_SCRAPE;
      if (out_latency_us) *out_latency_us = (uint32_t)(elapsed > 0 ? elapsed : 0);
      return true;
    }
  }

  if (out_tier) *out_tier = (uint8_t)VW_TRANSLATE_TIER_NONE;
  if (out_latency_us) *out_latency_us = (uint32_t)(get_monotonic_us() - t_start);
  return false;
}
