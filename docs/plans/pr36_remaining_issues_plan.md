# Task: Close PR 36 MVP release defects

## Goal
Resolve every reproducible issue in the post-PR-36 audit so the branch passes the Linux, Windows cross-build, installer, formatting, and memory-safety release gates.

## Context
- Relevant docs/ADR: `docs/architecture.md`, `docs/product.md`, `docs/source-layout.md`, `docs/api-contracts.md`, `docs/whisper-api.md`, `docs/test-strategy.md`, `docs/roadmap.md`, `docs/issues.md`.
- VLC/worker/protocol version affected: VLC 3.0.x plugin and worker; protocol receives a compatible minor STARTED-session correlation extension if required.
- Assumptions and explicit non-goals: preserve local-only operation and VLC playback; no new UI, cloud service, telemetry, or unrelated refactor. `graphify-out/` predates the branch and is used as a navigation aid only; sources remain authoritative.

## Scope
- In scope: async translation invalidation; seek/timing and handshake failures; IPC authentication endpoint hardening; callback realtime safety; Windows Unicode/runtime and installer edge cases; test reliability; stale documentation and formatting defects.
- Out of scope: new captioning features, VLC 4 support, cloud transcription, and redesigns not required to close an audited failure.
- Files/components expected to change: plugin worker client/module, worker main/translation/source/download/configuration, protocol IPC/codec, CMake/NSIS packaging, focused tests, and mapped documentation.

## Design
- Inputs and outputs: reject invalid or oversized inputs explicitly; correlate session startup replies; emit only captions belonging to the current media epoch.
- Ownership/threading model: keep the VLC audio callback allocation-, lock-, logging-, IPC-, and inference-free; serialize worker control and delivery ownership without holding translation locks across IPC; make cancellation ownership explicit.
- Bounds, time units, and failure behavior: retain signed 64-bit media microseconds for captions; fail closed on monotonic-clock errors, IPC/authentication failures, truncated paths/URLs, and invalid packages while VLC playback continues.
- Privacy/security implications: use unpredictable, owner-only local IPC endpoints with cleanup and peer checks where supported; do not add network paths or persistent media/transcript data.
- Protocol change: compatible minor if STARTED session correlation requires an added field; decoder compatibility is retained where practical.

## Acceptance criteria
- [x] Every active item in the PR-36 follow-up ledger is fixed or disproved with a regression test and documented evidence.
- [x] Failure behavior preserves VLC playback.
- [x] Limits/validation are explicit.
- [x] Automated tests cover success and failure.
- [x] Documentation/version metadata updated.

## Test plan
Run `clang-format --dry-run --Werror` on all changed C/H files; `git diff --check`; `cmake --preset linux-x64-debug`; `cmake --build --preset linux-x64-debug`; `ctest --preset linux-x64-debug --output-on-failure`; `ctest --test-dir build/linux-x64-debug -T memcheck`; MinGW CPU and GPU release configure/builds; direct CPack and NSIS installer targets, including a path containing spaces where practical. Inspect installer contents and run focused tests repeatedly to catch destructive or ordering failures. A Windows VM is not available, so native Windows UI/runtime behavior remains cross-build and static-contract verified.

## Definition of done
- [x] C17 code; no project-authored C++ introduced.
- [x] No blocking work in VLC audio callback.
- [x] No unapproved network access, telemetry, transcript/PCM persistence, or sensitive logs introduced; any approved egress is documented and opt-in.
- [x] Memory, audio queue, frame, text, and retry limits are bounded.
- [x] Error path is safe: captions may stop, playback does not.
- [x] Unit/contract/integration tests pass as applicable.
- [x] Formatting, warnings-as-errors, and static checks pass.
- [x] Protocol contract and compatibility version updated if needed.
- [x] `docs/decisions.md`, roadmap, and AI context updated when assumptions change.
- [x] Reviewer can reproduce the result from a clean checkout.

## Evidence
- Build/test outputs: Linux debug configure/build and 26/26 CTest pass; Valgrind executes 25 tests with the documented
  model-heavy test skipped, reports no defects, and `vw_memcheck_gate` confirms 26 clean logs. Windows CPU and GPU
  release cross-builds pass. Both NSIS variants pass staged hash/input checks, direct GPU CPack emits the ZIP with CPU
  fallback, and a CPU-only NSIS installer builds successfully from a build path containing spaces.
- Hygiene: `clang-format --dry-run --Werror` on every changed C/H file and `git diff --check` pass.
- Measured performance (if relevant): realtime callback remains constant-time bounded queue work only.
- Known limitations/follow-ups: native Windows VLC/installer smoke testing requires a Windows host or VM.
