# Task: Experiment with LocalAgreement-2 for live caption latency

## Goal
Measure perceived live/network caption latency when immutable output is gated by Whisper-Streaming-style LocalAgreement-2 instead of immediate first-pass commitment.

## Context
- Relevant docs/ADR: `docs/architecture.md`, `docs/test-strategy.md`, `docs/whisper-api.md`, ADR-017/018/019/020/021.
- VLC/worker/protocol version affected: worker implementation only; protocol remains v1.6.
- Assumptions and explicit non-goals: branch starts at `main` SHA `3d1387444bfc85e808497858e6dffe00abc16567`; this is an experiment, not a production policy decision. No AlignAtt, fuzzy agreement, automatic Whisper history, new network behavior, presenter rewrite, or source/lookahead change. `graphify-out/` is absent on current `main`, so dependencies are verified from CMake and source callers instead.

## Scope
- In scope: LocalAgreement-2 for `VW_SOURCE_LIVE_AUDIO`, live-only token timestamps, exact confirmed-prefix skipping near the last committed timestamp, state reset on discontinuity/pause/restart, unit tests, and documentation.
- Out of scope: full 30-second sentence-aware Whisper-Streaming buffer trimming, the paper's 200-word inter-sentence prompt, `no_context=false`, LocalAgreement for local lookahead, and protocol changes.
- Files/components expected to change: `vw_local_agreement.[ch]`, `vw_whisper_engine.[ch]`, `vw_worker.c`, worker/test CMake, LocalAgreement/Whisper tests, architecture/source-layout/whisper-api/test-strategy docs.

## Design
- Inputs and outputs: each live Whisper pass exposes normal text tokens with relative token timestamps. The worker converts them to absolute PTS and feeds a bounded LocalAgreement state. Only the longest common prefix shared by two consecutive unconfirmed hypotheses is emitted to the existing immutable segment builder.
- Ownership/threading model: LocalAgreement state belongs to the worker main loop; no plugin callback or IPC reader-thread work is added.
- Bounds, time units, and failure behavior: fixed token/history capacities; all PTS are signed microseconds. Confirmed-prefix alignment uses the paper's 1-second timestamp neighborhood and exact 1..5-token suffix/prefix matching. Overflow withholds excess text for a later update rather than emitting truncated text. Reset discards unconfirmed state.
- Privacy/security implications: no transcript/PCM persistence or new network access. Diagnostics contain counts/timing only, never token text.
- Protocol change: none.

The experiment intentionally retains VLC-Whisper's current 2-second first live inference and 1-second subsequent cadence so `main` versus this branch isolates the commit policy. The existing 500 ms growing-window edge holdback is bypassed when LocalAgreement is active; two-pass agreement becomes the stability gate. `no_context=true` remains unchanged. This is LocalAgreement-2 adapted to VLC-Whisper's bounded 8-second rolling buffer, not a claim to reproduce every Whisper-Streaming subsystem.

## Acceptance criteria
- [ ] First live hypothesis is hidden; a token prefix is emitted only after exact agreement with the next hypothesis.
- [ ] Confirmed tokens are never re-emitted when overlapping windows repeat them near the prior commit frontier.
- [ ] Pause, seek/discontinuity, session restart, invalid PTS, and stop clear unconfirmed agreement state.
- [ ] Source/lookahead and local-file PCM fallback retain existing behavior.
- [ ] Emitted protocol segments remain `is_final=true`; visible captions are never revised.
- [ ] Existing benchmark reports continue measuring first-caption and live utterance latency from the newly committed token PTS.
- [ ] Automated tests cover first-pass withholding, two-pass commit, divergence, confirmed-prefix skipping, and reset.
- [ ] Documentation matches the experimental behavior.

## Test plan
Run `clang-format --dry-run --Werror` over modified C/H/test files, `cmake --preset linux-x64-debug`, `cmake --build --preset linux-x64-debug`, `ctest --preset linux-x64-debug --output-on-failure`, and `ctest --test-dir build/linux-x64-debug -T memcheck`. On real VLC/network media, compare `main` and this branch using the existing benchmark report fields for first sent-caption elapsed time and live utterance latency p50/p95.

## Definition of done
- [ ] C17 code; no project-authored C++ introduced
- [ ] No blocking work in VLC audio callback
- [ ] No unapproved network access, telemetry, transcript/PCM persistence, or sensitive logs introduced
- [ ] Memory, audio queue, frame, text, and retry limits are bounded
- [ ] Error path is safe: captions may stop, playback does not
- [ ] Unit/contract/integration tests pass as applicable
- [ ] Formatting, warnings-as-errors, and static checks pass
- [ ] Protocol contract remains compatible with no version change
- [ ] Relevant architecture/source/test/Whisper documentation updated
- [ ] Reviewer can reproduce the result from a clean checkout

## Evidence
- Build/test outputs or CI links: to be filled by PR CI.
- Measured performance: intentionally deferred to live-stream A/B runs; branch provides the commit policy under test.
- Known limitations/follow-ups: exact LocalAgreement only; no fuzzy matching, no sentence-aware 30-second buffer, no committed-text prompt, and no AlignAtt.
