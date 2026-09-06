#include "vw_test.h"
#include "vw_test_worker_harness.h"
#include "vw_test_worker_stubs.h"

int main(void) {
  vw_test_worker_stubs_reset();

  vw_test_worker_fixture_t fixture;
  bool started = vw_test_worker_fixture_start(&fixture, "live-tail");
  vw_test_check_true("stub worker starts", started);
  if (started) {
    vw_test_check_true("live session starts", vw_worker_client_start_session(fixture.client, 0, "tiny", NULL));
    vw_test_check_true("sub-threshold live audio is queued", vw_test_send_audio_chunks(fixture.client, 2, 0));
    vw_platform_sleep_ms(75);
    vw_test_check_true("audio remains below progressive startup threshold before media end",
                       vw_test_whisper_transcribe_calls() == 0);

    vw_worker_client_stop_session(fixture.client, VW_CTRL_REASON_MEDIA_END);
    vw_platform_sleep_ms(100);
    vw_test_check_true("MEDIA_END flushes held-back residual speech before teardown",
                       vw_test_whisper_transcribe_calls() > 0);
  }
  if (fixture.thread_started) {
    vw_test_check_true("stub worker shuts down cleanly", vw_test_worker_fixture_shutdown(&fixture) == 0);
  }
  return vw_test_finish("test_live_media_end_flushes_tail");
}
