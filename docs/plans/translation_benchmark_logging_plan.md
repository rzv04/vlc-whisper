# Task: Improve translation and benchmark logging

## Goal
Make translation failures immediately diagnosable in VLC Messages while keeping diagnostic output privacy-safe, and make benchmark output use one predictable `.txt` file containing the latest session with human-readable timing units.

## Context
- Relevant docs/ADR: `docs/architecture.md`, `docs/product.md`, `docs/source-layout.md`, `docs/api-contracts.md`, `docs/test-strategy.md`, `docs/plans/step20_benchmarking_plan.md`.
- VLC/worker/protocol version affected: VLC plugin and local benchmark writer; protocol v1.6 remains unchanged.
- Assumptions and explicit non-goals: preserve the existing three-tier translator and 800 ms global cue deadline; do not log subtitle bodies, PCM, auth tokens, or credentials; do not add a new UI or wire message; keep internal timing and protocol values in microseconds.

## Scope
- In scope: classify failed translation attempts in the plugin diagnostic stream; include segment timing/latency context using human-readable display units; replace unique benchmark report names with one last-session `.txt` path; export benchmark durations in seconds/milliseconds; update tests and docs.
- Out of scope: changing translation providers, retry policy, protocol fields, internal microsecond timing, or VLC playback/caption timing behavior.
- Files/components expected to change: `plugin/src/vw_benchmark.c`, `plugin/include/vw_benchmark.h`, `tests/unit/test_benchmark.c`, and benchmark/logging documentation.

## Design
- Inputs and outputs: existing `SEGMENT.translation_attempted`, `translation_latency_us`, tier/success metadata feed a `PLUGIN_TRANSLATION_FAILURE` diagnostic; benchmark snapshots write to `vlc-whisper-benchmark.txt` in the platform temp directory.
- Ownership/threading model: existing plugin sender thread remains the owner of benchmark updates and translation-result observation; no work is added to the realtime audio callback.
- Bounds, time units, and failure behavior: internal segment PTS, translation latency, timeout decisions, protocol fields, and benchmark samples remain microsecond-based. At the presentation boundary, translation diagnostics show media positions in seconds and latency in milliseconds. Benchmark report schema v2 exports long accumulated/session durations in seconds with millisecond precision and latency/drop metrics in milliseconds. Failures classify as pipeline unavailable, global deadline exhausted, or all configured provider fallbacks failed before the deadline. Playback and source-caption fallback continue unchanged.
- Privacy/security implications: diagnostics contain no source/translated subtitle bodies and benchmark output remains aggregate-only. POSIX snapshots retain owner-only permissions.
- Protocol change: none.

## Acceptance criteria
- [ ] Every attempted translation failure observed by the plugin emits a privacy-safe VLC Messages error with segment context, millisecond latency, and failure class.
- [ ] Translation failures remain recoverable and never interrupt playback.
- [ ] Benchmark output always targets one predictable `vlc-whisper-benchmark.txt` file in the platform temp directory and replaces the previous session.
- [ ] Benchmark report timing fields use seconds for long accumulated/session durations and milliseconds for latency/drop measurements, with no benchmark-facing raw microsecond timing fields.
- [ ] Unit tests cover failure classifications, display-unit conversions, and stable benchmark path behavior.
- [ ] Benchmark/logging documentation reflects the new behavior.

## Test plan
Run `clang-format --dry-run --Werror plugin/include/vw_benchmark.h plugin/src/vw_benchmark.c tests/unit/test_benchmark.c`, then `cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug --output-on-failure`, followed by `ctest --test-dir build/linux-x64-debug -T memcheck`. On Windows/VLC, enable `whisper-logging`, force an unavailable/failed translation path, and confirm `PLUGIN_TRANSLATION_FAILURE` appears in VLC Messages with seconds/milliseconds but without subtitle text. Confirm `%TEMP%\\vlc-whisper-benchmark.txt` is replaced on the next playback session and contains report version 2 human-readable timing fields.

## Definition of done
- [ ] C17 code; no project-authored C++ introduced
- [ ] No blocking work in VLC audio callback
- [ ] No unapproved network access, telemetry, transcript/PCM persistence, or sensitive logs introduced; any approved egress is documented and opt-in
- [ ] Memory, audio queue, frame, text, and retry limits are bounded
- [ ] Error path is safe: captions may stop, playback does not
- [ ] Unit/contract/integration tests pass as applicable
- [ ] Formatting, warnings-as-errors, and static checks pass
- [ ] Protocol contract and compatibility version updated if needed
- [ ] `docs/decisions.md`, roadmap, and AI context updated when assumptions change
- [ ] Reviewer can reproduce the result from a clean checkout

## Evidence
- Build/test outputs or CI links: pending branch verification.
- Measured performance (if relevant): not applicable; no hot-path work added.
- Known limitations/follow-ups: current translator result metadata cannot distinguish HTTP status from response-parse failure within a provider tier without a future compatible diagnostic extension; this change reports the most specific failure class currently preserved at the plugin boundary.
