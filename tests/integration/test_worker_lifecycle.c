#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _XOPEN_SOURCE 500
#include <unistd.h>

#include "vw_ipc_transport.h"
#include "vw_protocol_codec.h"
#include "vw_protocol_types.h"
#include "vw_test.h"
#include "vw_worker.h"
#include "vw_worker_client.h"

#if defined(__linux__)
// Same guard as test_whisper_engine: under memcheck, whisper.cpp's model load + Vulkan driver
// discovery produces loader-level false positives, so the model-gated section is skipped.
static int running_under_valgrind(void) {
  FILE* f = fopen("/proc/self/maps", "r");
  if (!f) return 0;
  char line[512];
  int found = 0;
  while (fgets(line, sizeof(line), f)) {
    if (strstr(line, "vgpreload") != NULL) {
      found = 1;
      break;
    }
  }
  fclose(f);
  return found;
}
#else
static int running_under_valgrind(void) { return 0; }
#endif

static void* worker_thread(void* arg) {
  vw_worker_config_t* config = (vw_worker_config_t*)arg;
  int res = vw_worker_run(config);
  return (void*)(intptr_t)res;
}

int main(void) {
  vw_worker_config_t config;
  memset(&config, 0, sizeof(config));
#ifdef _WIN32
  // Windows named pipes require the \\\\.\\pipe\\ prefix (Unix sockets take a bare path).
  strncpy(config.pipe_name, "\\\\.\\pipe\\test_lifecycle_socket", sizeof(config.pipe_name) - 1);
#else
  strncpy(config.pipe_name, "test_lifecycle_socket", sizeof(config.pipe_name) - 1);
#endif
  for (int i = 0; i < VW_AUTH_TOKEN_BYTES; i++) config.auth_token[i] = (uint8_t)i;

  pthread_t thread;
  int err = pthread_create(&thread, NULL, worker_thread, &config);
  (void)err;
  assert(err == 0);
  usleep(100000);

  // 1. Try with wrong token: handshake must fail and the worker must exit
  uint8_t bad_token[VW_AUTH_TOKEN_BYTES] = {0};
  vw_worker_client_t* bad_client = vw_worker_client_launch_and_connect(NULL, config.pipe_name, bad_token, NULL);
  EXPECT(bad_client == NULL);  // HELLO rejected by worker

  void* ret_val;
  pthread_join(thread, &ret_val);
  EXPECT((int)(intptr_t)ret_val == 1);  // Worker exited due to bad auth

  // Restart listener
  err = pthread_create(&thread, NULL, worker_thread, &config);
  (void)err;
  assert(err == 0);
  usleep(100000);

  // 2. Connect with good token (handshake inside), send START_SESSION, then SHUTDOWN
  vw_worker_client_t* client = vw_worker_client_launch_and_connect(NULL, config.pipe_name, config.auth_token, NULL);
  EXPECT(client != NULL);  // HELLO handshake completed

  vw_msg_start_t start_msg = {.timeline_origin_pts_us = 0,
                              .sample_rate = 16000,
                              .channels = 1,
                              .sample_format = 1,
                              .model_id = "ggml-tiny.en.bin",
                              .language = "en",
                              .source_kind = VW_SOURCE_LOCAL_FILE};

  uint8_t payload_buf[1024];
  size_t written = 0;
  EXPECT(vw_protocol_encode_payload(VW_MSG_START_SESSION, &start_msg, payload_buf, sizeof(payload_buf), &written));

  vw_frame_header_t hdr;
  hdr.magic = VW_PROTOCOL_MAGIC;
  hdr.major = VW_PROTOCOL_VERSION_MAJOR;
  hdr.type = VW_MSG_START_SESSION;
  hdr.payload_length = written;
  hdr.sequence = 1;

  uint8_t hdr_buf[20];
  EXPECT(vw_protocol_encode_header(&hdr, hdr_buf, 20));

  vw_ipc_send((vw_ipc_handle_t*)client->pipe_handle, hdr_buf, 20);
  vw_ipc_send((vw_ipc_handle_t*)client->pipe_handle, payload_buf, written);

  usleep(50000);

  // Now send SHUTDOWN
  hdr.type = VW_MSG_SHUTDOWN;
  hdr.payload_length = 0;
  hdr.sequence = 2;
  EXPECT(vw_protocol_encode_header(&hdr, hdr_buf, 20));
  vw_ipc_send((vw_ipc_handle_t*)client->pipe_handle, hdr_buf, 20);

  pthread_join(thread, &ret_val);
  EXPECT((int)(intptr_t)ret_val == 0);  // Worker exited successfully

  vw_worker_client_disconnect(client);

  // 3. Client-side failure paths: bad args and no listener
  EXPECT(vw_worker_client_launch_and_connect(NULL, NULL, config.auth_token, NULL) == NULL);  // NULL endpoint
  EXPECT(vw_worker_client_launch_and_connect(NULL, "no_such_lifecycle_endpoint", config.auth_token, NULL) ==
         NULL);                                                                             // no listener
  EXPECT(vw_worker_client_launch_and_connect(NULL, config.pipe_name, NULL, NULL) == NULL);  // NULL token

  // 4. First frame is not HELLO: worker must reject the connection and exit non-zero
  err = pthread_create(&thread, NULL, worker_thread, &config);
  (void)err;
  assert(err == 0);
  usleep(100000);

  vw_ipc_handle_t* raw = vw_ipc_connect(config.pipe_name);
  EXPECT(raw != NULL);

  vw_frame_header_t non_hello = {.magic = VW_PROTOCOL_MAGIC,
                                 .major = VW_PROTOCOL_VERSION_MAJOR,
                                 .type = VW_MSG_SHUTDOWN,
                                 .payload_length = 0,
                                 .sequence = 1};
  uint8_t non_hello_buf[sizeof(vw_frame_header_t)];
  EXPECT(vw_protocol_encode_header(&non_hello, non_hello_buf, sizeof(non_hello_buf)));
  vw_ipc_send(raw, non_hello_buf, sizeof(non_hello_buf));

  pthread_join(thread, &ret_val);
  EXPECT((int)(intptr_t)ret_val == 1);  // First message must be HELLO
  vw_ipc_close(raw);

  // 5. Worker with zeroed model_path (engine NULL): START via the client API must fail with the
  // ERROR path (E_MODEL_MISSING), proving the split preserves worker-side model rejection.
  {
    vw_worker_config_t no_model = config;
    memset(no_model.model_path, 0, sizeof(no_model.model_path));  // engine init fails -> NULL
    err = pthread_create(&thread, NULL, worker_thread, &no_model);
    (void)err;
    assert(err == 0);
    usleep(100000);

    vw_worker_client_t* c = vw_worker_client_launch_and_connect(NULL, no_model.pipe_name, no_model.auth_token, NULL);
    EXPECT(c != NULL);
    EXPECT(!vw_worker_client_start_session(c, 0, "tiny.en", NULL));  // E_MODEL_MISSING ERROR reply

    // Clean shutdown of the still-running worker.
    vw_worker_client_shutdown(c);
    pthread_join(thread, &ret_val);
    EXPECT((int)(intptr_t)ret_val == 0);
    vw_worker_client_disconnect(c);
  }

  // 6. Model worker (only if models/ggml-tiny.en.bin is present; else print a skip notice):
  // STARTED via the client API, 4 synthetic silence chunks streamed, STOP, SHUTDOWN, exit 0.
  const char* model_paths[] = {"models/ggml-tiny.en.bin", "../../../models/ggml-tiny.en.bin",
                               "../../models/ggml-tiny.en.bin", "../models/ggml-tiny.en.bin"};
  const char* model_path = NULL;
  for (size_t i = 0; i < sizeof(model_paths) / sizeof(model_paths[0]); i++) {
    FILE* f = fopen(model_paths[i], "rb");
    if (f) {
      fclose(f);
      model_path = model_paths[i];
      break;
    }
  }
  if (!model_path) {
    printf("test_worker_lifecycle: model ggml-tiny.en.bin absent - skipping model-gated section (exit 0)\n");
  } else if (running_under_valgrind()) {
    // Same policy as test_whisper_engine: skip heavy model load under memcheck (loader noise).
    printf("test_worker_lifecycle: running under Valgrind - skipping model-gated section\n");
  } else {
    vw_worker_config_t with_model = config;
    memset(with_model.pipe_name, 0, sizeof(with_model.pipe_name));
#ifdef _WIN32
    strncpy(with_model.pipe_name, "\\\\.\\pipe\\test_lifecycle_model_socket", sizeof(with_model.pipe_name) - 1);
#else
    strncpy(with_model.pipe_name, "test_lifecycle_model_socket", sizeof(with_model.pipe_name) - 1);
#endif
    strncpy(with_model.model_path, model_path, sizeof(with_model.model_path) - 1);

    err = pthread_create(&thread, NULL, worker_thread, &with_model);
    (void)err;
    assert(err == 0);
    usleep(100000);

    vw_worker_client_t* c =
        vw_worker_client_launch_and_connect(NULL, with_model.pipe_name, with_model.auth_token, NULL);
    EXPECT(c != NULL);
    EXPECT(vw_worker_client_start_session(c, 0, "tiny.en", NULL));

    // 4 synthetic silence chunks: 512ms each (16384 bytes at 16kHz S16LE — the chunk's inline
    // pcm_data cap), staggered PTS. A full 1s chunk (32000 bytes) cannot fit the inline array.
    for (int i = 0; i < 4; i++) {
      vw_audio_chunk_t chunk = {
          .start_pts_us = (int64_t)i * 512000,
          .duration_us = 512000,
          .sample_rate = 16000,
          .channels = 1,
          .bytes = 16384,
      };
      EXPECT(vw_worker_client_send_audio(c, &chunk));
    }
    // Pause/resume mid-stream: worker must accept both without dying; the session stays active
    // so the subsequent STOP flow still works (exit 0 proves the worker survived).
    vw_worker_client_pause_session(c);
    vw_worker_client_resume_session(c);
    // Step 17: seek restart — STOP(SEEK_DISCONTINUITY) then START again on the same connection.
    // The worker must accept the new epoch (new session_id), drop stale pre-seek AUDIO, and
    // continue transcribing; exit 0 proves the full STOP->START cycle.
    vw_worker_client_stop_session(c, VW_CTRL_REASON_SEEK_DISCONTINUITY);
    EXPECT(vw_worker_client_start_session(c, 4000000, "tiny.en", NULL));  // new epoch, new session_id
    for (int i = 0; i < 2; i++) {
      vw_audio_chunk_t chunk2 = {
          .start_pts_us = 4000000 + (int64_t)i * 512000,
          .duration_us = 512000,
          .sample_rate = 16000,
          .channels = 1,
          .bytes = 16384,
      };
      EXPECT(vw_worker_client_send_audio(c, &chunk2));
    }
    vw_worker_client_stop_session(c, 0);
    vw_worker_client_shutdown(c);
    pthread_join(thread, &ret_val);
    EXPECT((int)(intptr_t)ret_val == 0);
    vw_worker_client_disconnect(c);
  }

  printf("test_worker_lifecycle PASSED\n");
  return 0;
}
