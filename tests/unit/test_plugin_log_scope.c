#include <stdint.h>

#include "vw_log.h"
#include "vw_plugin_log_scope_override.h"
#include "vw_test.h"

static int g_first_calls = 0;
static int g_second_calls = 0;

static void vw_test_first_sink(vw_log_level_t level, const char* event_id, const char* formatted_msg, void* user_data) {
  (void)level;
  (void)event_id;
  (void)formatted_msg;
  vw_test_check_true("first sink keeps its instance identity", user_data == (void*)(uintptr_t)0x100U);
  g_first_calls++;
}

static void vw_test_second_sink(vw_log_level_t level, const char* event_id, const char* formatted_msg,
                                void* user_data) {
  (void)level;
  (void)event_id;
  (void)formatted_msg;
  vw_test_check_true("second sink keeps its instance identity", user_data == (void*)(uintptr_t)0x200U);
  g_second_calls++;
}

int main(void) {
  void* first = (void*)(uintptr_t)0x100U;
  void* second = (void*)(uintptr_t)0x200U;
  vw_log_set_enabled(true);
  vw_plugin_log_set_sink_scoped(vw_test_first_sink, first, first);
  vw_plugin_log_set_sink_scoped(vw_test_second_sink, second, second);

  vw_log_event(VW_LOG_LEVEL_INFO, "SCOPE_TEST", "both active");
  vw_test_check_true("both instances receive the first event", g_first_calls == 1 && g_second_calls == 1);

  // Close the first-opened instance before the second. The second must remain registered and callable.
  vw_plugin_log_set_sink_scoped(NULL, NULL, first);
  vw_log_event(VW_LOG_LEVEL_INFO, "SCOPE_TEST", "second remains");
  vw_test_check_true("out-of-order teardown removes only the closing instance",
                     g_first_calls == 1 && g_second_calls == 2);

  vw_plugin_log_set_sink_scoped(NULL, NULL, second);
  vw_log_set_enabled(false);
  return vw_test_finish("test_plugin_log_scope");
}
