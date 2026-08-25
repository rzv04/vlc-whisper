#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "vw_model_catalog.h"
#include "vw_model_download.h"
#include "vw_sha256.h"
#include "vw_test.h"

static void test_sha256_vectors(void) {
  uint8_t hash[32];
  char hex[65];

  // Empty string.
  vw_sha256((const uint8_t*)"", 0, hash);
  vw_sha256_to_hex(hash, hex);
  EXPECT_EQ_STR(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  // "abc"
  vw_sha256((const uint8_t*)"abc", 3, hash);
  vw_sha256_to_hex(hash, hex);
  EXPECT_EQ_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  // 1,000,000 x 'a' via streaming update loop (tests 64-byte block buffering).
  vw_sha256_context_t ctx;
  vw_sha256_init(&ctx);
  char chunk[1000];
  memset(chunk, 'a', sizeof(chunk));
  for (int i = 0; i < 1000; i++) {
    vw_sha256_update(&ctx, (const uint8_t*)chunk, sizeof(chunk));
  }
  vw_sha256_final(&ctx, hash);
  vw_sha256_to_hex(hash, hex);
  EXPECT_EQ_STR(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

  // Incremental "a"+"bc" should equal "abc".
  vw_sha256_init(&ctx);
  vw_sha256_update(&ctx, (const uint8_t*)"a", 1);
  vw_sha256_update(&ctx, (const uint8_t*)"bc", 2);
  vw_sha256_final(&ctx, hash);
  vw_sha256_to_hex(hash, hex);
  EXPECT_EQ_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

static void test_pct_math(void) {
  EXPECT(vw_model_download_pct(0, 100) == 0);
  EXPECT(vw_model_download_pct(50, 100) == 50);
  EXPECT(vw_model_download_pct(100, 100) == 100);
  EXPECT(vw_model_download_pct(150, 100) == 100);
  EXPECT(vw_model_download_pct(0, 0) == 0);
  EXPECT(vw_model_download_pct(77704715ULL, 77704715ULL) == 100);
  EXPECT(vw_model_download_pct(38852358ULL, 77704715ULL) == 50);
  // Saturating when .part exceeds total (should clamp).
  EXPECT(vw_model_download_pct(80000000ULL, 77704715ULL) == 100);
  EXPECT(vw_model_download_pct(UINT64_MAX, 100) == 100);
}

static void test_catalog(void) {
  EXPECT(vw_model_catalog_count() == 7);

  const vw_model_catalog_entry_t* e = vw_model_catalog_find("tiny.en");
  EXPECT(e != NULL);
  EXPECT_EQ_STR(e->id, "tiny.en");
  EXPECT_EQ_STR(e->filename, "ggml-tiny.en.bin");
  EXPECT_EQ_STR(e->sha256_hex, "921e4cf8686fdd993dcd081a5da5b6c365bfde1162e72b08d75ac75289920b1f");
  EXPECT(e->bytes == 77704715ULL);
  EXPECT(e->multilingual == false);
  EXPECT(strstr(e->url, "ggml-tiny.en.bin") != NULL);

  e = vw_model_catalog_find("tiny");
  EXPECT(e != NULL);
  EXPECT(e->bytes == 77691713ULL);
  EXPECT(e->multilingual == true);

  e = vw_model_catalog_find("base.en");
  EXPECT(e != NULL);
  EXPECT(e->bytes == 147964211ULL);
  EXPECT(e->multilingual == false);

  e = vw_model_catalog_find("base");
  EXPECT(e != NULL);
  EXPECT(e->bytes == 147951465ULL);
  EXPECT(e->multilingual == true);

  e = vw_model_catalog_find("small");
  EXPECT(e != NULL);
  EXPECT(e->bytes == 487601967ULL);

  e = vw_model_catalog_find("medium");
  EXPECT(e != NULL);
  EXPECT(e->bytes == 1533763059ULL);

  e = vw_model_catalog_find("large");
  EXPECT(e != NULL);
  EXPECT(e->bytes == 3095033483ULL);
  EXPECT(e->multilingual == true);

  // All 7 ids must be findable and have correct invariants.
  const char* ids[] = {"tiny.en", "tiny", "base.en", "base", "small", "medium", "large"};
  for (size_t i = 0; i < 7; i++) {
    const vw_model_catalog_entry_t* ent = vw_model_catalog_find(ids[i]);
    EXPECT(ent != NULL);
    EXPECT(strlen(ent->sha256_hex) == 64);
    EXPECT(ent->bytes > 0);
    EXPECT(strstr(ent->url, "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/") != NULL);
    EXPECT(strstr(ent->url, ent->filename) != NULL);
  }

  // Miss cases.
  EXPECT(vw_model_catalog_find("unknown") == NULL);
  EXPECT(vw_model_catalog_find("") == NULL);
  EXPECT(vw_model_catalog_find(NULL) == NULL);
  EXPECT(vw_model_catalog_find("TINY") == NULL);
}

static void test_default_dir(void) {
  char out[4096];
  EXPECT(vw_model_download_default_dir(out, sizeof(out)) == true);
  EXPECT(strstr(out, "vlc-whisper") != NULL);
  EXPECT(strstr(out, "models") != NULL);
  struct stat st;
  EXPECT(stat(out, &st) == 0);
  EXPECT(S_ISDIR(st.st_mode));

  // Null and zero-size should fail gracefully.
  EXPECT(vw_model_download_default_dir(NULL, 0) == false);
  EXPECT(vw_model_download_default_dir(out, 0) == false);

  // Tiny buffer should still null-terminate without overflow (still creates dir).
  char tiny[8];
  // This may truncate but should not crash; result depends on implementation.
  // Just ensure it doesn't overflow: if buffer too small, function may still succeed
  // or fail; we only check it doesn't crash.
  (void)vw_model_download_default_dir(tiny, sizeof(tiny));
}

static void test_poll_and_lifecycle(void) {
  vw_download_progress_t prog;
  EXPECT(vw_model_download_poll(NULL, &prog) == false);
  vw_model_download_abort(NULL);
  vw_model_download_free(NULL);

  const vw_model_catalog_entry_t* e = vw_model_catalog_find("tiny");
  EXPECT(e != NULL);
  // Start with NULL entry or dir should return NULL without network.
  EXPECT(vw_model_download_start(NULL, "/tmp") == NULL);
  EXPECT(vw_model_download_start(e, NULL) == NULL);
  EXPECT(vw_model_download_start(e, "") == NULL);

  // Poll with null out should fail.
  // Create a temp dir for a smoke start that we immediately abort to avoid network.
  char tmpl[] = "/tmp/vw_test_dl_XXXXXX";
  char* tmpdir = mkdtemp(tmpl);
  EXPECT(tmpdir != NULL);
  // Use a short timeout: start then immediately abort and free; no network assertion.
  vw_model_download_t* dl = vw_model_download_start(e, tmpdir);
  if (dl) {
    EXPECT(vw_model_download_poll(dl, NULL) == false);
    EXPECT(vw_model_download_poll(dl, &prog) == true);
    // model_id should be NUL-terminated copy of entry id.
    EXPECT_EQ_STR(prog.model_id, "tiny");
    vw_model_download_abort(dl);
    // Poll after abort should reflect ABORTING or IDLE.
    // Give thread a moment to observe abort.
    do {
      struct timespec ts = {0, 200000000L};
      nanosleep(&ts, NULL);
    } while (0);
    EXPECT(vw_model_download_poll(dl, &prog) == true);
    vw_model_download_free(dl);
    // Ensure no leaked .part remains (cleanup helper also).
    vw_model_download_cleanup_partial(tmpdir);
  }
  // Cleanup temp dir.
  rmdir(tmpdir);
}

static void test_cleanup_partial(void) {
  char tmpl[] = "/tmp/vw_test_cleanup_XXXXXX";
  char* tmpdir = mkdtemp(tmpl);
  EXPECT(tmpdir != NULL);

  char part1[4096], part2[4096], keep[4096];
  snprintf(part1, sizeof(part1), "%s/a.part", tmpdir);
  snprintf(part2, sizeof(part2), "%s/b.part", tmpdir);
  snprintf(keep, sizeof(keep), "%s/keep.bin", tmpdir);
  FILE* f = fopen(part1, "wb");
  EXPECT(f != NULL);
  fwrite("x", 1, 1, f);
  fclose(f);
  f = fopen(part2, "wb");
  EXPECT(f != NULL);
  fwrite("y", 1, 1, f);
  fclose(f);
  f = fopen(keep, "wb");
  EXPECT(f != NULL);
  fwrite("z", 1, 1, f);
  fclose(f);

  vw_model_download_cleanup_partial(tmpdir);
  struct stat st;
  EXPECT(stat(part1, &st) != 0);
  EXPECT(stat(part2, &st) != 0);
  EXPECT(stat(keep, &st) == 0);

  // Null and non-existent dir should not crash.
  vw_model_download_cleanup_partial(NULL);
  vw_model_download_cleanup_partial("/nonexistent/path/xyz");

  unlink(keep);
  rmdir(tmpdir);
}

int main(void) {
  test_sha256_vectors();
  test_pct_math();
  test_catalog();
  test_default_dir();
  test_poll_and_lifecycle();
  test_cleanup_partial();
  printf("test_model_download: all checks passed\n");
  return 0;
}
