#ifndef VW_WORKER_TRANSLATION_DIAG_OVERRIDE_H_
#define VW_WORKER_TRANSLATION_DIAG_OVERRIDE_H_

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vw_ipc_transport.h"
#include "vw_protocol_codec.h"
#include "vw_protocol_types.h"
#include "vw_translate.h"
#include "vw_translate_async.h"

typedef struct vw_worker_translation_delivery_view {
  vw_ipc_handle_t* handle;
  uint64_t* sequence;
  const vw_session_id_t* session_id;
  const bool* session_active;
  _Atomic bool* running;
  _Atomic bool* fatal_exit;
} vw_worker_translation_delivery_view_t;

static inline const char* vw_worker_translation_cause_name(uint8_t cause) {
  switch (cause) {
    case VW_TRANSLATE_FAILURE_PROVIDER:
      return "provider";
    case VW_TRANSLATE_FAILURE_TRANSPORT:
      return "transport";
    case VW_TRANSLATE_FAILURE_PARSE:
      return "parse";
    case VW_TRANSLATE_FAILURE_DEADLINE:
      return "deadline";
    case VW_TRANSLATE_FAILURE_LOCAL:
      return "local";
    default:
      return "unknown";
  }
}

static inline const char* vw_worker_translation_tier_name(uint8_t tier) {
  switch (tier) {
    case VW_TRANSLATE_TIER_WEB_RPC:
      return "rpc";
    case VW_TRANSLATE_TIER_GTX:
      return "gtx";
    case VW_TRANSLATE_TIER_MOBILE_SCRAPE:
      return "mobile";
    default:
      return "none";
  }
}

static inline vw_error_code_t vw_worker_translation_error_code(uint8_t cause) {
  switch (cause) {
    case VW_TRANSLATE_FAILURE_PROVIDER:
      return E_TRANSLATION_PROVIDER;
    case VW_TRANSLATE_FAILURE_TRANSPORT:
      return E_TRANSLATION_TRANSPORT;
    case VW_TRANSLATE_FAILURE_PARSE:
      return E_TRANSLATION_PARSE;
    case VW_TRANSLATE_FAILURE_DEADLINE:
      return E_TRANSLATION_DEADLINE;
    case VW_TRANSLATE_FAILURE_LOCAL:
    default:
      return E_TRANSLATION_LOCAL;
  }
}

// Builds the bounded worker-to-plugin blame line. It intentionally contains no source/translated subtitle body,
// credentials, PCM, URL, or response body.
static inline bool vw_worker_translation_diag_build(const vw_translate_async_result_t* result,
                                                    vw_error_code_t* out_code, char* out, size_t out_size) {
  if (!result || !out_code || !out || out_size == 0 || result->success || !result->attempted ||
      result->failure.cause == VW_TRANSLATE_FAILURE_NONE) {
    return false;
  }

  *out_code = vw_worker_translation_error_code(result->failure.cause);
  uint32_t latency_us = result->segment.translation_latency_us;
  uint32_t whole_ms = latency_us / 1000U;
  uint32_t fractional_us = latency_us % 1000U;
  int written = 0;
  if (result->failure.provider_status != 0) {
    written = snprintf(out, out_size,
                       "segment=%llu cause=%s tier=%s attempts=0x%02x status=%u latency_ms=%u.%03u",
                       (unsigned long long)result->segment.segment_id,
                       vw_worker_translation_cause_name(result->failure.cause),
                       vw_worker_translation_tier_name(result->failure.terminal_tier),
                       (unsigned int)result->failure.attempted_tiers, (unsigned int)result->failure.provider_status,
                       (unsigned int)whole_ms, (unsigned int)fractional_us);
  } else {
    written = snprintf(out, out_size, "segment=%llu cause=%s tier=%s attempts=0x%02x latency_ms=%u.%03u",
                       (unsigned long long)result->segment.segment_id,
                       vw_worker_translation_cause_name(result->failure.cause),
                       vw_worker_translation_tier_name(result->failure.terminal_tier),
                       (unsigned int)result->failure.attempted_tiers, (unsigned int)whole_ms,
                       (unsigned int)fractional_us);
  }
  return written >= 0 && (size_t)written < out_size;
}

static inline bool vw_worker_translation_diag_send(vw_worker_translation_delivery_view_t* delivery,
                                                   vw_error_code_t code, const char* detail) {
  if (!delivery || !delivery->handle || !delivery->sequence || !delivery->session_id || !detail) return false;
  vw_msg_error_t error;
  memset(&error, 0, sizeof(error));
  memcpy(error.session_id.bytes, delivery->session_id->bytes, VW_SESSION_ID_BYTES);
  error.error_code = code;
  error.recoverable = 1U;
  snprintf(error.message, sizeof(error.message), "%s", detail);

  uint8_t payload[512];
  size_t payload_len = 0;
  if (!vw_protocol_encode_payload(VW_MSG_ERROR, &error, payload, sizeof(payload), &payload_len)) return false;
  vw_frame_header_t header = {.magic = VW_PROTOCOL_MAGIC,
                              .major = VW_PROTOCOL_VERSION_MAJOR,
                              .type = VW_MSG_ERROR,
                              .payload_length = (uint32_t)payload_len,
                              .sequence = ++(*delivery->sequence)};
  uint8_t header_buf[sizeof(vw_frame_header_t)];
  if (!vw_protocol_encode_header(&header, header_buf, sizeof(header_buf))) return false;
  return vw_ipc_send(delivery->handle, header_buf, sizeof(header_buf)) &&
         vw_ipc_send(delivery->handle, payload, payload_len);
}

typedef struct vw_worker_translation_diag_context {
  vw_translate_async_delivery_fn deliver;
  void* user_data;
} vw_worker_translation_diag_context_t;

static inline void vw_worker_translation_diag_deliver(const vw_translate_async_result_t* result, void* opaque) {
  vw_worker_translation_diag_context_t* context = (vw_worker_translation_diag_context_t*)opaque;
  if (!context || !context->deliver) return;
  vw_worker_translation_delivery_view_t* delivery = (vw_worker_translation_delivery_view_t*)context->user_data;

  vw_error_code_t code = E_TRANSLATION_LOCAL;
  char detail[VW_MAX_ERROR_MSG_BYTES];
  if (vw_worker_translation_diag_build(result, &code, detail, sizeof(detail)) &&
      !vw_worker_translation_diag_send(delivery, code, detail)) {
    if (delivery && delivery->fatal_exit) atomic_store(delivery->fatal_exit, true);
    if (delivery && delivery->running) atomic_store(delivery->running, false);
    return;
  }
  context->deliver(result, context->user_data);
}

static inline bool vw_worker_translate_async_try_deliver_scoped(vw_translate_async_t* async,
                                                                vw_translate_async_delivery_fn deliver,
                                                                void* user_data) {
  vw_worker_translation_diag_context_t context = {.deliver = deliver, .user_data = user_data};
  return vw_translate_async_try_deliver(async, vw_worker_translation_diag_deliver, &context);
}

#ifdef VW_WORKER_TRANSLATION_DIAG_OVERRIDE
#define vw_translate_async_try_deliver(async, deliver, user_data) \
  vw_worker_translate_async_try_deliver_scoped((async), (deliver), (user_data))
#endif

#endif  // VW_WORKER_TRANSLATION_DIAG_OVERRIDE_H_
