#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vw_log.h"
#include "vw_test.h"

static int g_sink1_calls = 0;
static int g_sink2_calls = 0;
static char g_last_sink1_msg[256];
static char g_last_sink2_msg[256];

static void mock_sink1(vw_log_level_t level, const char* event_id, const char* formatted_msg, void* user_data) {
  (void)level;
  (void)event_id;
  assert(user_data == (void*)0x100);
  g_sink1_calls++;
  snprintf(g_last_sink1_msg, sizeof(g_last_sink1_msg), "%s", formatted_msg);
}

static void mock_sink2(vw_log_level_t level, const char* event_id, const char* formatted_msg, void* user_data) {
  (void)level;
  (void)event_id;
  assert(user_data == (void*)0x200);
  g_sink2_calls++;
  snprintf(g_last_sink2_msg, sizeof(g_last_sink2_msg), "%s", formatted_msg);
}

static void test_multi_instance_sink(void) {
  vw_log_set_enabled(true);
  g_sink1_calls = 0;
  g_sink2_calls = 0;

  // Register two independent sinks (e.g. two plugin instances)
  vw_log_set_sink(mock_sink1, (void*)0x100);
  vw_log_set_sink(mock_sink2, (void*)0x200);

  vw_log_event(VW_LOG_LEVEL_INFO, "TEST_EVENT", "hello %s %d", "world", 42);
  vw_test_check_true("sink 1 received event", g_sink1_calls == 1);
  vw_test_check_true("sink 2 received event", g_sink2_calls == 1);
  vw_test_check_true("sink 1 formatted message matches", strcmp(g_last_sink1_msg, "hello world 42") == 0);
  vw_test_check_true("sink 2 formatted message matches", strcmp(g_last_sink2_msg, "hello world 42") == 0);

  // Unregister instance 1; instance 2 must remain active
  vw_log_set_sink(NULL, (void*)0x100);
  vw_log_event(VW_LOG_LEVEL_WARN, "TEST_EVENT2", "active message");
  vw_test_check_true("sink 1 stayed at 1 call after unregistering", g_sink1_calls == 1);
  vw_test_check_true("sink 2 received second event", g_sink2_calls == 2);
  vw_test_check_true("sink 2 has updated message", strcmp(g_last_sink2_msg, "active message") == 0);

  // Unregister instance 2
  vw_log_set_sink(NULL, (void*)0x200);
  vw_log_event(VW_LOG_LEVEL_ERROR, "TEST_EVENT3", "fallback message");
  vw_test_check_true("sink 2 stayed at 2 calls after unregistering", g_sink2_calls == 2);

  vw_log_set_enabled(false);
}

static void test_file_output_and_flush(void) {
  char temp_path[] = "/tmp/vw_test_log_XXXXXX";
  int fd = mkstemp(temp_path);
  vw_test_check_true("mkstemp created file", fd >= 0);
  FILE* fp = fdopen(fd, "w+");
  vw_test_check_true("fdopen succeeded", fp != NULL);

  vw_log_set_enabled(true);
  vw_log_set_file(fp);

  vw_log_event(VW_LOG_LEVEL_INFO, "FILE_TEST", "logged to file %d", 123);
  vw_log_flush();

  // Read back contents
  fflush(fp);
  fseek(fp, 0, SEEK_SET);
  char buf[512] = {0};
  size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
  buf[n] = '\0';

  vw_test_check_true("log file received formatted event", strstr(buf, "[INFO] [FILE_TEST] logged to file 123") != NULL);

  vw_log_set_file(NULL);
  fclose(fp);
  unlink(temp_path);
  vw_log_set_enabled(false);
}

static void* thread_logger_worker(void* arg) {
  (void)arg;
  for (int i = 0; i < 200; i++) {
    vw_log_event(VW_LOG_LEVEL_DEBUG, "CONC_EVENT", "iteration %d", i);
    if (i % 50 == 0) {
      vw_log_flush();
    }
  }
  return NULL;
}

static void test_concurrent_logging(void) {
  vw_log_set_enabled(true);
  pthread_t threads[4];
  for (int i = 0; i < 4; i++) {
    pthread_create(&threads[i], NULL, thread_logger_worker, NULL);
  }
  for (int i = 0; i < 4; i++) {
    pthread_join(threads[i], NULL);
  }
  vw_log_flush();
  vw_log_set_enabled(false);
  vw_test_check_true("concurrent logging completed without crashing", true);
}

int main(void) {
  test_multi_instance_sink();
  test_file_output_and_flush();
  test_concurrent_logging();
  return vw_test_finish("test_log");
}
