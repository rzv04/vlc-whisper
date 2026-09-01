#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "vw_ipc_transport.h"
#include "vw_platform.h"
#include "vw_protocol_codec.h"
#include "vw_protocol_types.h"
#include "vw_test.h"
#include "vw_worker_client.h"

// In-process mock server thread simulating worker IPC responses:
// 1. Listens on local Unix socket / named pipe.
// 2. Completes HELLO / HELLO_ACK handshake.
// 3. Receives START_SESSION and replies with STARTED after 100ms delay.
// 4. Receives AUDIO_PCM message using custom transport receive timeout.
// 5. Receives STOP_SESSION and SHUTDOWN control frames.
static void* vw_fake_server_thread(void* arg) {
  const char* endpoint = (const char*)arg;
  vw_ipc_handle_t* server = vw_ipc_listen(endpoint);
  if (!server) return (void*)1;

  // Step 1: Receive HELLO frame from client launch_and_connect
  uint8_t hdr_buf[20];
  if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
    vw_ipc_close(server);
    return (void*)2;
  }
  vw_frame_header_t hdr;
  vw_protocol_decode_header(hdr_buf, 20, &hdr);

  uint8_t payload[256];
  if (vw_ipc_receive(server, payload, hdr.payload_length) != (int32_t)hdr.payload_length) {
    vw_ipc_close(server);
    return (void*)3;
  }

  // Step 2: Send HELLO_ACK payload to confirm version & capability handshakes
  vw_msg_hello_ack_t ack = {
      .selected_major = VW_PROTOCOL_VERSION_MAJOR,
      .selected_minor = 0,
      .capability_flags = VW_CAPABILITY_PCM_S16LE_16K_MONO,
  };
  size_t ack_len = 0;
  vw_protocol_encode_payload(VW_MSG_HELLO_ACK, &ack, payload, sizeof(payload), &ack_len);
  vw_frame_header_t ack_hdr = {
      .magic = VW_PROTOCOL_MAGIC,
      .major = VW_PROTOCOL_VERSION_MAJOR,
      .type = VW_MSG_HELLO_ACK,
      .payload_length = (uint32_t)ack_len,
      .sequence = 1,
  };
  vw_protocol_encode_header(&ack_hdr, hdr_buf, 20);
  vw_ipc_send(server, hdr_buf, 20);
  vw_ipc_send(server, payload, ack_len);

  // Step 3: Model provisioning may arrive before START_SESSION. The zero session ID is intentional when it does.
  if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
    vw_ipc_close(server);
    return (void*)4;
  }
  vw_protocol_decode_header(hdr_buf, 20, &hdr);
  if (vw_ipc_receive(server, payload, hdr.payload_length) != (int32_t)hdr.payload_length) {
    vw_ipc_close(server);
    return (void*)6;
  }
  if (hdr.type == VW_MSG_MODEL_CTRL) {
    vw_msg_model_ctrl_t model_ctrl;
    const uint8_t zero_session[VW_SESSION_ID_BYTES] = {0};
    if (!vw_protocol_decode_payload(VW_MSG_MODEL_CTRL, payload, hdr.payload_length, &model_ctrl) ||
        memcmp(model_ctrl.session_id.bytes, zero_session, sizeof(zero_session)) != 0 ||
        model_ctrl.action != VW_MODEL_ACTION_DOWNLOAD || strcmp(model_ctrl.model_id, "tiny") != 0) {
      vw_ipc_close(server);
      return (void*)7;
    }

    // Step 4: Receive START_SESSION after the pre-session model request.
    if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
      vw_ipc_close(server);
      return (void*)8;
    }
    vw_protocol_decode_header(hdr_buf, 20, &hdr);
    if (hdr.type != VW_MSG_START_SESSION) {
      vw_ipc_close(server);
      return (void*)9;
    }
    if (vw_ipc_receive(server, payload, hdr.payload_length) != (int32_t)hdr.payload_length) {
      vw_ipc_close(server);
      return (void*)10;
    }
  } else if (hdr.type != VW_MSG_START_SESSION) {
    vw_ipc_close(server);
    return (void*)10;
  }
  vw_msg_start_t start;
  if (!vw_protocol_decode_payload(VW_MSG_START_SESSION, payload, hdr.payload_length, &start) ||
      strcmp(start.language, "ro") != 0) {
    vw_ipc_close(server);
    return (void*)17;
  }

  // Step 5: Delay 100ms (testing client's polling/waiting loop), then reply with zero-payload STARTED frame
  vw_platform_sleep_ms(100);
  vw_frame_header_t started_hdr = {
      .magic = VW_PROTOCOL_MAGIC,
      .major = VW_PROTOCOL_VERSION_MAJOR,
      .type = VW_MSG_STARTED,
      .payload_length = 0,
      .sequence = 2,
  };
  vw_protocol_encode_header(&started_hdr, hdr_buf, 20);
  vw_ipc_send(server, hdr_buf, 20);

  // Step 6: Receive AUDIO_PCM payload sent by vw_worker_client_send_audio (verifying vw_ipc_receive_timeout)
  if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
    vw_ipc_close(server);
    return (void*)7;
  }
  vw_protocol_decode_header(hdr_buf, 20, &hdr);
  if (hdr.type != VW_MSG_AUDIO_PCM) {
    vw_ipc_close(server);
    return (void*)8;
  }
  uint8_t* big_payload = (uint8_t*)malloc(hdr.payload_length);
  if (!big_payload) {
    vw_ipc_close(server);
    return (void*)9;
  }
  if (vw_ipc_receive_timeout(server, big_payload, hdr.payload_length, 3000000) != (int32_t)hdr.payload_length) {
    free(big_payload);
    vw_ipc_close(server);
    return (void*)9;
  }
  free(big_payload);

  // Step 7: Receive PAUSE control frame sent by vw_worker_client_pause_session
  if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
    vw_ipc_close(server);
    return (void*)10;
  }
  vw_protocol_decode_header(hdr_buf, 20, &hdr);
  if (hdr.type != VW_MSG_PAUSE) {
    vw_ipc_close(server);
    return (void*)11;
  }
  if (vw_ipc_receive(server, payload, hdr.payload_length) != (int32_t)hdr.payload_length) {
    vw_ipc_close(server);
    return (void*)12;
  }
  vw_msg_control_t pause_ctrl;
  vw_protocol_decode_payload(VW_MSG_PAUSE, payload, hdr.payload_length, &pause_ctrl);
  if (pause_ctrl.reason != VW_CTRL_REASON_USER_PAUSE) {
    vw_ipc_close(server);
    return (void*)15;
  }

  // Step 8: Receive RESUME control frame sent by vw_worker_client_resume_session
  if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
    vw_ipc_close(server);
    return (void*)13;
  }
  vw_protocol_decode_header(hdr_buf, 20, &hdr);
  if (hdr.type != VW_MSG_RESUME) {
    vw_ipc_close(server);
    return (void*)14;
  }
  if (vw_ipc_receive(server, payload, hdr.payload_length) != (int32_t)hdr.payload_length) {
    vw_ipc_close(server);
    return (void*)16;
  }
  vw_msg_control_t resume_ctrl;
  vw_protocol_decode_payload(VW_MSG_RESUME, payload, hdr.payload_length, &resume_ctrl);
  if (resume_ctrl.reason != VW_CTRL_REASON_USER_RESUME) {
    vw_ipc_close(server);
    return (void*)17;
  }

  // Step 9: Receive STOP_SESSION control frame sent by vw_worker_client_stop_session
  if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
    vw_ipc_close(server);
    return (void*)18;
  }
  vw_protocol_decode_header(hdr_buf, 20, &hdr);
  if (hdr.type != VW_MSG_STOP_SESSION) {
    vw_ipc_close(server);
    return (void*)19;
  }
  if (vw_ipc_receive(server, payload, hdr.payload_length) != (int32_t)hdr.payload_length) {
    vw_ipc_close(server);
    return (void*)20;
  }
  vw_msg_control_t stop_ctrl;
  vw_protocol_decode_payload(VW_MSG_STOP_SESSION, payload, hdr.payload_length, &stop_ctrl);
  if (stop_ctrl.reason != VW_CTRL_REASON_SEEK_DISCONTINUITY) {
    vw_ipc_close(server);
    return (void*)23;
  }

  // Step 10: Receive SHUTDOWN control frame sent by vw_worker_client_shutdown
  if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
    vw_ipc_close(server);
    return (void*)21;
  }
  vw_protocol_decode_header(hdr_buf, 20, &hdr);
  if (hdr.type != VW_MSG_SHUTDOWN) {
    vw_ipc_close(server);
    return (void*)22;
  }

  vw_ipc_close(server);
  return (void*)0;
}

// Fake server that completes the handshake, then pushes worker->plugin frames in order: PAUSE
// (unknown type the receiver must skip), CAPTION_SEGMENT, STATUS, ERROR — then closes. Used to
// exercise vw_worker_client_receive_frame's drain/decoder/EOF paths.
static void* vw_fake_server_frames_thread(void* arg) {
  const char* endpoint = (const char*)arg;
  vw_ipc_handle_t* server = vw_ipc_listen(endpoint);
  if (!server) return (void*)1;

  // HELLO / HELLO_ACK handshake (same shape as vw_fake_server_thread)
  uint8_t hdr_buf[20];
  if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
    vw_ipc_close(server);
    return (void*)2;
  }
  vw_frame_header_t hdr;
  vw_protocol_decode_header(hdr_buf, 20, &hdr);
  uint8_t payload[512];
  if (vw_ipc_receive(server, payload, hdr.payload_length) != (int32_t)hdr.payload_length) {
    vw_ipc_close(server);
    return (void*)3;
  }
  vw_msg_hello_ack_t ack = {.selected_major = VW_PROTOCOL_VERSION_MAJOR,
                            .selected_minor = 0,
                            .capability_flags = VW_CAPABILITY_PCM_S16LE_16K_MONO};
  size_t ack_len = 0;
  vw_protocol_encode_payload(VW_MSG_HELLO_ACK, &ack, payload, sizeof(payload), &ack_len);
  vw_frame_header_t ack_hdr = {.magic = VW_PROTOCOL_MAGIC,
                               .major = VW_PROTOCOL_VERSION_MAJOR,
                               .type = VW_MSG_HELLO_ACK,
                               .payload_length = (uint32_t)ack_len,
                               .sequence = 1};
  vw_protocol_encode_header(&ack_hdr, hdr_buf, 20);
  vw_ipc_send(server, hdr_buf, 20);
  vw_ipc_send(server, payload, ack_len);

  // Receive START_SESSION, reply STARTED (header-only)
  if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
    vw_ipc_close(server);
    return (void*)4;
  }
  vw_protocol_decode_header(hdr_buf, 20, &hdr);
  if (hdr.type != VW_MSG_START_SESSION) {
    vw_ipc_close(server);
    return (void*)5;
  }
  if (vw_ipc_receive(server, payload, hdr.payload_length) != (int32_t)hdr.payload_length) {
    vw_ipc_close(server);
    return (void*)6;
  }
  vw_frame_header_t started_hdr = {.magic = VW_PROTOCOL_MAGIC,
                                   .major = VW_PROTOCOL_VERSION_MAJOR,
                                   .type = VW_MSG_STARTED,
                                   .payload_length = 0,
                                   .sequence = 2};
  vw_protocol_encode_header(&started_hdr, hdr_buf, 20);
  vw_ipc_send(server, hdr_buf, 20);

  // Frame 1: PAUSE control frame (unknown type to receive_frame — must be drained and skipped).
  vw_msg_control_t pause = {.reason = 1};
  memset(pause.session_id.bytes, 0xAB, VW_SESSION_ID_BYTES);
  size_t pause_len = 0;
  vw_protocol_encode_payload(VW_MSG_PAUSE, &pause, payload, sizeof(payload), &pause_len);
  vw_frame_header_t pause_hdr = {.magic = VW_PROTOCOL_MAGIC,
                                 .major = VW_PROTOCOL_VERSION_MAJOR,
                                 .type = VW_MSG_PAUSE,
                                 .payload_length = (uint32_t)pause_len,
                                 .sequence = 3};
  vw_protocol_encode_header(&pause_hdr, hdr_buf, 20);
  vw_ipc_send(server, hdr_buf, 20);
  vw_ipc_send(server, payload, pause_len);

  // Frame 2: maximum-size CAPTION_SEGMENT ending in a multibyte code point.
  char max_text[VW_MAX_TEXT_BYTES + 1U];
  memset(max_text, 'a', VW_MAX_TEXT_BYTES - 3U);
  memcpy(max_text + VW_MAX_TEXT_BYTES - 3U, "\xE2\x82\xAC", 3U);
  max_text[VW_MAX_TEXT_BYTES] = '\0';
  vw_caption_segment_t seg = {.segment_id = 7,
                              .start_pts_us = 1000000,
                              .end_pts_us = 2000000,
                              .is_final = true,
                              .text_utf8 = max_text,
                              .text_bytes = VW_MAX_TEXT_BYTES};
  memset(seg.session_id.bytes, 0xAB, VW_SESSION_ID_BYTES);
  size_t seg_len = 0;
  uint8_t seg_buf[VW_CAPTION_SEGMENT_FIXED_BYTES + VW_MAX_TEXT_BYTES];
  vw_protocol_encode_payload(VW_MSG_CAPTION_SEGMENT, &seg, seg_buf, sizeof(seg_buf), &seg_len);
  vw_frame_header_t seg_hdr = {.magic = VW_PROTOCOL_MAGIC,
                               .major = VW_PROTOCOL_VERSION_MAJOR,
                               .type = VW_MSG_CAPTION_SEGMENT,
                               .payload_length = (uint32_t)seg_len,
                               .sequence = 4};
  vw_protocol_encode_header(&seg_hdr, hdr_buf, 20);
  vw_ipc_send(server, hdr_buf, 20);
  vw_ipc_send(server, seg_buf, seg_len);

  // Frame 3: STATUS
  vw_msg_status_t st = {.state = 1, .queued_audio_us = 4000000, .inference_us = 300000, .dropped_audio_us = 12345};
  memset(st.session_id.bytes, 0xAB, VW_SESSION_ID_BYTES);
  size_t st_len = 0;
  vw_protocol_encode_payload(VW_MSG_STATUS, &st, payload, sizeof(payload), &st_len);
  vw_frame_header_t st_hdr = {.magic = VW_PROTOCOL_MAGIC,
                              .major = VW_PROTOCOL_VERSION_MAJOR,
                              .type = VW_MSG_STATUS,
                              .payload_length = (uint32_t)st_len,
                              .sequence = 5};
  vw_protocol_encode_header(&st_hdr, hdr_buf, 20);
  vw_ipc_send(server, hdr_buf, 20);
  vw_ipc_send(server, payload, st_len);

  // Frame 4: ERROR (recoverable=1 so a receiver policy would continue; receive_frame must decode it)
  vw_msg_error_t err = {.error_code = E_BACKPRESSURE, .recoverable = 1};
  memset(err.session_id.bytes, 0xAB, VW_SESSION_ID_BYTES);
  snprintf(err.message, sizeof(err.message), "dropped audio");
  size_t err_len = 0;
  vw_protocol_encode_payload(VW_MSG_ERROR, &err, payload, sizeof(payload), &err_len);
  vw_frame_header_t err_hdr = {.magic = VW_PROTOCOL_MAGIC,
                               .major = VW_PROTOCOL_VERSION_MAJOR,
                               .type = VW_MSG_ERROR,
                               .payload_length = (uint32_t)err_len,
                               .sequence = 6};
  vw_protocol_encode_header(&err_hdr, hdr_buf, 20);
  vw_ipc_send(server, hdr_buf, 20);
  vw_ipc_send(server, payload, err_len);

  vw_ipc_close(server);  // EOF: next receive_frame must return VW_IPC_RECV_FATAL
  return (void*)0;
}

// Fake server that completes the handshake, then sends a 20-byte header that fails
// vw_protocol_decode_header (zero magic) and closes. Exercises the corrupt-header path: the client
// must report VW_IPC_RECV_FATAL and drop the transport — never the timeout value.
static void* vw_fake_server_bad_header_thread(void* arg) {
  const char* endpoint = (const char*)arg;
  vw_ipc_handle_t* server = vw_ipc_listen(endpoint);
  if (!server) return (void*)1;

  // HELLO / HELLO_ACK handshake (same shape as vw_fake_server_thread)
  uint8_t hdr_buf[20];
  if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
    vw_ipc_close(server);
    return (void*)2;
  }
  vw_frame_header_t hdr;
  vw_protocol_decode_header(hdr_buf, 20, &hdr);
  uint8_t payload[512];
  if (vw_ipc_receive(server, payload, hdr.payload_length) != (int32_t)hdr.payload_length) {
    vw_ipc_close(server);
    return (void*)3;
  }
  vw_msg_hello_ack_t ack = {.selected_major = VW_PROTOCOL_VERSION_MAJOR,
                            .selected_minor = 0,
                            .capability_flags = VW_CAPABILITY_PCM_S16LE_16K_MONO};
  size_t ack_len = 0;
  vw_protocol_encode_payload(VW_MSG_HELLO_ACK, &ack, payload, sizeof(payload), &ack_len);
  vw_frame_header_t ack_hdr = {.magic = VW_PROTOCOL_MAGIC,
                               .major = VW_PROTOCOL_VERSION_MAJOR,
                               .type = VW_MSG_HELLO_ACK,
                               .payload_length = (uint32_t)ack_len,
                               .sequence = 1};
  vw_protocol_encode_header(&ack_hdr, hdr_buf, 20);
  vw_ipc_send(server, hdr_buf, 20);
  vw_ipc_send(server, payload, ack_len);

  // Receive START_SESSION, reply STARTED (header-only)
  if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
    vw_ipc_close(server);
    return (void*)4;
  }
  vw_protocol_decode_header(hdr_buf, 20, &hdr);
  if (hdr.type != VW_MSG_START_SESSION) {
    vw_ipc_close(server);
    return (void*)5;
  }
  if (vw_ipc_receive(server, payload, hdr.payload_length) != (int32_t)hdr.payload_length) {
    vw_ipc_close(server);
    return (void*)6;
  }
  vw_frame_header_t started_hdr = {.magic = VW_PROTOCOL_MAGIC,
                                   .major = VW_PROTOCOL_VERSION_MAJOR,
                                   .type = VW_MSG_STARTED,
                                   .payload_length = 0,
                                   .sequence = 2};
  vw_protocol_encode_header(&started_hdr, hdr_buf, 20);
  vw_ipc_send(server, hdr_buf, 20);

  // Corrupt frame: 20 zero bytes (magic 0 != VW_PROTOCOL_MAGIC) — must fail decode/validate.
  memset(hdr_buf, 0, sizeof(hdr_buf));
  vw_ipc_send(server, hdr_buf, 20);

  vw_ipc_close(server);
  return (void*)0;
}

int main(void) {
#ifdef _WIN32
  // Named pipes require the \\\\.\\pipe\\ prefix on Windows.
  const char* pipe_name = "\\\\.\\pipe\\test_worker_client_socket";
#else
  char pipe_name[256];
  snprintf(pipe_name, sizeof(pipe_name), "/tmp/vlc-whisper-worker-client-%ld.sock", (long)getpid());
#endif
  uint8_t auth_token[VW_AUTH_TOKEN_BYTES] = {0};

  // Start in-process mock IPC server
  pthread_t thread;
  int err = pthread_create(&thread, NULL, vw_fake_server_thread, (void*)pipe_name);
  EXPECT(err == 0);

  vw_platform_sleep_ms(100);

  // Test 1: Connect and perform HELLO/HELLO_ACK handshake
  vw_worker_client_t* client =
      vw_worker_client_launch_and_connect_ex(NULL, pipe_name, auth_token, NULL, "auto", "ro", 4, -1, NULL, false);
  EXPECT(client != NULL);

  // Test 2: Send MODEL_CTRL before START_SESSION; the worker-scoped download path has no caption session yet.
  EXPECT(vw_worker_client_send_model_ctrl(client, VW_MODEL_ACTION_DOWNLOAD, "tiny"));

  // Test 3: Start session and wait for STARTED confirmation
  EXPECT(vw_worker_client_start_session(client, 1000, "ggml-tiny.en.bin", NULL));

  // Test 4: Transport receive timeout — the socket is idle between frames (the
  // server waits for AUDIO), so a short probe must time out, exercising the
  // VW_IPC_RECV_TIMEOUT path of vw_ipc_receive_timeout.
  uint8_t probe[1];
  EXPECT(vw_ipc_receive_timeout((vw_ipc_handle_t*)client->pipe_handle, probe, sizeof(probe), 1000) ==
         VW_IPC_RECV_TIMEOUT);

  // Test 5: Frame and send PCM audio chunk
  uint8_t pcm_data[320] = {0};  // 10ms of 16kHz Mono S16LE audio (320 bytes)
  vw_audio_chunk_t chunk = {
      .start_pts_us = 1000,
      .duration_us = 10000,
      .sample_rate = 16000,
      .channels = 1,
      .bytes = 320,
  };
  memcpy(chunk.pcm_data, pcm_data, 320);
  EXPECT(vw_worker_client_send_audio(client, &chunk));

  // Test 6: Send PAUSE and RESUME control frames (session stays active), then STOP + SHUTDOWN
  vw_worker_client_pause_session(client);
  vw_worker_client_resume_session(client);
  vw_worker_client_stop_session(client, VW_CTRL_REASON_SEEK_DISCONTINUITY);
  vw_worker_client_shutdown(client);

  // Test 6: Verify server thread cleanly received all expected protocol frames
  void* ret_val;
  pthread_join(thread, &ret_val);
  EXPECT((int)(intptr_t)ret_val == 0);

  // Test 7: Disconnect IPC pipe and release client resources
  vw_worker_client_disconnect(client);

  // Test 8: receive_frame decodes worker frames in order, skips unknown types, times out, and EOFs.
  // Fresh endpoint; the frames server pushes PAUSE (skipped), SEGMENT, STATUS, ERROR, then closes.
#ifdef _WIN32
  const char* pipe_name2 = "\\\\.\\pipe\\test_worker_client_socket_frames";
#else
  char pipe_name2[256];
  snprintf(pipe_name2, sizeof(pipe_name2), "/tmp/vlc-whisper-worker-client-frames-%ld.sock", (long)getpid());
#endif
  pthread_t thread2;
  err = pthread_create(&thread2, NULL, vw_fake_server_frames_thread, (void*)pipe_name2);
  EXPECT(err == 0);
  vw_platform_sleep_ms(100);

  vw_worker_client_t* client2 = vw_worker_client_launch_and_connect(NULL, pipe_name2, auth_token, NULL);
  EXPECT(client2 != NULL);
  EXPECT(vw_worker_client_start_session(client2, 0, "ggml-tiny.en.bin", NULL));

  vw_worker_recv_t recv;
  memset(&recv, 0, sizeof(recv));

  // First receive skips the PAUSE frame and returns the CAPTION_SEGMENT.
  EXPECT(vw_worker_client_receive_frame(client2, 1000000, &recv) == VW_IPC_RECV_OK);
  EXPECT(recv.type == VW_MSG_CAPTION_SEGMENT);
  EXPECT(recv.segment.segment_id == 7);
  EXPECT(recv.segment.start_pts_us == 1000000);
  EXPECT(recv.segment.end_pts_us == 2000000);
  EXPECT(recv.segment.is_final);
  EXPECT(recv.segment.text_bytes == VW_MAX_TEXT_BYTES);
  EXPECT(strlen(recv.segment.text_utf8) == VW_MAX_TEXT_BYTES);
  EXPECT(memcmp(recv.segment.text_utf8 + VW_MAX_TEXT_BYTES - 3U, "\xE2\x82\xAC", 3U) == 0);
  EXPECT(recv.segment.text_utf8 == recv.text_buf);  // owned storage, not the wire buffer

  // Then STATUS.
  memset(&recv, 0, sizeof(recv));
  EXPECT(vw_worker_client_receive_frame(client2, 1000000, &recv) == VW_IPC_RECV_OK);
  EXPECT(recv.type == VW_MSG_STATUS);
  EXPECT(recv.status.queued_audio_us == 4000000);
  EXPECT(recv.status.inference_us == 300000);
  EXPECT(recv.status.dropped_audio_us == 12345);

  // Then ERROR.
  memset(&recv, 0, sizeof(recv));
  EXPECT(vw_worker_client_receive_frame(client2, 1000000, &recv) == VW_IPC_RECV_OK);
  EXPECT(recv.type == VW_MSG_ERROR);
  EXPECT(recv.error.error_code == E_BACKPRESSURE);
  EXPECT(recv.error.recoverable == 1);

  // Server closed: next receive must report the dead transport (VW_IPC_RECV_FATAL).
  EXPECT(vw_worker_client_receive_frame(client2, 1000000, &recv) == VW_IPC_RECV_FATAL);

  vw_worker_client_disconnect(client2);
  pthread_join(thread2, &ret_val);
  EXPECT((int)(intptr_t)ret_val == 0);

  // Test 9: Silent server — receive_frame with a short timeout returns 0 (no frame) and keeps
  // the transport usable. Reuse a fresh endpoint with a listener that only does the handshake.
#ifdef _WIN32
  const char* pipe_name3 = "\\\\.\\pipe\\test_worker_client_socket_silent";
#else
  char pipe_name3[256];
  snprintf(pipe_name3, sizeof(pipe_name3), "/tmp/vlc-whisper-worker-client-silent-%ld.sock", (long)getpid());
#endif
  pthread_t thread3;
  err = pthread_create(&thread3, NULL, vw_fake_server_thread, (void*)pipe_name3);
  EXPECT(err == 0);
  vw_platform_sleep_ms(100);

  vw_worker_client_t* client3 =
      vw_worker_client_launch_and_connect_ex(NULL, pipe_name3, auth_token, NULL, "auto", "ro", 4, -1, NULL, false);
  EXPECT(client3 != NULL);
  EXPECT(vw_worker_client_start_session(client3, 0, "ggml-tiny.en.bin", NULL));

  // The server waits for AUDIO (idle): a 50ms receive must time out (VW_IPC_RECV_TIMEOUT) with the
  // connection intact.
  EXPECT(vw_worker_client_receive_frame(client3, 50000, &recv) == VW_IPC_RECV_TIMEOUT);

  // Transport still usable: send audio, then stop+shutdown like the base flow.
  vw_audio_chunk_t chunk2 = {
      .start_pts_us = 2000,
      .duration_us = 10000,
      .sample_rate = 16000,
      .channels = 1,
      .bytes = 320,
  };
  memcpy(chunk2.pcm_data, pcm_data, 320);
  EXPECT(vw_worker_client_send_audio(client3, &chunk2));
  vw_worker_client_pause_session(client3);
  vw_worker_client_resume_session(client3);
  vw_worker_client_stop_session(client3, VW_CTRL_REASON_SEEK_DISCONTINUITY);
  vw_worker_client_shutdown(client3);
  vw_worker_client_disconnect(client3);
  pthread_join(thread3, &ret_val);
  EXPECT((int)(intptr_t)ret_val == 0);

  // Test 10: Corrupt header — receive_frame must report VW_IPC_RECV_FATAL (never the timeout
  // value) and drop the transport, so the sender stops instead of spinning on a dead client.
#ifdef _WIN32
  const char* pipe_name4 = "\\\\.\\pipe\\test_worker_client_socket_badheader";
#else
  char pipe_name4[256];
  snprintf(pipe_name4, sizeof(pipe_name4), "/tmp/vlc-whisper-worker-client-badheader-%ld.sock", (long)getpid());
#endif
  pthread_t thread4;
  err = pthread_create(&thread4, NULL, vw_fake_server_bad_header_thread, (void*)pipe_name4);
  EXPECT(err == 0);
  vw_platform_sleep_ms(100);

  vw_worker_client_t* client4 = vw_worker_client_launch_and_connect(NULL, pipe_name4, auth_token, NULL);
  EXPECT(client4 != NULL);
  EXPECT(vw_worker_client_start_session(client4, 0, "ggml-tiny.en.bin", NULL));

  // First receive hits the corrupt header: fatal, not timeout.
  memset(&recv, 0, sizeof(recv));
  EXPECT(vw_worker_client_receive_frame(client4, 1000000, &recv) == VW_IPC_RECV_FATAL);

  // Transport was dropped (pipe handle closed): a second call must stay fatal, not loop forever
  // returning the timeout value with a dead handle.
  EXPECT(vw_worker_client_receive_frame(client4, 1000000, &recv) == VW_IPC_RECV_FATAL);

  vw_worker_client_disconnect(client4);
  pthread_join(thread4, &ret_val);
  EXPECT((int)(intptr_t)ret_val == 0);

  return 0;
}
