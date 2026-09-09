#include "vw_test.h"
#include "vw_test_worker_harness.h"
#include "vw_test_worker_stubs.h"

int main(void) {
  vw_test_worker_stubs_reset();

  vw_test_worker_fixture_t fixture;
  bool started = vw_test_worker_fixture_start(&fixture, "session-reset");
  vw_test_check_true("stub worker starts", started);
  if (started) {
    vw_test_check_true("first live session starts", vw_worker_client_start_session(fixture.client, 0, "tiny", NULL));
    vw_worker_client_pause_session(fixture.client);
    vw_platform_sleep_ms(50);

    vw_test_check_true("fresh session epoch starts without requiring RESUME",
                       vw_worker_client_start_session(fixture.client, 2000000, "tiny", NULL));
    vw_test_check_true("fresh session accepts enough audio to cross startup threshold",
                       vw_test_send_audio_chunks(fixture.client, 4, 2000000));
    vw_platform_sleep_ms(150);
    vw_test_check_true("fresh START resets stale pause/session state and reaches inference",
                       vw_test_whisper_transcribe_calls() > 0);

    if (fixture.client->session_active) vw_worker_client_stop_session(fixture.client, VW_CTRL_REASON_USER_STOP);
  }
  if (fixture.thread_started) {
    vw_test_check_true("stub worker shuts down cleanly", vw_test_worker_fixture_shutdown(&fixture) == 0);
  }
  return vw_test_finish("test_new_session_resets_all_session_state");
}
