# Task: Real-Time Subtitle Translation & Unified Settings GUI (Step 21b)

## Goal
Provide user-opt-in real-time translation of transcribed subtitles using an asynchronous keyless Google translation engine (Web RPC, GTX, and Mobile scrape fallbacks), integrated translation latency benchmarking, and a unified settings UI inside the existing VLC-Whisper settings dialog. Translation must never block the worker inference/control loop or VLC's Lua/UI/audio paths.

## Context
- **Relevant docs/ADR**: [`docs/plans/step21_translation_research.md`](step21_translation_research.md), `ADR-022` (Lua Settings Extension), `ADR-023` (Worker Network Carve-Out Policy), `ADR-024` (Opt-in Subtitle Translation Service), [`docs/product.md`](../product.md), [`docs/architecture.md`](../architecture.md), and review issue #28.
- **VLC/worker/protocol version affected**: VLC 3.0.23, `vlc-whisper-worker` (v0.3.0), Protocol v1.5 minor contract bump.
- **Review remediation branch**: `fix/step-21b-review-findings`, derived only from `gemini/milestone-4-step-21b`.
- **Assumptions and explicit non-goals**:
  - **Assumptions**: Subtitle translation is strictly opt-in; when disabled (`whisper-translate-enabled = false`), zero translation network requests are made. When enabled, only finalized segment text is transmitted over TLS to translation endpoints.
  - **Explicit non-goals**: Cloud transcription (Whisper stays local/offline); third-party telemetry; storing transcript bodies or credentials to disk; custom multi-window dialogs; adding Windows CI on the review-fix branch.

## Scope
- **In scope**:
  1. **Single-Dialog Settings UI Extension (`lua/extensions/vlc_whisper_settings.lua`)**:
     - **Auto Translation**: Checkbox widget (`whisper-translate-enabled`, default `false`).
     - **Source Language (From)**: Dropdown widget (`whisper-translate-from`, default `auto`).
     - **Target Language (To)**: Dropdown widget (`whisper-translate-to`, default `en` or `ro`).
     - **Screen Placement / Display Mode**: Dropdown widget (`whisper-translate-mode`: translation only or source + translation).
     - **Safe Test Guidance**: The dialog provides worker-runtime test instructions but performs no HTTP. A future dedicated asynchronous test-result IPC can restore phrase-entry testing without blocking Lua.
  2. **Worker Keyless Translation Engine (`worker/src/vw_translate.c` & `worker/include/vw_translate.h`)**:
     - C17 implementation of the 3-tier fallback translation engine derived from [`samples/snippets/script.py`](../../samples/snippets/script.py):
       1. **Tier 1 (Primary)**: Google Translate Web RPC (`MkEWBc`).
       2. **Tier 2 (Fallback 1)**: Legacy GTX single-query endpoint.
       3. **Tier 3 (Fallback 2)**: Mobile scrape endpoint.
     - Platform transports:
       - **Windows**: WinHTTP with remaining-deadline timeouts, HTTP-status validation, and partial-read failure handling.
       - **Linux**: `curl` child with close-on-exec pipe descriptors and one remaining-deadline `-m` timeout.
     - Bounded JSON/HTML parsing: correct nested JSON escaping, surrogate-pair decoding, escaped-quote parity, and overflow failure rather than silent truncation.
  3. **Dedicated Worker Translation Thread (`worker/src/vw_translate_async.c`)**:
     - Maximum four pending translation jobs.
     - Main inference/control loop only copies finalized cues into the queue and never executes network I/O.
     - IPC reader invalidates translation epochs immediately on seek/session/pause/resume/translation-config/shutdown arrival so a pre-seek completion cannot become visible after the control frame has arrived.
  4. **Translation Latency Benchmarking (`plugin/src/vw_benchmark.c`)**:
     - Per-cue elapsed latency is recorded for both success and failure.
     - Tier distribution for successful translations.
     - Separate early failure and true global-deadline timeout counters.
     - Bounded latency percentiles include failed attempts so slow failures cannot disappear from p95/max.
  5. **Timing & Realtime Fallback Handling**:
     - One absolute translation budget of **800 ms per cue across all three fallback tiers**, not 800 ms per request/tier.
     - Network or parser failure never stalls Whisper inference or control processing.
     - Translation queue unavailability/saturation degrades to source captions rather than blocking.
  6. **Protocol v1.5 Wire Contract**:
     - Caption segments carry optional translated text, total translation elapsed time, and resolving tier.
     - `VW_MSG_TRANSLATE_CTRL` is advertised by `VW_CAPABILITY_TRANSLATION`; newer plugins do not send message type 16 to older same-major workers lacking that bit.
  7. **Automated Unit & Contract Tests**:
     - URL/query encoding and nested Web RPC JSON escaping.
     - GTX/RPC/mobile response parsing, surrogate pairs, escaped backslashes/quotes, and overflow rejection.
     - Deterministic injected transport fallback and one-global-deadline behavior without live network calls.
     - Asynchronous submit latency and seek-style epoch invalidation.
     - v1.4-compatible translation-disabled client behavior without an IPC send.
     - Translation benchmark failure/timeout classification and latency retention.
- **Out of scope**:
  - Embedded commercial API keys or billing accounts.
  - Disk caching of translated phrases.
  - Multi-dialog UI frameworks.
  - A new Windows CI workflow/job; requested verification remains Linux-only on this remediation branch.

## Design

```
+-------------------------------------------------------------------------------+
|                           VLC Media Player 3.0.23                             |
|                                                                               |
|  +-------------------------------------------------------------------------+  |
|  |                Lua Extension: vlc_whisper_settings.lua                  |  |
|  |  Config-only callbacks: no HTTP, polling, sleeps, or phrase logging     |  |
|  +-------------------------------------------------------------------------+  |
|                                     |                                         |
|                       cfg_set() / config variables                            |
|                                     v                                         |
|  +-------------------------------------------------------------------------+  |
|  |             VLC Audio Filter Plugin (libvlc_whisper_plugin)             |  |
|  |  - Audio Callback: 0 locks, 0 allocs, bounded SPSC queue                |  |
|  |  - Sender Thread: config sync, authenticated IPC, benchmark, presenter  |  |
|  +-------------------------------------------------------------------------+  |
+-------------------------------------|-----------------------------------------+
                                      | Local Authenticated IPC (Named Pipe / UDS)
                                      v
+-------------------------------------------------------------------------------+
|                    Worker Process (vlc-whisper-worker)                        |
|                                                                               |
|  +--------------------------+       +--------------------------------------+  |
|  | Whisper/VAD Main Loop    | ----> | Bounded Async Translation Thread     |  |
|  | - Local inference        | copy  | - <= 4 pending jobs                 |  |
|  | - Control/seek handling  | cue   | - one 800ms cue-wide deadline       |  |
|  | - Never network-blocked  |       | - 3-tier keyless fallback           |  |
|  +--------------------------+       +--------------------------------------+  |
|             ^                                  |                               |
|             | IPC reader invalidates epoch     | HTTPS finalized text only     |
|             +----------------------------------+------------------------------>|
|                                                Google Translate endpoints      |
+-------------------------------------------------------------------------------+
```

### 1. Inputs and Outputs
- **UI Inputs**:
  - `whisper-translate-enabled` (bool, default `false`).
  - `whisper-translate-from` (string, ISO-639 code or `auto`).
  - `whisper-translate-to` (string, ISO-639 code).
  - `whisper-translate-mode` (`0` translation only, `1` dual-line).
- **Worker Outputs**:
  - `VW_MSG_CAPTION_SEGMENT` containing source text and optional translated subtitle payload plus `translation_latency_us` and `translation_tier`.
- **Benchmark Outputs** include request/success/tier/failure/timeout counters, total elapsed translation duration, and bounded latency min/p50/p95/max.

### 2. Ownership & Threading Model
- **VLC Audio Thread**: zero translation work, locks, or allocation.
- **Lua/UI Thread**: config writes and guidance only; zero HTTP/stream reads.
- **Plugin Sender Thread**: sends config/control IPC and receives translated segment frames/metrics.
- **Worker Main Loop**: inference, VAD, source decoding, model progress, and single-writer IPC serialization; never calls the network translator.
- **Worker IPC Reader**: queues frames and invalidates old translation epochs immediately when a timeline/config-invalidating control arrives.
- **Worker Translation Thread**: the only subtitle-translation network executor.

### 3. Bounds, Time Units, and Failure Behavior
- **Translation deadline**: one global 800 ms absolute deadline per attempted cue across Tier 1 -> Tier 2 -> Tier 3.
- **Queue limit**: maximum four pending jobs.
- **Text bound**: 1,024 UTF-8 bytes per cue.
- **Graceful fallback**: failed/deadline/unavailable translation emits source text; invalidated old-epoch results are discarded.
- **Response overflow**: fails translation; parsers never report a silently truncated translation as successful.

### 4. Privacy & Security Implications
- Translation is explicitly opt-in and disabled by default.
- Audio/PCM, Whisper model state, and VAD data never leave the machine for translation.
- Only finalized subtitle text is sent over HTTPS when translation is enabled.
- Diagnostic logs and benchmark reports contain only metadata/aggregate metrics, not source or translated subtitle bodies.
- Lua performs no HTTP; worker-only egress keeps the network boundary auditable.

### 5. Protocol Compatibility
- Protocol remains v1.5.
- `VW_CAPABILITY_TRANSLATION = 1U << 4` gates `VW_MSG_TRANSLATE_CTRL` and translation semantics.
- An older same-major worker without that flag remains usable when translation is disabled. Enabling unsupported translation fails locally rather than sending an unknown message.

## Acceptance Criteria
- [x] Auto Translation, source, target, and display-mode controls remain in the single settings dialog.
- [x] Lua translation callbacks are nonblocking and perform zero HTTP; test guidance directs verification through the real worker runtime.
- [x] Runtime translation is executed by a dedicated bounded worker thread, not by the main inference/control loop.
- [x] Seek/session/config control arrival invalidates old translation work before stale completion can be emitted.
- [x] The three fallback tiers share one <=800 ms total cue budget.
- [x] POSIX translation pipe descriptors are close-on-exec so concurrent model-download children cannot hold translation EOF open.
- [x] WinHTTP rejects non-2xx/partial read failures and uses remaining-deadline timeouts.
- [x] RPC JSON escaping, Unicode surrogate decoding, quote/backslash handling, and output overflow behavior are bounded and tested.
- [x] Protocol v1.5 translation traffic is capability-gated for same-major compatibility.
- [x] Translation failure latency is included in benchmark duration/percentiles; failures and timeouts are classified separately.
- [x] Source/translated subtitle bodies are not written to diagnostic logs or benchmark reports.
- [x] Auto Translation disabled means zero translation requests are issued.
- [ ] Windows CI is intentionally **not added** on this remediation branch per project-owner request. Existing Windows build presets remain unchanged.
- [ ] Linux format/build/CTest/Valgrind verification evidence is filled in below after the remediation branch run completes.

## Test Plan
1. **Unit Tests**:
   - `test_translate`: request escaping, Unicode, overflow, deterministic fallback, global timeout.
   - `test_translate_async`: nonblocking queue submit and stale-epoch cancellation.
   - `test_worker_client_compat`: no v1.5 message is sent to a legacy-capability peer.
   - `test_benchmark`: failure vs timeout classification and failed-latency retention.
2. **Integration Tests**:
   - Existing worker IPC/lifecycle tests now link the asynchronous translator.
   - Existing caption presenter/protocol tests continue covering translated segment ownership and display.
3. **Linux Verification**:
   - Strict `clang-format --dry-run --Werror` over project C/H.
   - `cmake --preset linux-x64-coverage` and build.
   - `ctest --preset linux-x64-coverage --output-on-failure`.
   - Valgrind memcheck through the existing CI workflow.
4. **Windows**:
   - No CI job is introduced here. Manual/release preset verification can be performed in a later Windows-specific pass.

## Definition of Done
- [x] All remediation writes are confined to `fix/step-21b-review-findings`.
- [x] No other branch is changed and no pull request is opened.
- [x] Network translation is off the worker main loop and off the VLC Lua/UI path.
- [x] One cue-wide deadline, parser/transport hardening, epoch cancellation, compatibility gating, privacy-safe logging, and benchmark fixes are implemented.
- [x] Regression tests for the review findings are present.
- [x] README and protocol/privacy documentation match actual behavior.
- [ ] Existing Linux CI-equivalent verification passes and temporary branch-trigger change is reverted before completion.

## Review Remediation Evidence

Issue #28 is the review source. This section is completed by the remediation branch rather than claiming evidence from the original Step 21b implementation.

- Branch: `fix/step-21b-review-findings` from `gemini/milestone-4-step-21b` tip `29a367d85188600646de15afef43b6a4d79f7e4e`.
- Windows CI: explicitly excluded from this branch by project-owner request.
- Linux verification: pending final branch run.
