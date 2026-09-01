#include "vw_protocol.h"
#include "vw_test.h"

int main(void) {
  uint8_t buffer[2048];
  size_t written = 0;

  // Header
  vw_frame_header_t header = {.magic = VW_PROTOCOL_MAGIC,
                              .major = VW_PROTOCOL_VERSION_MAJOR,
                              .type = VW_MSG_HELLO,
                              .payload_length = 64,
                              .sequence = 1};
  EXPECT(vw_protocol_encode_header(&header, buffer, sizeof(buffer)));
  vw_frame_header_t decoded = {0};
  EXPECT(vw_protocol_decode_header(buffer, sizeof(buffer), &decoded));
  EXPECT(decoded.magic == VW_PROTOCOL_MAGIC);
  EXPECT(decoded.type == VW_MSG_HELLO);
  EXPECT(decoded.sequence == 1);

  // HELLO
  vw_msg_hello_t hello = {.min_major = 1, .max_major = 1, .client_version_length = 6, .client_version = (char*)"1.0.0"};
  memset(hello.auth_token, 0xAB, VW_AUTH_TOKEN_BYTES);
  EXPECT(vw_protocol_encode_payload(VW_MSG_HELLO, &hello, buffer, sizeof(buffer), &written));
  vw_msg_hello_t decoded_hello = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_HELLO, buffer, written, &decoded_hello));
  EXPECT(decoded_hello.min_major == 1);
  EXPECT(decoded_hello.auth_token[0] == 0xAB);
  EXPECT_EQ_STR(decoded_hello.client_version, "1.0.0");

  // HELLO_ACK
  vw_msg_hello_ack_t hello_ack = {.selected_major = 1,
                                  .selected_minor = 0,
                                  .capability_flags = 3,
                                  .worker_version_length = 6,
                                  .worker_version = (char*)"1.0.0"};
  EXPECT(vw_protocol_encode_payload(VW_MSG_HELLO_ACK, &hello_ack, buffer, sizeof(buffer), &written));
  vw_msg_hello_ack_t decoded_ack = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_HELLO_ACK, buffer, written, &decoded_ack));
  EXPECT(decoded_ack.capability_flags == 3);
  EXPECT_EQ_STR(decoded_ack.worker_version, "1.0.0");

  // START
  vw_msg_start_t start = {
      .timeline_origin_pts_us = 1000, .sample_rate = 16000, .channels = 1, .sample_format = 1, .source_kind = 1};
  memset(start.session_id.bytes, 1, VW_SESSION_ID_BYTES);
  strcpy(start.model_id, "ggml-tiny");
  strcpy(start.language, "en");
  EXPECT(vw_protocol_encode_payload(VW_MSG_START_SESSION, &start, buffer, sizeof(buffer), &written));
  vw_msg_start_t decoded_start = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_START_SESSION, buffer, written, &decoded_start));
  EXPECT(decoded_start.sample_rate == 16000);
  EXPECT_EQ_STR(decoded_start.model_id, "ggml-tiny");

  // AUDIO PCM
  vw_msg_audio_t audio = {
      .start_pts_us = 0, .duration_us = 1000000, .pcm_bytes = 4, .pcm_data = (const uint8_t*)"\x01\x02\x03\x04"};
  EXPECT(vw_protocol_encode_payload(VW_MSG_AUDIO_PCM, &audio, buffer, sizeof(buffer), &written));
  vw_msg_audio_t decoded_audio = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_AUDIO_PCM, buffer, written, &decoded_audio));
  EXPECT(decoded_audio.duration_us == 1000000);
  EXPECT(decoded_audio.pcm_bytes == 4);
  EXPECT(decoded_audio.pcm_data[0] == 0x01);

  // CONTROL
  vw_msg_control_t control = {.reason = 42};
  memset(control.session_id.bytes, 2, VW_SESSION_ID_BYTES);
  EXPECT(vw_protocol_encode_payload(VW_MSG_PAUSE, &control, buffer, sizeof(buffer), &written));
  vw_msg_control_t decoded_control = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_PAUSE, buffer, written, &decoded_control));
  EXPECT(decoded_control.reason == 42);

  // STATUS (v1.3 60B with resolved_backend)
  vw_msg_status_t status = {.state = 1, .queued_audio_us = 500, .inference_us = 100, .dropped_audio_us = 0};
  memset(status.session_id.bytes, 0xAA, VW_SESSION_ID_BYTES);
  snprintf(status.resolved_backend, sizeof(status.resolved_backend), "gpu");
  EXPECT(vw_protocol_encode_payload(VW_MSG_STATUS, &status, buffer, sizeof(buffer), &written));
  EXPECT(written == 60);
  vw_msg_status_t decoded_status = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_STATUS, buffer, written, &decoded_status));
  EXPECT(decoded_status.inference_us == 100);
  EXPECT(decoded_status.state == 1);
  EXPECT(strcmp(decoded_status.resolved_backend, "gpu") == 0);
  // Legacy 44B frame decodes with empty backend
  vw_msg_status_t decoded_legacy = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_STATUS, buffer, 44, &decoded_legacy));
  EXPECT(decoded_legacy.inference_us == 100);
  EXPECT(decoded_legacy.resolved_backend[0] == '\0');
  // ERROR
  vw_msg_error_t err = {.error_code = 99, .recoverable = 0};
  strcpy(err.message, "Fail");
  EXPECT(vw_protocol_encode_payload(VW_MSG_ERROR, &err, buffer, sizeof(buffer), &written));
  vw_msg_error_t decoded_err = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_ERROR, buffer, written, &decoded_err));
  EXPECT(decoded_err.error_code == 99);
  EXPECT_EQ_STR(decoded_err.message, "Fail");

  // CAPTION SEGMENT
  vw_caption_segment_t seg = {.segment_id = 1,
                              .start_pts_us = 10,
                              .end_pts_us = 20,
                              .is_final = true,
                              .text_bytes = 5,
                              .text_utf8 = (char*)"text"};
  EXPECT(vw_protocol_encode_payload(VW_MSG_CAPTION_SEGMENT, &seg, buffer, sizeof(buffer), &written));
  vw_caption_segment_t decoded_seg = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_CAPTION_SEGMENT, buffer, written, &decoded_seg));
  EXPECT(decoded_seg.end_pts_us == 20);
  EXPECT(decoded_seg.is_final == true);
  EXPECT_EQ_STR(decoded_seg.text_utf8, "text");

  // START with source_url
  vw_msg_start_t start_url = {.sample_rate = 16000, .channels = 1, .source_kind = VW_SOURCE_LOCAL_FILE};
  strcpy(start_url.model_id, "tiny.en");
  strcpy(start_url.language, "en");
  strcpy(start_url.source_url, "file:///path/to/video.mp4");
  start_url.source_url_len = (uint16_t)strlen(start_url.source_url);
  EXPECT(vw_protocol_encode_payload(VW_MSG_START_SESSION, &start_url, buffer, sizeof(buffer), &written));
  vw_msg_start_t decoded_start_url = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_START_SESSION, buffer, written, &decoded_start_url));
  EXPECT(decoded_start_url.sample_rate == 16000);
  EXPECT_EQ_STR(decoded_start_url.model_id, "tiny.en");
  EXPECT_EQ_STR(decoded_start_url.source_url, "file:///path/to/video.mp4");
  EXPECT(decoded_start_url.source_url_len == strlen("file:///path/to/video.mp4"));

  // POSITION
  vw_msg_position_t pos = {
      .current_pts_us = 12345000LL, .input_time_us = 12300000LL, .playback_rate = 1.0f, .flags = VW_POSITION_FLAG_SEEK};
  EXPECT(vw_protocol_encode_payload(VW_MSG_POSITION, &pos, buffer, sizeof(buffer), &written));
  vw_msg_position_t decoded_pos = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_POSITION, buffer, written, &decoded_pos));
  EXPECT(decoded_pos.current_pts_us == 12345000LL);
  EXPECT(decoded_pos.input_time_us == 12300000LL);
  EXPECT(decoded_pos.playback_rate == 1.0f);
  EXPECT(decoded_pos.flags == VW_POSITION_FLAG_SEEK);

  // STARTED
  vw_msg_started_t started = {.source_active = VW_SOURCE_ACTIVE_ACTIVE};
  for (size_t i = 0; i < VW_SESSION_ID_BYTES; i++) started.session_id.bytes[i] = (uint8_t)(i + 1U);
  EXPECT(vw_protocol_encode_payload(VW_MSG_STARTED, &started, buffer, sizeof(buffer), &written));
  EXPECT(written == VW_MSG_STARTED_PAYLOAD_BYTES);
  vw_msg_started_t decoded_started = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_STARTED, buffer, written, &decoded_started));
  EXPECT(decoded_started.source_active == VW_SOURCE_ACTIVE_ACTIVE);
  EXPECT(memcmp(decoded_started.session_id.bytes, started.session_id.bytes, VW_SESSION_ID_BYTES) == 0);

  // Legacy v1.2-v1.5 STARTED remains decodable for negotiated older peers.
  buffer[0] = VW_SOURCE_ACTIVE_ACTIVE;
  EXPECT(vw_protocol_decode_payload(VW_MSG_STARTED, buffer, 1U, &decoded_started));
  EXPECT(decoded_started.source_active == VW_SOURCE_ACTIVE_ACTIVE);

  // SHUTDOWN
  EXPECT(vw_protocol_encode_payload(VW_MSG_SHUTDOWN, NULL, buffer, sizeof(buffer), &written));
  EXPECT(written == 0);

  // ---- v1.6 PROTOCOL VERSION ----
  _Static_assert(VW_PROTOCOL_VERSION_MINOR == 6U, "protocol v1.6 minor must be 6");
  EXPECT(VW_PROTOCOL_VERSION_MINOR == 6U);
  EXPECT(VW_MSG_MODEL_CTRL_PAYLOAD_BYTES == 49U);
  EXPECT(VW_MSG_MODEL_PROGRESS_PAYLOAD_BYTES == 66U);
  EXPECT(VW_MSG_TRANSLATE_CTRL_PAYLOAD_BYTES == 50U);

  // MODEL_CTRL golden bytes (little-endian, fixed layout: 16 session + 1 action + 32 model_id)
  {
    vw_msg_model_ctrl_t ctrl = {0};
    for (size_t i = 0; i < VW_SESSION_ID_BYTES; i++) ctrl.session_id.bytes[i] = (uint8_t)(i + 1);
    ctrl.action = VW_MODEL_ACTION_DOWNLOAD;
    memset(ctrl.model_id, 0, 32);
    memcpy(ctrl.model_id, "tiny", 4);
    EXPECT(vw_protocol_encode_payload(VW_MSG_MODEL_CTRL, &ctrl, buffer, sizeof(buffer), &written));
    EXPECT(written == VW_MSG_MODEL_CTRL_PAYLOAD_BYTES);
    uint8_t expected_ctrl[VW_MSG_MODEL_CTRL_PAYLOAD_BYTES] = {0};
    for (size_t i = 0; i < 16; i++) expected_ctrl[i] = (uint8_t)(i + 1);
    expected_ctrl[16] = VW_MODEL_ACTION_DOWNLOAD;
    memcpy(expected_ctrl + 17, "tiny", 4);
    // remaining 28 bytes are zero already
    EXPECT(memcmp(buffer, expected_ctrl, VW_MSG_MODEL_CTRL_PAYLOAD_BYTES) == 0);
    vw_msg_model_ctrl_t decoded_ctrl = {0};
    EXPECT(vw_protocol_decode_payload(VW_MSG_MODEL_CTRL, buffer, written, &decoded_ctrl));
    EXPECT(decoded_ctrl.action == VW_MODEL_ACTION_DOWNLOAD);
    EXPECT(memcmp(decoded_ctrl.session_id.bytes, ctrl.session_id.bytes, VW_SESSION_ID_BYTES) == 0);
    EXPECT(strcmp(decoded_ctrl.model_id, "tiny") == 0);
    // NUL-padding preserved
    EXPECT(decoded_ctrl.model_id[4] == '\0');
    for (size_t i = 5; i < 32; i++) EXPECT(decoded_ctrl.model_id[i] == '\0');
  }

  // MODEL_PROGRESS golden bytes (16 session +1 stage +1 pct +8 done +8 total +32 model_id)
  {
    vw_msg_model_progress_t prog = {0};
    for (size_t i = 0; i < VW_SESSION_ID_BYTES; i++) prog.session_id.bytes[i] = (uint8_t)(0x11 + i);
    prog.stage = VW_MODEL_STAGE_DOWNLOADING;
    prog.pct = 42;
    prog.bytes_done = 0x0102030405060708ULL;
    prog.bytes_total = 0x1122334455667788ULL;
    memset(prog.model_id, 0, 32);
    memcpy(prog.model_id, "medium", 6);
    EXPECT(vw_protocol_encode_payload(VW_MSG_MODEL_PROGRESS, &prog, buffer, sizeof(buffer), &written));
    EXPECT(written == VW_MSG_MODEL_PROGRESS_PAYLOAD_BYTES);
    uint8_t expected_prog[VW_MSG_MODEL_PROGRESS_PAYLOAD_BYTES] = {0};
    for (size_t i = 0; i < 16; i++) expected_prog[i] = (uint8_t)(0x11 + i);
    expected_prog[16] = VW_MODEL_STAGE_DOWNLOADING;
    expected_prog[17] = 42;
    // bytes_done LE
    expected_prog[18] = 0x08;
    expected_prog[19] = 0x07;
    expected_prog[20] = 0x06;
    expected_prog[21] = 0x05;
    expected_prog[22] = 0x04;
    expected_prog[23] = 0x03;
    expected_prog[24] = 0x02;
    expected_prog[25] = 0x01;
    // bytes_total LE
    expected_prog[26] = 0x88;
    expected_prog[27] = 0x77;
    expected_prog[28] = 0x66;
    expected_prog[29] = 0x55;
    expected_prog[30] = 0x44;
    expected_prog[31] = 0x33;
    expected_prog[32] = 0x22;
    expected_prog[33] = 0x11;
    memcpy(expected_prog + 34, "medium", 6);
    EXPECT(memcmp(buffer, expected_prog, VW_MSG_MODEL_PROGRESS_PAYLOAD_BYTES) == 0);
    vw_msg_model_progress_t decoded_prog = {0};
    EXPECT(vw_protocol_decode_payload(VW_MSG_MODEL_PROGRESS, buffer, written, &decoded_prog));
    EXPECT(decoded_prog.stage == VW_MODEL_STAGE_DOWNLOADING);
    EXPECT(decoded_prog.pct == 42);
    EXPECT(decoded_prog.bytes_done == 0x0102030405060708ULL);
    EXPECT(decoded_prog.bytes_total == 0x1122334455667788ULL);
    EXPECT(strcmp(decoded_prog.model_id, "medium") == 0);
  }

  // Roundtrip: every stage 0..5 and both actions
  for (uint8_t stage = VW_MODEL_STAGE_IDLE; stage <= VW_MODEL_STAGE_ABORTING; stage++) {
    vw_msg_model_progress_t prog = {0};
    memset(prog.session_id.bytes, 0x55, VW_SESSION_ID_BYTES);
    prog.stage = stage;
    prog.pct = (stage == VW_MODEL_STAGE_DONE) ? 100 : (uint8_t)(stage * 20);
    prog.bytes_done = (uint64_t)stage * 1000;
    prog.bytes_total = 5000;
    snprintf(prog.model_id, sizeof(prog.model_id), "stage-%u", stage);
    EXPECT(vw_protocol_encode_payload(VW_MSG_MODEL_PROGRESS, &prog, buffer, sizeof(buffer), &written));
    EXPECT(written == VW_MSG_MODEL_PROGRESS_PAYLOAD_BYTES);
    EXPECT(vw_protocol_validate_payload(VW_MSG_MODEL_PROGRESS, &prog));
    vw_msg_model_progress_t dec = {0};
    EXPECT(vw_protocol_decode_payload(VW_MSG_MODEL_PROGRESS, buffer, written, &dec));
    EXPECT(vw_protocol_validate_payload(VW_MSG_MODEL_PROGRESS, &dec));
    EXPECT(dec.stage == stage);
    EXPECT(dec.pct == prog.pct);
    EXPECT(dec.bytes_done == prog.bytes_done);
    EXPECT(dec.bytes_total == prog.bytes_total);
    EXPECT(strcmp(dec.model_id, prog.model_id) == 0);
  }
  for (uint8_t act = VW_MODEL_ACTION_DOWNLOAD; act <= VW_MODEL_ACTION_ABORT; act++) {
    vw_msg_model_ctrl_t ctrl = {0};
    memset(ctrl.session_id.bytes, 0x33, VW_SESSION_ID_BYTES);
    ctrl.action = act;
    snprintf(ctrl.model_id, sizeof(ctrl.model_id), "small");
    EXPECT(vw_protocol_encode_payload(VW_MSG_MODEL_CTRL, &ctrl, buffer, sizeof(buffer), &written));
    EXPECT(written == VW_MSG_MODEL_CTRL_PAYLOAD_BYTES);
    EXPECT(vw_protocol_validate_payload(VW_MSG_MODEL_CTRL, &ctrl));
    vw_msg_model_ctrl_t dec = {0};
    EXPECT(vw_protocol_decode_payload(VW_MSG_MODEL_CTRL, buffer, written, &dec));
    EXPECT(vw_protocol_validate_payload(VW_MSG_MODEL_CTRL, &dec));
    EXPECT(dec.action == act);
    EXPECT(strcmp(dec.model_id, "small") == 0);
  }

  // Rejection: wrong payload sizes must fail decode
  {
    vw_msg_model_ctrl_t ctrl = {0};
    memset(ctrl.session_id.bytes, 0xAA, VW_SESSION_ID_BYTES);
    ctrl.action = VW_MODEL_ACTION_DOWNLOAD;
    snprintf(ctrl.model_id, sizeof(ctrl.model_id), "base");
    EXPECT(vw_protocol_encode_payload(VW_MSG_MODEL_CTRL, &ctrl, buffer, sizeof(buffer), &written));
    EXPECT(written == 49);
    vw_msg_model_ctrl_t dec = {0};
    EXPECT(!vw_protocol_decode_payload(VW_MSG_MODEL_CTRL, buffer, 48, &dec));
    EXPECT(!vw_protocol_decode_payload(VW_MSG_MODEL_CTRL, buffer, 50, &dec));
    // also validate that truncated buffer is rejected even if we pad buffer to 50
    uint8_t padded[50] = {0};
    memcpy(padded, buffer, 49);
    EXPECT(!vw_protocol_decode_payload(VW_MSG_MODEL_CTRL, padded, 48, &dec));
    EXPECT(!vw_protocol_decode_payload(VW_MSG_MODEL_CTRL, padded, 50, &dec));
  }
  {
    vw_msg_model_progress_t prog = {0};
    memset(prog.session_id.bytes, 0xBB, VW_SESSION_ID_BYTES);
    prog.stage = VW_MODEL_STAGE_VERIFYING;
    prog.pct = 50;
    prog.bytes_done = 100;
    prog.bytes_total = 200;
    snprintf(prog.model_id, sizeof(prog.model_id), "tiny");
    EXPECT(vw_protocol_encode_payload(VW_MSG_MODEL_PROGRESS, &prog, buffer, sizeof(buffer), &written));
    EXPECT(written == 66);
    vw_msg_model_progress_t dec = {0};
    EXPECT(!vw_protocol_decode_payload(VW_MSG_MODEL_PROGRESS, buffer, 65, &dec));
    EXPECT(!vw_protocol_decode_payload(VW_MSG_MODEL_PROGRESS, buffer, 67, &dec));
    uint8_t padded[67] = {0};
    memcpy(padded, buffer, 66);
    EXPECT(!vw_protocol_decode_payload(VW_MSG_MODEL_PROGRESS, padded, 65, &dec));
    EXPECT(!vw_protocol_decode_payload(VW_MSG_MODEL_PROGRESS, padded, 67, &dec));
  }

  // Rejection: invalid enum values must fail validation
  {
    vw_msg_model_ctrl_t bad = {0};
    memset(bad.session_id.bytes, 1, VW_SESSION_ID_BYTES);
    snprintf(bad.model_id, sizeof(bad.model_id), "tiny");
    bad.action = 0;
    EXPECT(!vw_protocol_validate_payload(VW_MSG_MODEL_CTRL, &bad));
    bad.action = 3;
    EXPECT(!vw_protocol_validate_payload(VW_MSG_MODEL_CTRL, &bad));
    bad.action = 255;
    EXPECT(!vw_protocol_validate_payload(VW_MSG_MODEL_CTRL, &bad));
    // valid actions must pass
    bad.action = VW_MODEL_ACTION_DOWNLOAD;
    EXPECT(vw_protocol_validate_payload(VW_MSG_MODEL_CTRL, &bad));
    bad.action = VW_MODEL_ACTION_ABORT;
    EXPECT(vw_protocol_validate_payload(VW_MSG_MODEL_CTRL, &bad));
  }
  {
    vw_msg_model_progress_t bad = {0};
    memset(bad.session_id.bytes, 1, VW_SESSION_ID_BYTES);
    snprintf(bad.model_id, sizeof(bad.model_id), "small");
    bad.bytes_total = 1000;
    bad.stage = 6;
    bad.pct = 50;
    EXPECT(!vw_protocol_validate_payload(VW_MSG_MODEL_PROGRESS, &bad));
    bad.stage = 255;
    EXPECT(!vw_protocol_validate_payload(VW_MSG_MODEL_PROGRESS, &bad));
    bad.stage = VW_MODEL_STAGE_DONE;
    bad.pct = 101;
    EXPECT(!vw_protocol_validate_payload(VW_MSG_MODEL_PROGRESS, &bad));
    bad.pct = 255;
    EXPECT(!vw_protocol_validate_payload(VW_MSG_MODEL_PROGRESS, &bad));
    // Active progress still requires a known total.
    bad.stage = VW_MODEL_STAGE_DOWNLOADING;
    bad.pct = 50;
    bad.bytes_total = 0;
    EXPECT(!vw_protocol_validate_payload(VW_MSG_MODEL_PROGRESS, &bad));
    // Terminal failures may have no known byte total.
    bad.stage = VW_MODEL_STAGE_FAILED;
    bad.pct = 0;
    EXPECT(vw_protocol_validate_payload(VW_MSG_MODEL_PROGRESS, &bad));
    // IDLE and DONE with valid totals must pass.
    bad.stage = VW_MODEL_STAGE_IDLE;
    bad.pct = 0;
    bad.bytes_total = 0;
    EXPECT(vw_protocol_validate_payload(VW_MSG_MODEL_PROGRESS, &bad));
    bad.stage = VW_MODEL_STAGE_DONE;
    bad.pct = 100;
    bad.bytes_total = 1000;
    EXPECT(vw_protocol_validate_payload(VW_MSG_MODEL_PROGRESS, &bad));
  }

  // Defensive NUL-termination: decoder force-terminates model_id[31] even if wire has no NUL
  {
    uint8_t raw_ctrl[VW_MSG_MODEL_CTRL_PAYLOAD_BYTES] = {0};
    memset(raw_ctrl, 0x41, sizeof(raw_ctrl));  // fill with 'A', no NUL
    // keep session_id as 'A's, action 1, but last byte should be forced to NUL after decode
    raw_ctrl[16] = VW_MODEL_ACTION_DOWNLOAD;
    vw_msg_model_ctrl_t dec = {0};
    EXPECT(vw_protocol_decode_payload(VW_MSG_MODEL_CTRL, raw_ctrl, sizeof(raw_ctrl), &dec));
    EXPECT(dec.model_id[31] == '\0');
    // ensure string is NUL-terminated (strlen <32 because [31] forced NUL)
    EXPECT(strlen(dec.model_id) < 32);
    uint8_t raw_prog[VW_MSG_MODEL_PROGRESS_PAYLOAD_BYTES] = {0};
    memset(raw_prog, 0x42, sizeof(raw_prog));
    raw_prog[16] = VW_MODEL_STAGE_DOWNLOADING;
    raw_prog[17] = 50;
    vw_msg_model_progress_t decp = {0};
    EXPECT(vw_protocol_decode_payload(VW_MSG_MODEL_PROGRESS, raw_prog, sizeof(raw_prog), &decp));
    EXPECT(decp.model_id[31] == '\0');
    EXPECT(strlen(decp.model_id) < 32);
  }

  // TRANSLATE_CTRL (v1.5)
  {
    vw_msg_translate_ctrl_t tctrl = {
        .enabled = 1,
        .mode = 1,
    };
    memset(tctrl.session_id.bytes, 0x55, VW_SESSION_ID_BYTES);
    strcpy(tctrl.source_lang, "auto");
    strcpy(tctrl.target_lang, "ro");
    EXPECT(vw_protocol_encode_payload(VW_MSG_TRANSLATE_CTRL, &tctrl, buffer, sizeof(buffer), &written));
    EXPECT(written == VW_MSG_TRANSLATE_CTRL_PAYLOAD_BYTES);
    vw_msg_translate_ctrl_t decoded_tctrl = {0};
    EXPECT(vw_protocol_decode_payload(VW_MSG_TRANSLATE_CTRL, buffer, written, &decoded_tctrl));
    EXPECT(decoded_tctrl.enabled == 1);
    EXPECT(decoded_tctrl.mode == 1);
    EXPECT_EQ_STR(decoded_tctrl.source_lang, "auto");
    EXPECT_EQ_STR(decoded_tctrl.target_lang, "ro");
    EXPECT(vw_protocol_validate_payload(VW_MSG_TRANSLATE_CTRL, &decoded_tctrl));
  }

  // CAPTION SEGMENT with translation fields (v1.5)
  {
    vw_caption_segment_t tseg = {
        .segment_id = 42,
        .start_pts_us = 1000,
        .end_pts_us = 2000,
        .is_final = true,
        .text_bytes = 11,
        .text_utf8 = (char*)"Hello world",
        .translated_text_bytes = 10,
        .translated_text_utf8 = (char*)"Salut lume",
        .translation_attempted = true,
        .translation_latency_us = 250000,
        .translation_tier = 1,
    };
    memset(tseg.session_id.bytes, 0x77, VW_SESSION_ID_BYTES);
    EXPECT(vw_protocol_encode_payload(VW_MSG_CAPTION_SEGMENT, &tseg, buffer, sizeof(buffer), &written));
    vw_caption_segment_t dec_tseg = {0};
    EXPECT(vw_protocol_decode_payload(VW_MSG_CAPTION_SEGMENT, buffer, written, &dec_tseg));
    EXPECT(dec_tseg.segment_id == 42);
    EXPECT(dec_tseg.text_bytes == 11);
    EXPECT(strncmp(dec_tseg.text_utf8, "Hello world", 11) == 0);
    EXPECT(dec_tseg.translated_text_bytes == 10);
    EXPECT(strncmp(dec_tseg.translated_text_utf8, "Salut lume", 10) == 0);
    EXPECT(dec_tseg.translation_latency_us == 250000);
    EXPECT(dec_tseg.translation_tier == 1);
    EXPECT(dec_tseg.translation_attempted);
    EXPECT(vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &dec_tseg));

    // Failed attempts still carry elapsed latency so benchmark timeout/failure classification remains accurate.
    tseg.translated_text_bytes = 0;
    tseg.translated_text_utf8 = NULL;
    tseg.translation_latency_us = 800000;
    tseg.translation_tier = 0;
    EXPECT(vw_protocol_encode_payload(VW_MSG_CAPTION_SEGMENT, &tseg, buffer, sizeof(buffer), &written));
    memset(&dec_tseg, 0, sizeof(dec_tseg));
    EXPECT(vw_protocol_decode_payload(VW_MSG_CAPTION_SEGMENT, buffer, written, &dec_tseg));
    EXPECT(dec_tseg.translation_attempted);
    EXPECT(dec_tseg.translated_text_bytes == 0);
    EXPECT(dec_tseg.translation_latency_us == 800000);
    EXPECT(dec_tseg.translation_tier == 0);
    EXPECT(vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &dec_tseg));

    // Once the optional translation length appears, latency and tier are mandatory and no trailing bytes are valid.
    EXPECT(!vw_protocol_decode_payload(VW_MSG_CAPTION_SEGMENT, buffer, written - 1U, &dec_tseg));
    buffer[written] = 0;
    EXPECT(!vw_protocol_decode_payload(VW_MSG_CAPTION_SEGMENT, buffer, written + 1U, &dec_tseg));
  }

  printf("test_protocol_codec PASSED\n");
  return 0;
}
