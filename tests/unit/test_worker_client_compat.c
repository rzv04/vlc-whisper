#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vw_protocol_types.h"
#include "vw_worker_client.h"

int main(void) {
  // Model a same-major v1.4 worker: authenticated transport exists, but the worker did not advertise the optional
  // v1.5 translation capability. No IPC call is allowed from either assertion below; the fake handle is deliberately
  // non-dereferenceable so an accidental send would fail loudly under the test suite.
  vw_worker_client_t legacy;
  memset(&legacy, 0, sizeof(legacy));
  legacy.pipe_handle = (void*)(uintptr_t)1;
  legacy.worker_protocol_minor = 4;
  legacy.worker_capabilities = VW_CAPABILITY_PCM_S16LE_16K_MONO;

  assert(vw_worker_client_send_translate_ctrl(&legacy, false, "auto", "ro", 1));
  assert(!vw_worker_client_send_translate_ctrl(&legacy, true, "auto", "ro", 1));

  // A v1.5 worker advertises the explicit capability. The actual serialization/send path is covered by the protocol
  // codec/integration suite; this test exists specifically to guarantee that legacy peers are never sent message 16.
  legacy.worker_protocol_minor = 5;
  legacy.worker_capabilities |= VW_CAPABILITY_TRANSLATION;
  assert((legacy.worker_capabilities & VW_CAPABILITY_TRANSLATION) != 0);

  // Oversized source URLs are rejected before any transport write. The deliberately invalid handle proves the
  // validation cannot silently truncate and continue into IPC.
  vw_worker_client_t bounded;
  memset(&bounded, 0, sizeof(bounded));
  bounded.pipe_handle = (void*)(uintptr_t)1;
  char oversized_url[VW_MAX_SOURCE_URL_BYTES + 1U];
  memset(oversized_url, 'x', sizeof(oversized_url));
  oversized_url[sizeof(oversized_url) - 1U] = '\0';
  assert(!vw_worker_client_start_session(&bounded, 0, "tiny", oversized_url));

  printf("test_worker_client_compat PASSED.\n");
  return 0;
}
