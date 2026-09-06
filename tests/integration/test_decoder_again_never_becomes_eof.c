#include "vw_test.h"
#include "vw_test_worker_harness.h"
#include "vw_test_worker_stubs.h"

int main(void) {
  vw_test_worker_stubs_reset();
  vw_test_decoder_set_mode(VW_TEST_DECODER_AGAIN_THEN_DATA);

  vw_test_worker_fixture_t fixture;
  bool started = vw_test_worker_fixture_start(&fixture, "decoder-again");
  vw_test_check_true("stub worker starts", started);
  if (started) {
    vw_test_check_true("source session starts",
                       vw_worker_client_start_session(fixture.client, 0, "tiny", "file:///vw-test-again.wav"));
    vw_platform_sleep_ms(150);
    vw_test_check_true("five transient no-progress reads do not become EOF before later data arrives",
                       vw_test_decoder_read_calls() >= 6);
    if (fixture.client->session_active) vw_worker_client_stop_session(fixture.client, VW_CTRL_REASON_USER_STOP);
  }
  if (fixture.thread_started) {
    vw_test_check_true("stub worker shuts down cleanly", vw_test_worker_fixture_shutdown(&fixture) == 0);
  }
  return vw_test_finish("test_decoder_again_never_becomes_eof");
}
