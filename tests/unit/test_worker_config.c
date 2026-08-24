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
    EXPECT(cfg.backend == VW_WORKER_BACKEND_AUTO);
    EXPECT(cfg.gpu_device == 0);
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

  // --- success: --log-file override; empty default when flag absent ---
  {
    vw_worker_config_t cfg;
    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_log[] = {"vlc-whisper-worker", "--log-file", "/var/log/vw.log", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 3, argv_log) == 0);
    EXPECT_EQ_STR(cfg.log_file, "/var/log/vw.log");
  }
  {
    vw_worker_config_t cfg;
    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_none[] = {"vlc-whisper-worker", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 1, argv_none) == 0);
    EXPECT(cfg.log_file[0] == '\0');  // empty => default temp-dir log
  }

  // --- success: --backend and --gpu-device parsing (step 17a) ---
  {
    vw_worker_config_t cfg;
    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_gpu[] = {"vlc-whisper-worker", "--backend", "gpu", "--gpu-device", "1", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 5, argv_gpu) == 0);
    EXPECT(cfg.backend == VW_WORKER_BACKEND_GPU);
    EXPECT(cfg.gpu_device == 1);

    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_cpu[] = {"vlc-whisper-worker", "--backend", "cpu", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 3, argv_cpu) == 0);
    EXPECT(cfg.backend == VW_WORKER_BACKEND_CPU);
    EXPECT(cfg.gpu_device == 0);  // untouched

    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_auto[] = {"vlc-whisper-worker", "--backend", "auto", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 3, argv_auto) == 0);
    EXPECT(cfg.backend == VW_WORKER_BACKEND_AUTO);
  }

  // --- failure: bad --backend value, negative --gpu-device, dangling flags ---
  {
    vw_worker_config_t cfg;
    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_bad_backend[] = {"vlc-whisper-worker", "--backend", "cuda", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 3, argv_bad_backend) == 2);

    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_neg_device[] = {"vlc-whisper-worker", "--gpu-device", "-1", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 3, argv_neg_device) == 2);

    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_dangling[] = {"vlc-whisper-worker", "--backend", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 2, argv_dangling) == 2);

    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_bad_device[] = {"vlc-whisper-worker", "--gpu-device", "abc", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 3, argv_bad_device) == 2);
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

  // --- failure: dangling options with no value ---
  {
    vw_worker_config_t cfg;
    vw_worker_config_init_defaults(&cfg);
    char* argv_dangling_tok[] = {"vlc-whisper-worker", "--pipe", "/tmp/vw.sock", "--token", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 4, argv_dangling_tok) == 2);

    char* argv_dangling_pipe[] = {"vlc-whisper-worker", "--pipe", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 2, argv_dangling_pipe) == 2);

    char* argv_dangling_model[] = {"vlc-whisper-worker", "--model", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 2, argv_dangling_model) == 2);

    char* argv_dangling_log[] = {"vlc-whisper-worker", "--log-file", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 2, argv_dangling_log) == 2);

    char* argv_dangling_gpu[] = {"vlc-whisper-worker", "--gpu-device", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 2, argv_dangling_gpu) == 2);

    char* argv_dangling_vad[] = {"vlc-whisper-worker", "--vad-model", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 2, argv_dangling_vad) == 2);
  }

  // --- success: --vad-model override ---
  {
    vw_worker_config_t cfg;
    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_vad[] = {"vlc-whisper-worker", "--vad-model", "models/custom-vad.bin", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 3, argv_vad) == 0);
    EXPECT_EQ_STR(cfg.vad_model_path, "models/custom-vad.bin");
  }

  // --- success: --language and --n-threads parse (step 19b) ---
  {
    vw_worker_config_t cfg;
    EXPECT(vw_worker_config_init_defaults(&cfg));
    EXPECT_EQ_STR(cfg.language, "en");
    EXPECT(cfg.n_threads == 4);
    char* argv_lang[] = {"vlc-whisper-worker", "--language", "ro", "--n-threads", "8", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 5, argv_lang) == 0);
    EXPECT_EQ_STR(cfg.language, "ro");
    EXPECT(cfg.n_threads == 8);
  }
  {
    vw_worker_config_t cfg;
    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_clamp_low[] = {"vlc-whisper-worker", "--n-threads", "0", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 3, argv_clamp_low) == 0);
    EXPECT(cfg.n_threads == 1);
    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_clamp_high[] = {"vlc-whisper-worker", "--n-threads", "32", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 3, argv_clamp_high) == 0);
    EXPECT(cfg.n_threads == 16);
    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_clamp_mid[] = {"vlc-whisper-worker", "--n-threads", "16", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 3, argv_clamp_mid) == 0);
    EXPECT(cfg.n_threads == 16);
  }
  // --- failure: --language auto rejected, empty, too long, dangling ---
  {
    vw_worker_config_t cfg;
    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_auto[] = {"vlc-whisper-worker", "--language", "auto", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 3, argv_auto) == 2);
    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_long[] = {"vlc-whisper-worker", "--language", "this_is_way_too_long_for_lang", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 3, argv_long) == 2);
    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_dangling_lang[] = {"vlc-whisper-worker", "--language", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 2, argv_dangling_lang) == 2);
    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_dangling_thr[] = {"vlc-whisper-worker", "--n-threads", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 2, argv_dangling_thr) == 2);
    EXPECT(vw_worker_config_init_defaults(&cfg));
    char* argv_bad_thr[] = {"vlc-whisper-worker", "--n-threads", "abc", NULL};
    EXPECT(vw_worker_config_parse_args(&cfg, 3, argv_bad_thr) == 2);
  }

  // --- failure: NULL config ---
  EXPECT(vw_worker_config_parse_args(NULL, 1, NULL) == 2);

  return 0;
}
