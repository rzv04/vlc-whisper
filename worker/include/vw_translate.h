// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

#ifndef VW_TRANSLATE_H_
#define VW_TRANSLATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VW_TRANSLATE_MAX_TEXT_BYTES 1024U
#define VW_TRANSLATE_MAX_URL_BYTES 4096U
#define VW_TRANSLATE_MAX_RESPONSE_BYTES 65536U
#define VW_TRANSLATE_TIMEOUT_MS 800U

typedef enum vw_translate_tier {
  VW_TRANSLATE_TIER_NONE = 0,
  VW_TRANSLATE_TIER_WEB_RPC = 1,
  VW_TRANSLATE_TIER_GTX = 2,
  VW_TRANSLATE_TIER_MOBILE_SCRAPE = 3
} vw_translate_tier_t;

typedef enum vw_translate_failure_cause {
  VW_TRANSLATE_FAILURE_NONE = 0,
  VW_TRANSLATE_FAILURE_PROVIDER = 1,
  VW_TRANSLATE_FAILURE_TRANSPORT = 2,
  VW_TRANSLATE_FAILURE_PARSE = 3,
  VW_TRANSLATE_FAILURE_DEADLINE = 4,
  VW_TRANSLATE_FAILURE_LOCAL = 5
} vw_translate_failure_cause_t;

typedef struct vw_translate_failure {
  uint8_t cause;
  uint8_t terminal_tier;
  uint8_t attempted_tiers;
  uint16_t provider_status;
} vw_translate_failure_t;

// Percent-encodes a UTF-8 source string into RFC 3986 format for secure HTTP URL queries and POST request payloads.
bool vw_url_encode(const char* src, char* dst, size_t dst_size);

// Unescapes common/numeric HTML entities and strips XML/HTML tags from mobile scrape responses safely.
bool vw_html_unescape(const char* src, char* dst, size_t dst_size);

// Parses Google Web RPC batchexecute response envelopes to extract translated text segments from internal MkEWBc
// structures.
bool vw_translate_parse_rpc_response(const char* raw, char* out, size_t out_size);

// Parses legacy Google GTX single-query JSON responses to concatenate translated text chunks from nested arrays safely.
bool vw_translate_parse_gtx_response(const char* raw, char* out, size_t out_size);

// Parses lightweight Google mobile web HTML responses to extract and unescape text within result-container elements.
bool vw_translate_parse_mobile_response(const char* raw, char* out, size_t out_size);

// Translates through the existing three fallback tiers while returning explicit terminal failure metadata, total
// latency, and successful tier without changing fallback ordering or deadline behavior.
bool vw_translate_text_detailed(const char* text, const char* src_lang, const char* dst_lang, char* out_text,
                                size_t out_size, uint8_t* out_tier, uint32_t* out_latency_us,
                                vw_translate_failure_t* out_failure);

// Preserves the legacy translation API for callers that do not need failure diagnostics.
bool vw_translate_text(const char* text, const char* src_lang, const char* dst_lang, char* out_text, size_t out_size,
                       uint8_t* out_tier, uint32_t* out_latency_us);

#ifdef VW_TRANSLATE_TESTING
// Deterministic transport injection used by legacy translation tests. The timeout argument is the remaining portion of
// the single global cue budget; production builds never expose or call this hook.
typedef bool (*vw_translate_test_http_hook_t)(const char* host, const char* path, const char* body,
                                              const char* content_type, char* out_buf, size_t buf_size,
                                              uint32_t timeout_ms, void* user_data);
void vw_translate_set_test_http_hook(vw_translate_test_http_hook_t hook, void* user_data);

typedef enum vw_translate_test_http_outcome {
  VW_TRANSLATE_TEST_HTTP_OK = 0,
  VW_TRANSLATE_TEST_HTTP_PROVIDER = 1,
  VW_TRANSLATE_TEST_HTTP_TRANSPORT = 2,
  VW_TRANSLATE_TEST_HTTP_DEADLINE = 3
} vw_translate_test_http_outcome_t;

// Diagnostic transport injection additionally carries provider status and an explicit cause so failure ownership can be
// tested without network access.
typedef vw_translate_test_http_outcome_t (*vw_translate_test_http_diagnostic_hook_t)(
    const char* host, const char* path, const char* body, const char* content_type, char* out_buf, size_t buf_size,
    uint32_t timeout_ms, uint16_t* out_status, void* user_data);
void vw_translate_set_test_http_diagnostic_hook(vw_translate_test_http_diagnostic_hook_t hook, void* user_data);

// Builds the exact form body used by the Tier-1 MkEWBc request so request escaping can be contract-tested without
// contacting the service.
bool vw_translate_build_rpc_body_for_test(const char* text, const char* src_lang, const char* dst_lang, char* out,
                                          size_t out_size);
#endif

#endif  // VW_TRANSLATE_H_
