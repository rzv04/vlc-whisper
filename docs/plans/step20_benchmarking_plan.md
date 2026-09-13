# Task: Add per-session caption benchmarking

## Goal

Record bounded, privacy-safe per-session benchmark results and keep one predictable last-session `.txt` report in the platform temporary directory.

## Context

- Relevant docs/ADR: `docs/roadmap.md` Step 20, `docs/architecture.md`, `docs/test-strategy.md`, `docs/api-contracts.md`, `docs/whisper-api.md`, `docs/source-layout.md`.
- VLC/worker/protocol version affected: VLC plugin and worker; protocol remains v1.4 because the existing STATUS `inference_us` field is sufficient for the aggregate timing export.
- Assumptions and explicit non-goals: reports are local diagnostics, contain no transcript, PCM, URL, token, or user path; abrupt process termination can only preserve the latest flushed snapshot.

## Scope

- In scope: worker inference timing, plugin transport/presentation counters, segment audio/text sizes, utterance latency percentiles, first-caption timing, processing-speed ratios, one last-session report path, and lifecycle finalization.
- Out of scope: telemetry, cloud calls, persistent transcripts, model benchmarking orchestration, and changes to caption scheduling policy.
- Files/components expected to change: plugin benchmark helper and module, worker inference/status accounting, tests, CMake, and benchmark-related documentation.

## Design

- Inputs and outputs: worker inference counters and STATUS snapshots feed the plugin; the plugin writes aggregate key/value text to `vlc-whisper-benchmark.txt` in the selected per-user temporary directory, replacing the previous session at the start of a successful new session.
- Ownership/threading model: plugin owns the report and presentation/IPC metrics on its sender thread; worker owns inference elapsed time and emits it through existing STATUS frames; the realtime audio callback remains counter/queue-only.
- Bounds, time units, and failure behavior: internal timing, protocol fields, PTS values, and bounded latency samples remain signed 64-bit microseconds. Benchmark report schema v2 converts accumulated/session durations to seconds with millisecond precision and latency/drop metrics to milliseconds, retaining negative look-ahead latency values. Each snapshot uses a unique same-directory staging file before atomic replacement of the stable destination.
- Privacy/security implications: reports and POSIX staging files are owner-only where supported and contain aggregate numeric metrics only. They are never uploaded or treated as transcript storage. POSIX fallback uses a private UID-scoped temporary directory rather than a shared `/tmp` filename.
- Protocol change: none; `STATUS.inference_us` remains a cumulative microsecond inference duration emitted through periodic worker status.

## Acceptance criteria

- A successful caption session writes one `vlc-whisper-benchmark.txt` report in the selected per-user temporary directory and replaces the previous session rather than accumulating uniquely named reports.
- Normal stop, media stop, worker failure, and filter close update the report with session duration, audio/caption sent and received counts, filtered counts, segment sizes, inference timing, latency percentiles, first-caption timing, and processing speed ratios.
- Benchmark-facing time fields use seconds for long accumulated/session durations and milliseconds for latency/drop measurements; internal timing remains microseconds.
- Snapshot writes use unique staging files and locale-independent dot-decimal serialization.
- The report remains present after VLC exits unexpectedly, containing the last flushed snapshot and an incomplete state marker.
- Seek/pause/media-swap handling does not block playback or allow unbounded benchmark memory growth.
- Worker status timing is measured around real inference and does not include post-filtering.

## Test plan

- Unit-test metric counters, zero-duration ratios, negative latency, percentile calculation, bounded sample storage, stable report naming/replacement, unique staging behavior, per-user POSIX fallback, human-readable timing-unit conversion, report privacy, and report finalization.
- Preserve the live-clock reset regression across seek/epoch transitions.
- Add worker/status assertions for nonzero measured inference time on the existing integration path where a model is available.
- Run format checks, the native CMake build and CTest suite, and Valgrind memcheck per repository policy.

## Definition of done

- Implementation, tests, CMake wiring, and documentation agree on metric definitions and lifecycle behavior.
- Verification evidence is recorded before completion.

## Evidence

- `plugin/src/vw_benchmark.c` owns bounded report generation and lifecycle snapshots; worker inference/status accounting remains unchanged.
- Original Step 20 verification belongs to its historical implementation; the current follow-up PR carries fresh CI evidence for the stable report contract.
