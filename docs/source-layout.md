# Source Layout

This file defines ownership boundaries, not a file inventory. Use repository search/tree for individual files.

| Area | Owns | Must not own |
| --- | --- | --- |
| `plugin/` | VLC lifecycle, PCM capture, bounded handoff, worker client/supervision, caption presentation, plugin metrics | Whisper/VAD policy, persistent transcripts |
| `worker/` | Session execution, source decode, VAD/windows, Whisper inference, segments, translation, model download, worker metrics | VLC callback/render internals |
| `protocol/` | Versioned frames/messages, codec/validation, transport abstraction, shared diagnostics | VLC/Whisper policy |
| `tests/` | Unit/contract/seam/integration/E2E verification and harnesses | Production behavior |
| `tools/quality_benchmark/` | Headless corpus acquisition/orchestration, 1x quality modes, WER/CER | VLC rendering, shipped fixtures |
| `models/` | Local model files/manifests | Runtime policy |
| `cmake/` | Build, packaging, toolchains, verification helpers | Runtime features |
| `docs/` | Canonical engineering/product reference | Generated state |
| `ai/`, `.agents/` | Task context and optional skills | Duplicate policy; root `AGENTS.md` is canonical |

All project-authored C is C17. The plugin does not link Whisper; the worker owns pinned `whisper.cpp`.

## Critical boundaries

**Plugin callback:** capture/normalize/enqueue bounded data only. No inference, blocking IPC/locks, filesystem work, or unbounded allocation.

**Worker lifecycle:** `vw_worker.*` owns caption-session state; decoder EOF/AGAIN/error semantics belong to the decoder API, not caller heuristics.

**Protocol:** codecs serialize; validators enforce structural and semantic contracts. Identity values must be rejected before any truncating copy.

**Tests:** `tests/unit/` covers local contracts; `tests/integration/` covers cross-component/process/lifecycle seams; `tests/support/` contains harnesses only; `tests/e2e/` covers real VLC/environment-heavy acceptance. `tests/include/vw_test.h` provides PR #50-style named accumulating checks.

## Adding or moving code

1. Put behavior with its authoritative owner, not the most convenient caller.
2. Map producer -> boundary -> consumer -> lifecycle owner.
3. Define failure/reset semantics before adding fields/APIs.
4. Keep identities untruncated and metrics single-owner.
5. Add/adjust seam tests when ownership crosses a boundary.
6. Update this file only when directory/component ownership changes.
