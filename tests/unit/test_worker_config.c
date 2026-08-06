// Unit tests for worker CLI config parsing (vw_worker_config_parse_args).
// Covers the --token/--pipe/--model success paths and the worker argv startup
// failure paths: malformed --token (bad length / non-hex), unknown option,
// dangling flag with no value, and NULL config (all map to exit code 2).
#include <string.h>

#include "vw_test.h"
#include "vw_worker_config.h"

// 64 hex chars decoding to bytes 0x00, 0x01, ..., 0x1f
static char kTokenHex[] = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

int main(void) {
  uint8_t zeros[VW_AUTH_TOKEN_BYTES] = {0};

  // --- success: no args keeps defaults and leaves token zeroed ---
  {
    vw_worker_config_t cfg;
    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_none[] = {"vlc-whisper-worker", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 1, argv_none) == 0);
    EXPECT_EQ_STR(cfg.model_path, "models/ggml-tiny.en.bin");
    EXPECT_EQ_STR(cfg.language, "en");
    EXPECT(cfg.sample_rate == 16000u);
    EXPECT(cfg.pipe_name[0] == '\0');
    EXPECT(memcmp(cfg.auth_token, zeros, VW_AUTH_TOKEN_BYTES) == 0);
  }

  // --- success: valid --token, --pipe, --model ---
  {
    vw_worker_config_t cfg;
    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_ok[] = {"vlc-whisper-worker",   "--pipe", "/tmp/vw.sock", "--token", kTokenHex, "--model",
                       "models/ggml-base.bin", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 7, argv_ok) == 0);
    EXPECT_EQ_STR(cfg.pipe_name, "/tmp/vw.sock");
    EXPECT_EQ_STR(cfg.model_path, "models/ggml-base.bin");
    for (size_t i = 0; i < VW_AUTH_TOKEN_BYTES; i++) {
      EXPECT(cfg.auth_token[i] == (uint8_t)i);
    }
  }

  // --- failure: --token too short ---
  {
    vw_worker_config_t cfg;
    vw_worker_config_init_defaults(&cfg);
    char* argv_short[] = {"vlc-whisper-worker", "--token", "aabbccdd", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 3, argv_short) == 2);
  }

  // --- failure: --token with non-hex digit ---
  {
    vw_worker_config_t cfg;
    vw_worker_config_init_defaults(&cfg);
    char bad_hex[65];
    memset(bad_hex, '0', 64);
    bad_hex[10] = 'g';  // invalid hex digit
    bad_hex[64] = '\0';
    char* argv_badhex[] = {"vlc-whisper-worker", "--token", bad_hex, NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 3, argv_badhex) == 2);
  }

  // --- failure: unknown option ---
  {
    vw_worker_config_t cfg;
    vw_worker_config_init_defaults(&cfg);
    char* argv_unknown[] = {"vlc-whisper-worker", "--bogus", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 2, argv_unknown) == 2);
  }

  // --- failure: dangling --token with no value ---
  {
    vw_worker_config_t cfg;
    vw_worker_config_init_defaults(&cfg);
    char* argv_dangling[] = {"vlc-whisper-worker", "--pipe", "/tmp/vw.sock", "--token", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 4, argv_dangling) == 2);
  }

  // --- failure: NULL config ---
  EXPECT(vw_worker_config_parse_args(NULL, 1, NULL) == 2);

  return 0;
}
