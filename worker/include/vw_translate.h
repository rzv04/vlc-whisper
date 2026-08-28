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

// Percent-encodes a UTF-8 source string into RFC 3986 format for secure HTTP URL queries and POST request payloads.
bool vw_url_encode(const char* src, char* dst, size_t dst_size);

// Unescapes HTML entities (&quot;, &#39;, &amp;, &lt;, &gt;) and strips XML tags from mobile scrape responses safely.
bool vw_html_unescape(const char* src, char* dst, size_t dst_size);

// Parses Google Web RPC batchexecute response envelopes to extract translated text segments from internal MkEWBc
// structures.
bool vw_translate_parse_rpc_response(const char* raw, char* out, size_t out_size);

// Parses legacy Google GTX single-query JSON responses to concatenate translated text chunks from nested arrays safely.
bool vw_translate_parse_gtx_response(const char* raw, char* out, size_t out_size);

// Parses lightweight Google mobile web HTML responses to extract and unescape text within result-container elements.
bool vw_translate_parse_mobile_response(const char* raw, char* out, size_t out_size);

// Translates a UTF-8 text string using a 3-tier fallback engine (Web RPC, GTX, and Mobile scrape) within a bounded
// timeout.
bool vw_translate_text(const char* text, const char* src_lang, const char* dst_lang, char* out_text, size_t out_size,
                       uint8_t* out_tier, uint32_t* out_latency_us);

#endif  // VW_TRANSLATE_H_
