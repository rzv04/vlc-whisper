# Task: Real-Time Subtitle Translation & Unified Settings GUI (Step 21b)

## Goal
Provide user-opt-in real-time translation of transcribed subtitles (PotPlayer parity) using an asynchronous keyless Google translation engine (with Web RPC, GTX, and Mobile scrape fallbacks), integrated translation latency benchmarking, and a unified settings UI inside the existing VLC-Whisper settings dialog, featuring an Auto Translation checkbox, Source language dropdown, Translation language dropdown, and an interactive Test translation button.

## Context
- **Relevant docs/ADR**: [`docs/plans/step21_translation_research.md`](step21_translation_research.md), `ADR-022` (Lua Settings Extension), `ADR-023` (Worker Network Carve-Out Policy), `ADR-024` (Opt-in Subtitle Translation Service), [`docs/product.md`](../product.md), [`docs/architecture.md`](../architecture.md).
- **VLC/worker/protocol version affected**: VLC 3.0.23, `vlc-whisper-worker` (v0.3.0), Protocol v1.5 minor contract bump.
- **Assumptions and explicit non-goals**:
  - **Assumptions**: Subtitle translation is strictly opt-in; when disabled (`whisper-translate-enabled = false`), zero network requests are made. When enabled, only finalized segment text is transmitted over TLS to translation endpoints.
  - **Explicit non-goals**: Cloud transcription (Whisper stays 100% local/offline); third-party telemetry; storing transcripts or credentials to disk; custom multi-window dialogs (all controls must reside in the single `vlc_whisper_settings.lua` dialog).

## Scope
- **In scope**:
  1. **Single-Dialog Settings UI Extension (`lua/extensions/vlc_whisper_settings.lua`)**:
     - **Auto Translation**: Checkbox widget (`whisper-translate-enabled`, default `false`).
     - **Source Language (From)**: Dropdown widget (`whisper-translate-from`, entries: `Auto detect`, `English`, `Romanian`, `Turkish`, `German`, `French`, `Spanish`, `Italian`, `Russian`, `Ukrainian`, `Chinese`, `Japanese`, `Korean`, `Arabic`, etc.; default `auto`).
     - **Target Language (To)**: Dropdown widget (`whisper-translate-to`, default `en` or `ro`).
     - **Screen Placement / Display Mode**: Dropdown widget (`whisper-translate-mode`: `Show translation only`, `Show source + translation (dual line)`).
     - **Interactive Test Widget**: Text input field with default test phrase (`With this feature you no longer have to wait for subtitles`) paired with a `Test` button.
     - **Test Status Label**: Dedicated UI label displaying live test translation results or failure diagnostics without blocking the VLC main thread.
  2. **Worker Keyless Translation Engine (`worker/src/vw_translate.c` & `worker/include/vw_translate.h`)**:
     - C17 implementation of the 3-tier fallback translation engine derived from [`samples/snippets/script.py`](../../samples/snippets/script.py):
       1. **Tier 1 (Primary)**: Google Translate Web RPC (`MkEWBc` batch execute payload via HTTP POST to `https://translate.google.com/_/TranslateWebserverUi/data/batchexecute`).
       2. **Tier 2 (Fallback 1)**: Legacy GTX single-query endpoint (`https://translate.googleapis.com/translate_a/single?client=gtx&sl={src}&tl={dst}&dt=t&q={text}`).
       3. **Tier 3 (Fallback 2)**: Mobile scrape endpoint (`https://translate.google.com/m?sl={src}&tl={dst}&q={text}`).
     - Platform-native HTTP/HTTPS transports:
       - **Windows**: WinHTTP client stack (`winhttp.dll` / `WinHttpOpen`, `WinHttpConnect`, `WinHttpSendRequest`), consistent with `vw_model_download.c`.
       - **Linux**: Child `curl` subprocess pipe or POSIX socket client.
     - Dedicated worker translation thread / async queue: translation requests run asynchronously and never block audio processing or Whisper inference.
  3. **Translation Latency Benchmarking Subsystem (`plugin/src/vw_benchmark.c` & `worker/src/vw_worker.c`)**:
     - Measurement of per-cue translation latency (network roundtrip + response parsing time in microseconds).
     - Tracking translation tier distribution: counts of successful requests serviced by Tier 1 (Web RPC), Tier 2 (GTX), and Tier 3 (Mobile scrape).
     - Tracking translation timeout / degradation events: counts of cues that exceeded the 800ms deadline and fell back to untranslated source text.
     - Statistical percentile calculation: min, p50, p95, max translation latency aggregated and reported in `benchmark_session_<id>.txt`.
     - Cumulative translation processing duration and throughput metrics.
  4. **Timing & Realtime Fallback Handling (`worker/src/vw_worker.c` & `plugin/src/vw_caption_presenter.c`)**:
     - Bounded latency budget ($\le 800\text{ ms}$). If a translation request times out, fails, or network is disconnected, the engine immediately falls back to displaying the untranslated Whisper source text.
     - Playback continuity invariant: translation failures never stall, stutter, or crash VLC media playback.
  5. **Protocol v1.5 Wire Contract (`protocol/include/vw_protocol_types.h` & `protocol/src/vw_protocol_codec.c`)**:
     - Extended `vw_caption_segment_t` or added `vw_msg_translated_segment_t` with `translated_text_utf8`, `translated_text_bytes`, `translation_latency_us`, and `translation_tier`.
     - Configuration message `VW_MSG_TRANSLATE_CTRL` (or extended `VW_MSG_START`) passing `translate_enabled`, `source_lang`, and `target_lang`.
  6. **Automated Unit & Contract Tests (`tests/unit/test_translate.c` & `tests/unit/test_benchmark.c`)**:
     - URL query encoding and escaping tests.
     - Web RPC JSON request generation and multi-level response unwrapping tests.
     - GTX response bracket parsing tests.
     - HTML entity decoding (`&quot;`, `&#39;`, `&amp;`, `&lt;`, `&gt;`) and `<div class="result-container">` regex extraction tests.
     - Translation latency metrics recording and percentile calculation tests in `test_benchmark.c`.
     - Timeout and graceful degradation tests.
- **Out of scope**:
  - Embedded third-party commercial API keys or proprietary billing accounts.
  - Disk caching of translated phrases.
  - Multi-dialog UI frameworks.

## Design

```
+-------------------------------------------------------------------------------+
|                           VLC Media Player 3.0.23                             |
|                                                                               |
|  +-------------------------------------------------------------------------+  |
|  |                Lua Extension: vlc_whisper_settings.lua                  |  |
|  |                                                                         |  |
|  |  Engine: [ auto (Vulkan GPU) v ]     Model: [ tiny (multilingual) v ]   |  |
|  |  Language: [ English (en)    v ]     Threads: [ 4 ]                     |  |
|  |  [x] Enable Diagnostic Logging                                          |  |
|  |  ---------------------------------------------------------------------  |  |
|  |  [x] Auto Translation                                                   |  |
|  |  Source (From):      [ Auto detect (auto)          v ]                  |  |
|  |  Translation (To):   [ Romanian (ro)               v ]                  |  |
|  |  Screen Placement:   [ Show source + translation   v ]                  |  |
|  |  Test phrase:        [ With this feature you no... ]  [ Test ]          |  |
|  |  Test result:        "Cu această funcție nu mai trebuie să..."          |  |
|  |  [ Apply Settings ]                                                     |  |
|  +-------------------------------------------------------------------------+  |
|                                     |                                         |
|                       cfg_set() / config variables                            |
|                                     v                                         |
|  +-------------------------------------------------------------------------+  |
|  |             VLC Audio Filter Plugin (libvlc_whisper_plugin)             |  |
|  |  - Audio Callback (Realtime): 0 locks, 0 allocs, bounded SPSC queue     |  |
|  |  - Sender Thread: 2s config snapshot polling, IPC packet framing        |  |
|  |  - Benchmark Module: Translation latency percentiles & tier counters   |  |
|  |  - Presenter: Dual-line / translated SPU subpicture rendering           |  |
|  +-------------------------------------------------------------------------+  |
+-------------------------------------|-----------------------------------------+
                                      | Local Authenticated IPC (Named Pipe / UDS)
                                      v
+-------------------------------------------------------------------------------+
|                    Worker Process (vlc-whisper-worker)                        |
|                                                                               |
|  +--------------------------+       +--------------------------------------+  |
|  |   Whisper Audio Engine   | ----> |   Asynchronous Translation Thread    |  |
|  |  - Local GGML Inference  |       |  - 800ms bounded latency queue       |  |
|  |  - Finalized Cues        |       |  - Latency stopwatch (microsecond)   |  |
|  +--------------------------+       |  - 3-Tier Keyless Fallback Engine    |  |
|                                     |    1. Web RPC (MkEWBc batchexecute)  |  |
|                                     |    2. GTX (translate_a/single)       |  |
|                                     |    3. Mobile Scrape (translate.com/m)|  |
|                                     +--------------------------------------+  |
|                                                        |                      |
|                                               HTTPS / TLS Requests            |
|                                                        v                      |
|                                            Google Translate Endpoints         |
+-------------------------------------------------------------------------------+
```

### 1. Inputs and Outputs:
- **UI Inputs**:
  - `whisper-translate-enabled` (bool, default `false`).
  - `whisper-translate-from` (string, ISO-639 code or `auto`).
  - `whisper-translate-to` (string, ISO-639 code, e.g. `ro`, `en`, `es`).
  - `whisper-translate-mode` (int: `0` = translated only, `1` = dual-line source + translation).
- **Worker Outputs**:
  - `VW_MSG_CAPTION_SEGMENT` containing primary text and optional translated subtitle payload, plus measured `translation_latency_us` and `translation_tier`.
- **Presenter Outputs**:
  - Single-line translated OSD cue or dual-line formatted cue (`<source_text>\n<translated_text>`) styled cleanly for VLC SPU rendering.
- **Benchmark Outputs (`benchmark_session_<id>.txt`)**:
  - `translation_enabled=true`
  - `translation_requests_sent=<count>`
  - `translation_success_count=<count>`
  - `translation_fallback_tier1_count=<count>`
  - `translation_fallback_tier2_count=<count>`
  - `translation_fallback_tier3_count=<count>`
  - `translation_timeout_count=<count>`
  - `translation_total_duration_us=<us>`
  - `translation_latency_samples=<count>`
  - `translation_latency_min_us=<us>`
  - `translation_latency_p50_us=<us>`
  - `translation_latency_p95_us=<us>`
  - `translation_latency_max_us=<us>`

### 2. Ownership & Threading Model:
- **VLC Audio Thread**: Zero translation work, zero locks, zero memory allocation. Realtime audio processing invariant is strictly preserved.
- **Plugin Sender Thread**: Reads translated segment frames from worker IPC and dispatches metrics to `vw_benchmark_t` and cues to `vw_caption_presenter_t`.
- **Worker Inference Thread**: Emits raw finalized Whisper segments into the worker's internal translation queue.
- **Worker Translation Thread**: Dedicated background worker thread that drains the translation queue, executes HTTPS requests with an 800ms deadline, timestamps latency, and passes translated results to the IPC output serializer.

### 3. Bounds, Time Units, and Failure Behavior:
- **Translation Timeout**: Maximum 800ms per cue.
- **Queue Limits**: Maximum 4 pending translation requests in worker queue; if saturated, oldest pending items drop translation and emit raw transcript immediately to prevent lag.
- **Text Bounds**: Maximum UTF-8 text limit of 1024 bytes per cue.
- **Graceful Fallback**: If all 3 fallback tiers fail or time out, the system defaults to displaying the original Whisper cue with zero user-visible error dialogs.

### 4. Privacy & Security Implications:
- Subtitle translation is **strictly opt-in**. The user must explicitly check "Auto Translation" in settings.
- When disabled, zero network traffic is generated.
- No translated or original transcript text is stored to disk or transmitted to any third-party logging service.
- All HTTP requests use TLS 1.2+ encryption.

### 5. Protocol Change:
- **Compatible Minor Bump (Protocol v1.5)**:
  - Add `VW_MSG_TRANSLATE_CTRL` message type (`16`).
  - Extend `vw_caption_segment_t` with `uint16_t translated_bytes`, `char translated_text[VW_MAX_CAPTION_TEXT_BYTES]`, `uint32_t translation_latency_us`, and `uint8_t translation_tier`.

## Acceptance Criteria
- [x] Settings dialog displays all translation controls in the same dialog: Auto Translation checkbox, Source dropdown, Target dropdown, Screen placement dropdown, Test input field, Test button, and Test status label.
- [x] Clicking "Test" in settings translates the test phrase and displays the translated text in the status label.
- [x] When Auto Translation is enabled and media is playing, subtitles appear translated in the selected target language.
- [x] When Dual-Line mode is selected, both source transcript and translated subtitles render cleanly on screen.
- [x] Translation latency metrics (min, p50, p95, max, tier breakdown, timeouts) are accurately captured in `vw_benchmark_t` and output to the session benchmark report.
- [x] When network is offline or translation times out (>800ms), original Whisper subtitles continue rendering without delay or stutter.
- [x] When Auto Translation is disabled, zero network requests are issued.
- [x] 100% of unit and integration tests pass under `ctest --preset linux-x64-debug` and Windows MinGW cross-build.
- [x] Code complies with standard C17, Google C style (`clang-format`), and symbol namespacing (`vw_`).

## Test Plan
1. **Unit Tests**:
   - Run `ctest -R test_translate` to test Web RPC payload formation, GTX response parsing, mobile regex extraction, HTML entity decoding, and language code validation.
   - Run `ctest -R test_benchmark` to verify translation latency statistical percentile calculation, tier breakdown counters, and report generation.
2. **Mock Network Tests**:
   - Simulate network timeouts and verify fallback to untranslated cues within $\le 800\text{ ms}$, validating that `translation_timeout_count` increments.
3. **Integration Verification**:
   - Verify Lua settings dialog with `luac -p lua/extensions/vlc_whisper_settings.lua`.
   - Verify full test suite and Valgrind memcheck:
     ```bash
     cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug
     ctest --test-dir build/linux-x64-debug -T memcheck
     ```
   - Verify Windows release and NSIS installer build:
     ```bash
     cmake --preset windows-x64-release && cmake --build --preset windows-x64-release --target installer
     ```

## Definition of Done
- [x] All authored code is standard C17 (`-std=c17`); no project-authored C++ introduced.
- [x] No blocking operations, locks, or allocations in VLC audio callbacks.
- [x] Network requests occur only when explicitly enabled by the user; no telemetry or disk logging of transcripts.
- [x] Translation queue is strictly bounded with timeout fallbacks and latency tracking.
- [x] Documentation updated: `docs/plans/step21b_translation_plan.md`, `docs/decisions.md` (ADR-024), `docs/api-contracts.md`, `docs/source-layout.md`, and `README.md`.
