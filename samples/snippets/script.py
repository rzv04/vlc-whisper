#!/usr/bin/env python3
"""Small keyless Google Translate web-client prototype.

This uses undocumented web endpoints, not Google's supported Translation API.
They may change, rate-limit, or block requests at any time.
"""

import argparse
import html
import json
import re
import sys
from urllib.parse import urlencode
from urllib.request import Request, urlopen
from urllib.error import HTTPError, URLError

USER_AGENT = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/124 Safari/537.36"
TIMEOUT = 10

LANGUAGES = {
    "en": "en", "eng": "en", "english": "en",
    "ro": "ro", "ron": "ro", "rum": "ro", "romanian": "ro", "romana": "ro", "română": "ro",
    "auto": "auto", "automatic": "auto", "detect": "auto",
}


class TranslationError(RuntimeError):
    pass


def language_code(value: str) -> str:
    key = value.strip().lower()
    if key in LANGUAGES:
        return LANGUAGES[key]
    if re.fullmatch(r"[a-z]{2,8}(?:-[a-z]{2,4})?", key):
        return key.split("-", 1)[0]
    raise argparse.ArgumentTypeError("unsupported language; use en, English, ro, Romanian, or a language code")


def request(url: str, data: bytes | None = None, content_type: str | None = None) -> str:
    headers = {"User-Agent": USER_AGENT, "Accept": "*/*"}
    if content_type:
        headers["Content-Type"] = content_type
    try:
        with urlopen(Request(url, data=data, headers=headers), timeout=TIMEOUT) as response:
            return response.read().decode("utf-8", errors="replace")
    except HTTPError as exc:
        raise TranslationError(f"HTTP {exc.code} from translation service") from exc
    except (URLError, TimeoutError, OSError) as exc:
        raise TranslationError(f"network error: {exc.reason if isinstance(exc, URLError) else exc}") from exc


def gtx_translate(text: str, source: str, target: str) -> str:
    query = urlencode({"client": "gtx", "sl": source, "tl": target, "dt": "t", "q": text})
    raw = request("https://translate.googleapis.com/translate_a/single?" + query)
    try:
        parts = json.loads(raw)[0]
        result = "".join(item[0] for item in parts if item and item[0])
    except (ValueError, TypeError, IndexError, KeyError) as exc:
        raise TranslationError("unexpected response from gtx endpoint") from exc
    if not result:
        raise TranslationError("gtx endpoint returned no translation")
    return result


def rpc_translate(text: str, source: str, target: str) -> str:
    # MkEWBc is the internal RPC used by translate.google.com.  Its format is
    # intentionally kept isolated because this is an unsupported web protocol.
    # The second RPC field is itself a JSON string.  The [null] member is part
    # of Google's web-client request shape and is required by this endpoint.
    inner = json.dumps([[text, source, target, True], [None]], ensure_ascii=False, separators=(",", ":"))
    rpc_call = ["MkEWBc", inner, None, "generic"]
    body = urlencode({"f.req": json.dumps([[rpc_call]], ensure_ascii=False, separators=(",", ":"))}).encode()
    url = "https://translate.google.com/_/TranslateWebserverUi/data/batchexecute?" + urlencode({
        "rpcids": "MkEWBc", "bl": "boq_translate-webserver_20221005.09_p0",
        "soc-app": "1", "soc-platform": "1", "soc-device": "1", "rt": "c",
    })
    raw = request(url, body, "application/x-www-form-urlencoded;charset=UTF-8")
    # Responses contain escaped JSON inside a length-prefixed batched envelope.
    # Locate the RPC result string and decode it separately, as the outer
    # response is not necessarily valid JSON from its first character.
    marker = '"wrb.fr"'
    marker_pos = raw.find(marker)
    if marker_pos >= 0:
        quoted_result = raw.find('"', raw.find(',', marker_pos) + 1)
        quoted_result = raw.find('"', raw.find(',', quoted_result) + 1) if quoted_result >= 0 else -1
        if quoted_result >= 0:
            decoder = json.JSONDecoder()
            try:
                result_string, _ = decoder.raw_decode(raw[quoted_result:])
                decoded = json.loads(result_string)
                found = rpc_result_text(decoded) or find_translation(decoded)
                if found:
                    return found
            except (ValueError, TypeError):
                pass
    raise TranslationError("unexpected response from web RPC")


def mobile_page_translate(text: str, source: str, target: str) -> str:
    """Last-resort parser for Google Translate's lightweight mobile page."""
    query = urlencode({"sl": source, "tl": target, "q": text})
    raw = request("https://translate.google.com/m?" + query)
    match = re.search(
        r'<div\s+class=["\']result-container["\'][^>]*>(.*?)</div>',
        raw,
        flags=re.IGNORECASE | re.DOTALL,
    )
    if not match:
        raise TranslationError("mobile page contained no result-container")
    # The current result is plain text, but remove markup defensively if the
    # page adds formatting around it.
    result = re.sub(r"<[^>]+>", "", match.group(1))
    result = html.unescape(result).strip()
    if not result:
        raise TranslationError("mobile page returned an empty translation")
    return result


def rpc_result_text(value):
    """Extract the known MkEWBc result shape before using generic traversal."""
    try:
        segments = value[1][0][0][5]
        if isinstance(segments, list):
            text = "".join(item[0] for item in segments if isinstance(item, list) and item and isinstance(item[0], str))
            return text or None
    except (IndexError, KeyError, TypeError):
        return None
    return None


def find_translation(value):
    if isinstance(value, str) and value.strip():
        return value if value.strip() != "MkEWBc" else None
    if isinstance(value, list):
        # Translation segments are normally nested under result[1][0][0][5].
        if len(value) > 5 and isinstance(value[5], list):
            segments = [x[0] for x in value[5] if isinstance(x, list) and x and isinstance(x[0], str)]
            if segments:
                return "".join(segments)
        for item in value:
            found = find_translation(item)
            if found:
                return found
    return None


def translate(text: str, source: str, target: str) -> tuple[str, str]:
    errors = []
    # This is the same order used by the PotPlayer script: web RPC, old GTX
    # endpoint, then mobile-page scraping.
    for name, method in (
        ("web RPC", rpc_translate),
        ("gtx", gtx_translate),
        ("mobile scrape", mobile_page_translate),
    ):
        try:
            return method(text, source, target), name
        except TranslationError as exc:
            errors.append(f"{name}: {exc}")
    raise TranslationError("; ".join(errors))


def main() -> int:
    parser = argparse.ArgumentParser(description="Translate phrases via unofficial Google web endpoints.")
    parser.add_argument("--from-language", required=True, type=language_code, metavar="FROM")
    parser.add_argument("--to-language", required=True, type=language_code, metavar="TO")
    args = parser.parse_args()
    if args.to_language == "auto":
        parser.error("--to-language cannot be auto")
    print(f"Translating {args.from_language} -> {args.to_language}. Enter a phrase; Ctrl-C or Ctrl-D exits.")
    while True:
        try:
            text = input("> ")
        except (EOFError, KeyboardInterrupt):
            print("\nBye.")
            return 0
        if not text.strip():
            continue
        try:
            result, method = translate(text, args.from_language, args.to_language)
            print(f"[{method}] {result}")
        except TranslationError as exc:
            print(f"Error: translation failed: {exc}", file=sys.stderr)
    

if __name__ == "__main__":
    raise SystemExit(main())
