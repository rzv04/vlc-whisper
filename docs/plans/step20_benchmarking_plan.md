# Task: Add per-session caption benchmarking

## Goal

Record bounded, privacy-safe per-session benchmark results and leave a unique temporary text report when playback stops or the filter is torn down.

## Context

- Relevant docs/ADR: `docs/roadmap.md` Step 20, `docs/architecture.md`, `docs/test-strategy.md`, `docs/api-contracts.md`, `docs/whisper-api.md`, `docs/source-layout.md`.
- VLC/worker/protocol version affected: VLC plugin and worker; protocol remains v1.4 because the existing STATUS `inference_us` field is sufficient for the aggregate timing export.
- Assumptions and explicit non-goals: reports are local diagnostics, contain no transcript, PCM, URL, token, or user path; abrupt process termination can only preserve the latest flushed snapshot.

## Scope

- In scope: worker inference timing, plugin transport/presentation counters, segment audio/text sizes, utterance latency percentiles, first-caption timing, processing-speed ratios, unique temporary report creation, and lifecycle finalization.
- Out of scope: telemetry, cloud calls, persistent transcripts, model benchmarking orchestration, and changes to caption scheduling policy.
- Files/components expected to change: plugin benchmark helper and module, worker inference/status accounting, tests, CMake, and benchmark-related documentation.

## Design

- Inputs and outputs: worker inference counters and STATUS snapshots feed the plugin; the plugin writes key/value text to a unique OS temporary file and logs only the report path.
- Ownership/threading model: plugin owns the report and presentation/IPC metrics on its sender thread; worker owns inference elapsed time and emits it through existing STATUS frames; the realtime audio callback remains counter/queue-only.
- Bounds, time units, and failure behavior: all durations are signed 64-bit microseconds; latency samples use a fixed bounded array and retain negative look-ahead values; report creation/write failure disables reporting without affecting playback.
- Privacy/security implications: reports are mode `0600` where supported, use unique temporary names, and contain aggregate numeric metrics only. They are never uploaded or treated as transcript storage.
- Protocol change: none; `STATUS.inference_us` becomes a measured cumulative inference duration and is emitted as periodic worker status.

## Acceptance criteria

- A successful caption session creates a unique temporary text report.
- Normal stop, media stop, worker failure, and filter close update the report with session duration, audio/caption sent and received counts, filtered counts, segment sizes, inference timing, latency percentiles, first-caption timing, and processing speed ratios.
- The report remains present after VLC exits unexpectedly, containing the last flushed snapshot and an incomplete state marker.
- Seek/pause/media-swap handling does not block playback or allow unbounded benchmark memory growth.
- Worker status timing is measured around real inference and does not include post-filtering.

## Test plan

- Unit-test metric counters, zero-duration ratios, negative latency, percentile calculation, bounded sample storage, report privacy, and report finalization.
- Add worker/status assertions for nonzero measured inference time on the existing integration path where a model is available.
- Run format checks, the native CMake build and CTest suite, and Valgrind memcheck per repository policy.

## Definition of done

- Implementation, tests, CMake wiring, and documentation agree on metric definitions and lifecycle behavior.
- The branch contains only Step 20 work plus the pre-existing untracked fixtures.
- Verification evidence is recorded before completion.

## Evidence

- `plugin/src/vw_benchmark.c` owns bounded report generation and lifecycle snapshots; `worker/src/vw_whisper_engine.c` measures `whisper_full()` wall time and `worker/src/vw_worker.c` exports it through existing STATUS frames.
- `clang-format --dry-run --Werror` passed for all changed C/H sources.
- `cmake --preset linux-x64-debug`, `cmake --build --preset linux-x64-debug -j2`, and `ctest --preset linux-x64-debug --output-on-failure` passed; CTest completed 22/22 tests.
- `ctest --test-dir build/linux-x64-debug -T memcheck --output-on-failure` completed all 22 tests; one model-gated test was skipped, and CTest reports 739 potential existing memory defects across source-decoder/VAD/engine/IPC/lifecycle paths, requiring separate cleanup and not attributable to the benchmark changes from the observed test output.
- `git diff --check` passed. The branch remains uncommitted and retains only Step 20 changes plus the pre-existing untracked audio fixtures.
