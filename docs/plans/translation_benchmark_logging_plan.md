# Task: Improve translation and benchmark logging

## Goal

Make translation failures immediately diagnosable in VLC Messages while keeping diagnostic output privacy-safe, and make benchmark output use one predictable `.txt` last-session file with human-readable timing units.

## Context

- Relevant contracts: `docs/invariants.md`, `docs/source-layout.md`, `docs/test-strategy.md`, `docs/plans/step20_benchmarking_plan.md`.
- VLC/worker/protocol version affected: VLC plugin and local benchmark writer; wire protocol remains unchanged.
- Preserve the existing translation providers and 800 ms global cue deadline; do not log subtitle bodies, PCM, auth tokens, or credentials; keep internal timing and protocol values in microseconds.

## Scope

- In scope: failed-translation diagnostics, stable benchmark report naming, second/millisecond report presentation, unique atomic staging writes, per-user POSIX fallback, tests, and corresponding canonical documentation.
- Out of scope: provider/retry policy, wire fields, internal microsecond timing, or caption scheduling behavior.

## Design

- Existing translation result metadata feeds `PLUGIN_TRANSLATION_FAILURE`; segment positions are rendered in seconds and translation latency in milliseconds.
- Benchmark snapshots target `vlc-whisper-benchmark.txt` in the selected per-user temporary directory. Each snapshot is first written to a unique same-directory staging file and atomically replaces the stable destination.
- If `XDG_RUNTIME_DIR` and `TMPDIR` are unavailable on POSIX, fallback is `/tmp/vlc-whisper-<uid>/` with private ownership/permissions validation.
- Report schema v2 serializes user-facing durations with dot-decimal fixed-point text independent of `LC_NUMERIC`; calculations and stored values remain microseconds.
- Translation failure classes remain pipeline unavailable, global deadline exhausted, or configured provider fallbacks failed before deadline.

## Acceptance criteria

- Every observed attempted translation failure emits a privacy-safe VLC error with segment context, millisecond latency, failure class, and detail.
- Routine successful VOUT discovery does not emit an informational message; actual VOUT lookup failure remains visible.
- The benchmark report uses one stable last-session `.txt` destination, unique staging files, and a private per-user POSIX fallback.
- Benchmark report v2 exposes long durations in seconds, latency/drop timing in milliseconds, and locale-independent dot decimals.
- Existing main-branch live-clock reset and caption presentation behavior are preserved.
- Regression tests cover the new report and diagnostic boundaries.

## Test plan

Run strict formatting, configure/build, CTest, and Valgrind through the repository CI. Unit coverage verifies failure classifications, unit conversion, stable replacement, unique staging, POSIX fallback isolation, and preservation of the live-clock reset regression.

## Definition of done

- C17 and current project invariants preserved.
- No realtime callback work added.
- No transcript/PCM/credential persistence or logging added.
- Canonical docs and tests agree with the report contract.
- PR is conflict-free with current `main` and CI passes.

## Evidence

Fresh verification is recorded on PR #44 after synchronizing current `main` with incoming changes prioritized.
