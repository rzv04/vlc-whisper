#ifndef VW_TEST_WORKER_HARNESS_H_
#define VW_TEST_WORKER_HARNESS_H_

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "vw_platform.h"
#include "vw_worker.h"
#include "vw_worker_client.h"
#include "vw_worker_config.h"

typedef struct vw_test_worker_fixture {
  vw_worker_config_t config;
  pthread_t thread;
  bool thread_started;
  vw_worker_client_t* client;
} vw_test_worker_fixture_t;

static inline long vw_test_process_id(void) {
#ifdef _WIN32
  return (long)_getpid();
#else
  return (long)getpid();
#endif
}

static inline void* vw_test_worker_thread_main(void* arg) {
  vw_worker_config_t* config = (vw_worker_config_t*)arg;
  return (void*)(intptr_t)vw_worker_run(config);
}

static inline bool vw_test_worker_fixture_start(vw_test_worker_fixture_t* fixture, const char* suffix) {
  if (!fixture || !suffix) return false;
  memset(fixture, 0, sizeof(*fixture));
  if (!vw_worker_config_init_defaults(&fixture->config)) return false;
#ifdef _WIN32
  snprintf(fixture->config.pipe_name, sizeof(fixture->config.pipe_name), "\\\\.\\pipe\\vw-test-%s-%ld", suffix,
           vw_test_process_id());
#else
  snprintf(fixture->config.pipe_name, sizeof(fixture->config.pipe_name), "/tmp/vw-test-%s-%ld.sock", suffix,
           vw_test_process_id());
#endif
  for (size_t i = 0; i < VW_AUTH_TOKEN_BYTES; i++) fixture->config.auth_token[i] = (uint8_t)(0x40U + i);
  snprintf(fixture->config.model_path, sizeof(fixture->config.model_path), "%s", "stub-model.bin");
  snprintf(fixture->config.language, sizeof(fixture->config.language), "%s", "en");
  fixture->config.backend = VW_WORKER_BACKEND_CPU;
  fixture->config.n_threads = 1;

  if (pthread_create(&fixture->thread, NULL, vw_test_worker_thread_main, &fixture->config) != 0) return false;
  fixture->thread_started = true;
  vw_platform_sleep_ms(100);
  fixture->client =
      vw_worker_client_launch_and_connect(NULL, fixture->config.pipe_name, fixture->config.auth_token, NULL);
  return fixture->client != NULL;
}

static inline bool vw_test_send_audio_chunks(vw_worker_client_t* client, int chunk_count, int64_t start_pts_us) {
  if (!client || chunk_count < 0) return false;
  for (int i = 0; i < chunk_count; i++) {
    vw_audio_chunk_t chunk = {
        .start_pts_us = start_pts_us + (int64_t)i * 512000,
        .duration_us = 512000,
        .sample_rate = 16000,
        .channels = 1,
        .bytes = 16384,
    };
    if (!vw_worker_client_send_audio(client, &chunk)) return false;
  }
  return true;
}

static inline int vw_test_worker_fixture_shutdown(vw_test_worker_fixture_t* fixture) {
  if (!fixture) return 1;
  if (fixture->client) vw_worker_client_shutdown(fixture->client);
  int worker_result = 1;
  if (fixture->thread_started) {
    void* thread_result = NULL;
    if (pthread_join(fixture->thread, &thread_result) == 0) worker_result = (int)(intptr_t)thread_result;
    fixture->thread_started = false;
  }
  if (fixture->client) {
    vw_worker_client_disconnect(fixture->client);
    fixture->client = NULL;
  }
  return worker_result;
}

#endif  // VW_TEST_WORKER_HARNESS_H_
