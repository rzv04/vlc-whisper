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

// Keep the worker's transport-fatal state source-local instead of exposing its private delivery context through the
// public async translation API. The worker main loop is the only IPC writer, and the delivery adapter registers these
// pointers before captions can reach the translation submit path.
typedef struct vw_worker_translation_transport_state {
  vw_ipc_handle_t* handle;
  _Atomic bool* running;
  _Atomic bool* fatal_exit;
  _Atomic bool failed;
} vw_worker_translation_transport_state_t;

static vw_worker_translation_transport_state_t vw_worker_translation_transport_state = {0};

static inline void vw_worker_translation_diag_register_transport_state(
    const vw_worker_translation_delivery_view_t* delivery) {
  if (!delivery || !delivery->handle) return;
  bool same_state = vw_worker_translation_transport_state.handle == delivery->handle &&
                    vw_worker_translation_transport_state.running == delivery->running &&
                    vw_worker_translation_transport_state.fatal_exit == delivery->fatal_exit;
  if (!same_state) {
    vw_worker_translation_transport_state.handle = delivery->handle;
    vw_worker_translation_transport_state.running = delivery->running;
    vw_worker_translation_transport_state.fatal_exit = delivery->fatal_exit;
    atomic_store(&vw_worker_translation_transport_state.failed, false);
  }
}

static inline void vw_worker_translation_diag_mark_transport_failed(vw_ipc_handle_t* handle) {
  if (!handle) return;
  atomic_store(&vw_worker_translation_transport_state.failed, true);
  if (vw_worker_translation_transport_state.handle != handle) return;
  if (vw_worker_translation_transport_state.fatal_exit) {
    atomic_store(vw_worker_translation_transport_state.fatal_exit, true);
  }
  if (vw_worker_translation_transport_state.running) {
    atomic_store(vw_worker_translation_transport_state.running, false);
  }
}

// Once a diagnostic write has failed, never append another worker frame to a possibly partial ERROR frame.
static inline bool vw_worker_translation_diag_guarded_ipc_send(vw_ipc_handle_t* handle, const void* data, size_t size) {
  if (handle && vw_worker_translation_transport_state.handle == handle &&
      atomic_load(&vw_worker_translation_transport_state.failed)) {
    return false;
  }
  return vw_ipc_send(handle, data, size);
}

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
    written = snprintf(
        out, out_size, "segment=%llu cause=%s tier=%s attempts=0x%02x status=%u latency_ms=%u.%03u",
        (unsigned long long)result->segment.segment_id, vw_worker_translation_cause_name(result->failure.cause),
        vw_worker_translation_tier_name(result->failure.terminal_tier), (unsigned int)result->failure.attempted_tiers,
        (unsigned int)result->failure.provider_status, (unsigned int)whole_ms, (unsigned int)fractional_us);
  } else {
    written = snprintf(
        out, out_size, "segment=%llu cause=%s tier=%s attempts=0x%02x latency_ms=%u.%03u",
        (unsigned long long)result->segment.segment_id, vw_worker_translation_cause_name(result->failure.cause),
        vw_worker_translation_tier_name(result->failure.terminal_tier), (unsigned int)result->failure.attempted_tiers,
        (unsigned int)whole_ms, (unsigned int)fractional_us);
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

static inline void vw_worker_translation_diag_prepare_local_rejection(const vw_caption_segment_t* segment,
                                                                      vw_translate_async_result_t* result) {
  if (!result) return;
  memset(result, 0, sizeof(*result));
  if (segment) result->segment = *segment;
  result->attempted = true;
  result->success = false;
  result->segment.translation_attempted = true;
  result->segment.translation_tier = VW_TRANSLATE_TIER_NONE;
  result->segment.translation_latency_us = 0;
  result->failure.cause = VW_TRANSLATE_FAILURE_LOCAL;
}

static inline bool vw_worker_translation_diag_report_local_rejection(const vw_caption_segment_t* segment,
                                                                     vw_ipc_handle_t* handle, uint64_t* sequence,
                                                                     const vw_session_id_t* session_id) {
  if (!segment || !handle || !sequence || !session_id) return false;
  vw_translate_async_result_t result;
  vw_worker_translation_diag_prepare_local_rejection(segment, &result);
  vw_error_code_t code = E_TRANSLATION_LOCAL;
  char detail[VW_MAX_ERROR_MSG_BYTES];
  if (!vw_worker_translation_diag_build(&result, &code, detail, sizeof(detail))) return false;
  vw_worker_translation_delivery_view_t delivery = {.handle = handle,
                                                    .sequence = sequence,
                                                    .session_id = session_id,
                                                    .session_active = NULL,
                                                    .running = NULL,
                                                    .fatal_exit = NULL};
  return vw_worker_translation_diag_send(&delivery, code, detail);
}

typedef struct vw_worker_translation_diag_context {
  vw_translate_async_delivery_fn deliver;
  void* user_data;
  vw_worker_translation_delivery_view_t delivery;
} vw_worker_translation_diag_context_t;

static inline void vw_worker_translation_diag_deliver(const vw_translate_async_result_t* result, void* opaque) {
  vw_worker_translation_diag_context_t* context = (vw_worker_translation_diag_context_t*)opaque;
  if (!context || !context->deliver) return;
  vw_worker_translation_delivery_view_t* delivery = &context->delivery;
  bool active_delivery = delivery->session_active && *delivery->session_active && delivery->session_id;
  bool matching_session = false;
  if (result && active_delivery) {
    matching_session = memcmp(result->segment.session_id.bytes, delivery->session_id->bytes, VW_SESSION_ID_BYTES) == 0;
  }
  bool active_session = active_delivery && matching_session;

  vw_error_code_t code = E_TRANSLATION_LOCAL;
  char detail[VW_MAX_ERROR_MSG_BYTES];
  if (active_session && vw_worker_translation_diag_build(result, &code, detail, sizeof(detail)) &&
      !vw_worker_translation_diag_send(delivery, code, detail)) {
    vw_worker_translation_diag_mark_transport_failed(delivery->handle);
    if (delivery->fatal_exit) atomic_store(delivery->fatal_exit, true);
    if (delivery->running) atomic_store(delivery->running, false);
    return;
  }
  context->deliver(result, context->user_data);
}

static inline bool vw_worker_translate_async_try_deliver_scoped(vw_translate_async_t* async,
                                                                vw_translate_async_delivery_fn deliver, void* user_data,
                                                                vw_worker_translation_delivery_view_t delivery) {
  vw_worker_translation_diag_register_transport_state(&delivery);
  if (delivery.handle == vw_worker_translation_transport_state.handle &&
      atomic_load(&vw_worker_translation_transport_state.failed)) {
    if (delivery.fatal_exit) atomic_store(delivery.fatal_exit, true);
    if (delivery.running) atomic_store(delivery.running, false);
    return false;
  }
  vw_worker_translation_diag_context_t context = {.deliver = deliver, .user_data = user_data, .delivery = delivery};
  return vw_translate_async_try_deliver(async, vw_worker_translation_diag_deliver, &context);
}

static inline bool vw_worker_translate_async_submit_scoped(vw_translate_async_t* async,
                                                           const vw_caption_segment_t* segment, const char* source_lang,
                                                           const char* target_lang, vw_ipc_handle_t* handle,
                                                           uint64_t* sequence, const vw_session_id_t* session_id) {
  if (handle == vw_worker_translation_transport_state.handle &&
      atomic_load(&vw_worker_translation_transport_state.failed)) {
    // A prior partial diagnostic write already made this transport unusable. Treat this cue as handled so callers
    // never append a source-caption fallback while the fatal worker exit is in flight.
    return true;
  }
  if (vw_translate_async_submit(async, segment, source_lang, target_lang)) return true;

  // A cue that reached the worker translation call site but cannot enter the bounded async pipeline is a local
  // translation failure. Emit blame before the existing source-only fallback path sends the caption. If the
  // diagnostic itself cannot be sent, suppress that fallback and force the worker through its transport-fatal path;
  // writing anything after a partial ERROR frame would desynchronize the IPC stream.
  if (!vw_worker_translation_diag_report_local_rejection(segment, handle, sequence, session_id)) {
    vw_worker_translation_diag_mark_transport_failed(handle);
    return true;
  }
  return false;
}

#ifdef VW_WORKER_TRANSLATION_DIAG_OVERRIDE
#define VW_WORKER_TRANSLATION_SEQUENCE_PTR(value) _Generic((value), uint64_t *: (value), default: &(value))
#define VW_WORKER_TRANSLATION_SESSION_PTR(value) \
  _Generic((value), vw_session_id_t *: (value), const vw_session_id_t*: (value), default: &(value))
#define VW_WORKER_TRANSLATION_DELIVERY_VIEW(user_data)                                    \
  ((vw_worker_translation_delivery_view_t){.handle = (user_data)->handle,                 \
                                           .sequence = (user_data)->sequence,             \
                                           .session_id = (user_data)->session_id,         \
                                           .session_active = (user_data)->session_active, \
                                           .running = (user_data)->running,               \
                                           .fatal_exit = (user_data)->fatal_exit})
#define vw_translate_async_try_deliver(async, deliver, user_data)               \
  vw_worker_translate_async_try_deliver_scoped((async), (deliver), (user_data), \
                                               VW_WORKER_TRANSLATION_DELIVERY_VIEW(user_data))
#define vw_translate_async_submit(async, segment, source_lang, target_lang)                           \
  vw_worker_translate_async_submit_scoped((async), (segment), (source_lang), (target_lang), (handle), \
                                          VW_WORKER_TRANSLATION_SEQUENCE_PTR(sequence),               \
                                          VW_WORKER_TRANSLATION_SESSION_PTR(session_id))
#define vw_ipc_send(handle, data, size) vw_worker_translation_diag_guarded_ipc_send((handle), (data), (size))
#endif

#endif  // VW_WORKER_TRANSLATION_DIAG_OVERRIDE_H_
