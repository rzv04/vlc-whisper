#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "vw_translate.h"

#define VW_TEST_TRANSLATE_RPC_BODY_BYTES 65536U

typedef struct vw_stack_test_state {
  int calls;
} vw_stack_test_state_t;

static bool vw_stack_test_http_hook(const char* host, const char* path, const char* body, const char* content_type,
                                    char* out_buf, size_t buf_size, uint32_t timeout_ms, void* user_data) {
  (void)body;
  (void)content_type;
  (void)timeout_ms;
  vw_stack_test_state_t* state = (vw_stack_test_state_t*)user_data;
  state->calls++;
  if (strstr(path, "batchexecute") != NULL) return false;
  assert(strcmp(host, "translate.googleapis.com") == 0);
  const char* response = "[[[\"Salut lume\",\"Hello world\",null,null,1]],null,\"en\"]";
  assert(strlen(response) + 1U < buf_size);
  memcpy(out_buf, response, strlen(response) + 1U);
  return true;
}

static void* vw_stack_test_thread(void* user_data) {
  vw_stack_test_state_t* state = (vw_stack_test_state_t*)user_data;
  char output[128];
  char body[VW_TEST_TRANSLATE_RPC_BODY_BYTES];
  uint8_t tier = VW_TRANSLATE_TIER_NONE;
  uint32_t latency_us = 0;
  assert(vw_translate_build_rpc_body_for_test("Hello world", "en", "ro", body, sizeof(body)));
  assert(vw_translate_text("Hello world", "en", "ro", output, sizeof(output), &tier, &latency_us));
  assert(strcmp(output, "Salut lume") == 0);
  assert(tier == VW_TRANSLATE_TIER_GTX);
  assert(state->calls == 2);
  return NULL;
}

int main(void) {
  vw_stack_test_state_t state = {0};
  vw_translate_set_test_http_hook(vw_stack_test_http_hook, &state);

  pthread_attr_t attr;
  assert(pthread_attr_init(&attr) == 0);
  assert(pthread_attr_setstacksize(&attr, 128U * 1024U) == 0);
  pthread_t thread;
  assert(pthread_create(&thread, &attr, vw_stack_test_thread, &state) == 0);
  assert(pthread_join(thread, NULL) == 0);
  assert(pthread_attr_destroy(&attr) == 0);
  vw_translate_set_test_http_hook(NULL, NULL);
  return 0;
}
