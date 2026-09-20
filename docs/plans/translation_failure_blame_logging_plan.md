# Task: Add explicit translation failure blame logging

## Outcome

Every delivered failed translation produces one concise privacy-safe VLC Messages diagnostic with an explicit worker-owned cause and terminal fallback tier, while the benchmark report aggregates provider, transport, and parse failures without storing subtitle text.

## Scope

- In:
  - Preserve translation latency measurement and existing successful tier telemetry.
  - Classify translation failures in the worker as provider, transport, parse, deadline, or local/overload causes.
  - Preserve the terminal fallback tier and attempted-tier mask in worker-internal translation results.
  - Reuse recoverable `VW_MSG_ERROR` frames for worker-to-plugin failure diagnostics instead of extending caption payloads.
  - Special-case translation diagnostic error codes in the plugin so VLC Messages receives one concise `PLUGIN_TRANSLATION_FAILURE` line rather than a generic worker error plus a second translation error.
  - Add benchmark aggregates `translation_provider_failure_count`, `translation_transport_failure_count`, and `translation_parse_failure_count`; keep `translation_timeout_count` but feed it from explicit deadline diagnostics rather than latency inference.
- Out:
  - Provider/retry ordering changes; the fallback remains Web RPC -> GTX -> Mobile scrape under one 800 ms cue deadline.
  - Translation quality scoring, retries beyond the existing three tiers, transcript/body logging, remote telemetry, or API-key providers.
  - Caption scheduling, renderer behavior, realtime audio callback work, or a caption-segment wire-layout change.
- Components/files:
  - `worker/include/vw_translate.h`, `worker/src/vw_translate.c`
  - `worker/include/vw_translate_async.h`, `worker/src/vw_translate_async.c`
  - `worker/src/vw_worker.c`
  - `protocol/include/vw_protocol_types.h`
  - `plugin/include/vw_benchmark.h`, `plugin/src/vw_benchmark.c`, `plugin/src/vw_whisper_module.c`
  - focused unit/seam tests and contract documentation

## Contract map

`translation HTTP/parser -> vw_translate_text result metadata -> async completion -> recoverable VW_MSG_ERROR -> plugin receive loop -> VLC logger + benchmark cause counters -> benchmark session lifecycle`

`translation completion latency/tier -> caption segment -> plugin benchmark translation metrics -> benchmark session lifecycle`

- Invariants touched: state/API, lifecycle, metric, privacy/network, failure classification.
- The worker remains the sole authority for cause classification. The plugin renders/counts the cause and never infers provider blame from latency.

## Failure semantics

| Boundary/failure | retry | recover | fail session | fail process/startup |
| --- | --- | --- | --- | --- |
| HTTP non-2xx/provider rejection | existing next translation tier while budget remains | source caption after fallback exhaustion | | |
| DNS/connect/TLS/curl/WinHTTP transport failure | existing next translation tier while budget remains | source caption after fallback exhaustion | | |
| Successful HTTP response with unusable provider body | existing next translation tier while budget remains | source caption after fallback exhaustion | | |
| Global 800 ms deadline exhausted | | source caption; explicit deadline diagnostic | | |
| Local translation setup/queue degradation | | source caption; explicit local diagnostic when a failed accepted job is delivered | | |
| Diagnostic frame send fails | | | | existing worker transport-fatal path |

A recoverable translation diagnostic never disables captions or playback. Stale epoch results remain discarded rather than logged against a new media epoch.

## Lifecycle impact

- START/new media: benchmark cause counters start at zero with the existing benchmark session; translator epoch behavior is unchanged.
- Pause/resume: unchanged.
- Seek/discontinuity: queued/in-flight stale translation results and their diagnostics remain invalidated with the translation epoch.
- Media swap: same reset behavior as the existing translation/benchmark session.
- EOF/tail flush: accepted translation completions retain their diagnostics and are drained in chronological order before teardown.
- STOP/shutdown: unchanged bounded translation drain policy; no background logging thread is added.
- Worker failure/respawn: counters remain plugin-session telemetry; a replacement worker reports new explicit causes normally.
- Overload/drop: source-only degradation remains bounded; accepted jobs that are failed locally carry an explicit local cause rather than masquerading as provider failure.

## Identity / metrics / hot path

- Identity-bearing values changed; overflow behavior: none. Diagnostic text is bounded by `VW_MAX_ERROR_MSG_BYTES`; it contains only segment id, symbolic cause/tier, attempted-tier mask, optional HTTP status, and latency.
- Metrics changed:
  - `translation_provider_failure_count`: plugin benchmark owner, count, benchmark-session reset, incremented only from explicit provider diagnostics.
  - `translation_transport_failure_count`: plugin benchmark owner, count, benchmark-session reset, incremented only from explicit transport diagnostics.
  - `translation_parse_failure_count`: plugin benchmark owner, count, benchmark-session reset, incremented only from explicit parse diagnostics.
  - `translation_timeout_count`: existing plugin benchmark owner, count, benchmark-session reset, changed to explicit deadline diagnostics rather than elapsed-time inference.
  - Existing translation request/success/failure totals and latency samples remain caption-result owned.
- Realtime-adjacent code changed: none. Classification occurs on translator threads; diagnostic IPC/logging occurs on the worker/plugin non-realtime loops.

## Tests first

- Red-before-implementation specs:
  - translator reports terminal tier + transport cause when all HTTP attempts fail at transport.
  - translator reports terminal tier + parse cause when each HTTP request succeeds but all response parsers reject the bodies.
  - translator reports provider cause for an injected provider rejection and preserves the provider status when available.
  - global deadline reports explicit deadline cause and does not attempt later tiers after budget exhaustion.
  - async result preserves failure metadata through the background queue and local-overload completion path.
  - plugin benchmark records provider/transport/parse/deadline aggregates without inferring timeout from latency.
  - translation diagnostic logging remains privacy-safe and concise, with no source or translated subtitle body.
  - worker-to-plugin seam emits a recoverable translation error diagnostic and still delivers the source caption fallback.
- Existing ledger regressions affected: translation global deadline, nonblocking setup failure, async ordering/epoch invalidation, protocol error-frame round trip, benchmark failure logging, media-end translation drain.
- Faults to inject: transport failure, HTTP/provider rejection, malformed provider response, deadline consumption, bounded local queue degradation.

## Implementation

1. Add a bounded worker translation failure result carrying cause, terminal tier, attempted-tier mask, and optional HTTP status.
2. Make WinHTTP/POSIX curl transport return explicit provider/transport/deadline outcomes; parser rejection supplies parse cause.
3. Preserve failure metadata through `vw_translate_async_result_t`, including local overload/eviction.
4. Add translation-specific recoverable `vw_error_code_t` values and emit exactly one diagnostic frame for each delivered failed attempted translation before its source-caption fallback.
5. Special-case those error codes in the plugin receive loop: route them to `PLUGIN_TRANSLATION_FAILURE`, update explicit-cause benchmark counters, and do not also emit `PLUGIN_WORKER_ERROR`.
6. Remove generic failure logging and latency-based timeout inference from `vw_benchmark_record_translation`; it remains responsible for total request/success/failure and latency/tier metrics.
7. Extend benchmark report schema fields and canonical docs/tests.
8. Search all translation result producers, async copies, protocol error consumers, benchmark call sites, teardown drains, and quality/test harness consumers before completion.

## Verification

- [ ] Relevant new specs failed for the intended reason before implementation
- [ ] New/affected specs pass
- [ ] Existing relevant regression suite passes
- [ ] `clang-format --dry-run --Werror <modified-c-files>`
- [ ] `cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug`
- [ ] `ctest --preset linux-x64-debug --output-on-failure`
- [ ] Existing memcheck gate run when available/applicable
- [ ] Only contract-relevant docs updated

## Evidence

- Commands/results: pending implementation and CI verification.
- Known limitations/follow-ups: POSIX curl can classify HTTP rejection from curl's exit status but may not always expose the exact HTTP status code; the symbolic provider cause remains authoritative when status is unavailable.
