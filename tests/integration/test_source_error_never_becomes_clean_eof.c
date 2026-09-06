#include "vw_test.h"
#include "vw_test_worker_harness.h"
#include "vw_test_worker_stubs.h"

int main(void) {
  vw_test_worker_stubs_reset();
  vw_test_decoder_set_mode(VW_TEST_DECODER_ERROR);

  vw_test_worker_fixture_t fixture;
  bool started = vw_test_worker_fixture_start(&fixture, "decoder-error");
  vw_test_check_true("stub worker starts", started);
  if (started) {
    vw_test_check_true("source session starts",
                       vw_worker_client_start_session(fixture.client, 0, "tiny", "file:///vw-test-error.wav"));

    bool saw_error = false;
    for (int i = 0; i < 8 && !saw_error; i++) {
      vw_worker_recv_t recv;
      int status = vw_worker_client_receive_frame(fixture.client, 50000, &recv);
      if (status == VW_IPC_RECV_OK && recv.type == VW_MSG_ERROR) saw_error = true;
      if (status == VW_IPC_RECV_FATAL) break;
    }
    vw_test_check_true("decoder I/O failure is surfaced as an error instead of clean EOF", saw_error);
    if (fixture.client->session_active) vw_worker_client_stop_session(fixture.client, VW_CTRL_REASON_USER_STOP);
  }
  if (fixture.thread_started) {
    vw_test_check_true("stub worker shuts down cleanly", vw_test_worker_fixture_shutdown(&fixture) == 0);
  }
  return vw_test_finish("test_source_error_never_becomes_clean_eof");
}
