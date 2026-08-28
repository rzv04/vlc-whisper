#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_translate.h"

static void test_url_encode(void) {
  char buf[256];

  // Test empty string
  bool ok = vw_url_encode("", buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "") == 0);

  // Test simple alphanumeric
  ok = vw_url_encode("HelloWorld123", buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "HelloWorld123") == 0);

  // Test space encoding
  ok = vw_url_encode("hello world", buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "hello%20world") == 0);

  // Test special punctuation
  ok = vw_url_encode("foo&bar=baz?q=1+2", buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "foo%26bar%3Dbaz%3Fq%3D1%2B2") == 0);

  // Test buffer overflow protection
  char small_buf[5];
  ok = vw_url_encode("hello world", small_buf, sizeof(small_buf));
  assert(!ok);
}

static void test_html_unescape(void) {
  char buf[256];

  // Plain text
  bool ok = vw_html_unescape("Hello World", buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "Hello World") == 0);

  // Named entities
  ok = vw_html_unescape("&quot;Hello &amp; &lt;World&gt;&#39;", buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "\"Hello & <World>'") == 0);

  // HTML tag stripping
  ok = vw_html_unescape("<div class=\"result\">Salut <b>lume</b>!</div>", buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "Salut lume!") == 0);

  // Decimal and hex numeric entities
  ok = vw_html_unescape("&#72;&#101;&#108;&#108;&#111; &#x57;&#x6f;&#x72;&#x6c;&#x64;", buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "Hello World") == 0);
}

static void test_parse_rpc_response(void) {
  char buf[256];

  // Realistic Google Web RPC (MkEWBc) response
  const char* rpc_resp =
      ")]}'\n\n[[\"wrb.fr\",\"MkEWBc\",\"[[[\\\"Salut lume\\\",null,null,null,1]]\\n]\",null,null,null,\"generic\"]]\n";
  bool ok = vw_translate_parse_rpc_response(rpc_resp, buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "Salut lume") == 0);

  // Invalid payload
  ok = vw_translate_parse_rpc_response("null", buf, sizeof(buf));
  assert(!ok);

  ok = vw_translate_parse_rpc_response("[[\"other_rpc\",\"payload\"]]", buf, sizeof(buf));
  assert(!ok);
}

static void test_parse_gtx_response(void) {
  char buf[256];

  // Single-chunk GTX response
  const char* gtx_single = "[[[\"Salut lume\",\"Hello world\",null,null,1]],null,\"en\"]";
  bool ok = vw_translate_parse_gtx_response(gtx_single, buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "Salut lume") == 0);

  // Multi-chunk GTX response
  const char* gtx_multi =
      "[[[\"Salut lume \",\"Hello world \",null,null,1],[\"cum merge?\",\"how is it "
      "going?\",null,null,1]],null,\"en\"]";
  ok = vw_translate_parse_gtx_response(gtx_multi, buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "Salut lume cum merge?") == 0);

  // Invalid GTX response
  ok = vw_translate_parse_gtx_response("[]", buf, sizeof(buf));
  assert(!ok);

  ok = vw_translate_parse_gtx_response("error", buf, sizeof(buf));
  assert(!ok);
}

static void test_parse_mobile_response(void) {
  char buf[256];

  // Mobile scrape HTML with result-container
  const char* html_resp =
      "<!DOCTYPE html><html><body><div class=\"main\"><div class=\"result-container\">Salut lume &amp; "
      "prieteni</div></div></body></html>";
  bool ok = vw_translate_parse_mobile_response(html_resp, buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "Salut lume & prieteni") == 0);

  // HTML missing result container
  const char* bad_html = "<html><body><div>No result here</div></body></html>";
  ok = vw_translate_parse_mobile_response(bad_html, buf, sizeof(buf));
  assert(!ok);
}

static void test_tier_constants(void) {
  assert(VW_TRANSLATE_TIER_NONE == 0);
  assert(VW_TRANSLATE_TIER_WEB_RPC == 1);
  assert(VW_TRANSLATE_TIER_GTX == 2);
  assert(VW_TRANSLATE_TIER_MOBILE_SCRAPE == 3);
  assert(VW_TRANSLATE_MAX_TEXT_BYTES == 1024);
  assert(VW_TRANSLATE_TIMEOUT_MS == 800);
}

int main(void) {
  printf("Running test_url_encode...\n");
  test_url_encode();

  printf("Running test_html_unescape...\n");
  test_html_unescape();

  printf("Running test_parse_rpc_response...\n");
  test_parse_rpc_response();

  printf("Running test_parse_gtx_response...\n");
  test_parse_gtx_response();

  printf("Running test_parse_mobile_response...\n");
  test_parse_mobile_response();

  printf("Running test_tier_constants...\n");
  test_tier_constants();

  printf("All translate unit tests PASSED.\n");
  return 0;
}
