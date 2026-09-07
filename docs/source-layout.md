# Source Layout

This document defines **ownership boundaries**, not an exhaustive file inventory. Use repository search/tree for individual files; duplicating the full tree here becomes stale and wastes context.

## Process/component ownership

| Area | Owns | Must not own |
| --- | --- | --- |
| `plugin/` | VLC lifecycle, decoded PCM capture, bounded handoff, worker supervision/client, caption presentation, plugin-side session metrics | Whisper inference, VAD policy, persistent transcripts |
| `worker/` | Session execution, source decoding, VAD/audio windows, Whisper inference, final segments, translation, model download, worker metrics | VLC callback/render internals |
| `protocol/` | Versioned message/frame types, encode/decode/semantic validation, transport abstraction, shared diagnostics | VLC/Whisper application policy |
| `tests/` | Unit/contract/seam/integration/E2E verification and support harnesses | Production behavior |
| `tools/quality_benchmark/` | Headless local corpus acquisition/orchestration, 1x modes, WER/CER reporting | Shipped media fixtures, VLC rendering, production telemetry |
| `models/` | Local model files/manifests used for development/packaging | Runtime application policy |
| `cmake/` | Build, packaging, toolchain and verification helpers | Runtime feature logic |
| `docs/` | Canonical engineering/product reference | Generated runtime state |
| `ai/`, `.agents/` | Agent plan/context/optional skills | Duplicate policy; root `AGENTS.md` is canonical |

All project-authored C is C17. The plugin does not link Whisper; the worker owns the pinned `whisper.cpp` dependency.

## Key plugin responsibilities

- `vw_whisper_module.*`: VLC module lifecycle and orchestration.
- `vw_audio_capture.*`: realtime-adjacent PCM capture/normalization.
- `vw_queue.*`: bounded plugin audio handoff.
- `vw_worker_client.*`: worker launch, authenticated IPC client, session/control frames.
- `vw_caption_presenter.*`: generated caption scheduling/rendering.
- `vw_session.*` and related module state: plugin-side lifecycle state.
- `vw_benchmark.*`: bounded plugin/session measurement; never realtime filesystem work.
- platform files: OS-specific process/path/time/random primitives.

**Boundary:** VLC audio callbacks only capture/normalize/enqueue bounded data. Inference, blocking IPC, filesystem work, and blocking waits are outside the callback.

## Key worker responsibilities

- `vw_worker.*`: authoritative worker session/control state machine.
- `vw_worker_queue.*`: bounded IPC-reader to worker-loop handoff.
- `vw_source_decoder*`: native source read/seek and explicit decoder status semantics.
- `vw_audio_buffer.*`: PCM/timeline buffer; discontinuities must be explicit.
- `vw_vad.*`: speech boundary policy.
- `vw_whisper_engine.*`: exact pinned Whisper model/inference owner.
- `vw_segment_builder.*`: finalized caption construction/deduplication.
- `vw_translate*`: optional finalized-text translation path.
- `vw_model_download*` / catalog/hash helpers: explicit verified model provisioning.
- worker config/process policy files: startup identity, backend, paths, process behavior.

**Boundary:** worker state that is session-scoped must reset/finalize through an authoritative lifecycle path. Decoder EOF/AGAIN/error meaning belongs to the decoder contract, not caller heuristics.

## Protocol responsibilities

- `vw_protocol_types.h`: wire identities, message payload types, version/capability constants.
- codec files: deterministic encode/decode only.
- validator: structural **and semantic** message validation.
- IPC transports: platform-specific connection/send/receive semantics.
- logging: privacy-safe diagnostics and synchronized sink/file ownership.

Identity values crossing the protocol are reject-on-overflow; producers must not truncate before a downstream validator can reject them.

## Tests

- `tests/unit/`: local logic/API contracts.
- `tests/integration/`: cross-component/process/lifecycle seams.
- `tests/support/`: reusable harnesses/stubs; no production logic.
- `tests/e2e/`: real VLC/manual or environment-heavy acceptance procedures.
- `tests/include/vw_test.h`: common assertions plus PR #50-style named accumulating contract checks.
- `tests/quality/` and `tools/quality_benchmark/`: network-free helper tests plus developer-only local WER/CER tooling.

See `test-strategy.md` for when a seam test is mandatory.

## Documentation map

Start with `docs/README.md`; it routes to the smallest relevant reference. Do not enumerate every source/header in documentation unless the list itself is a stable contract.

## Adding or moving code

Before adding a new component:

1. Put behavior with its authoritative owner rather than the most convenient caller.
2. Define producer -> boundary -> consumer -> lifecycle owner.
3. Define failure and reset semantics before adding fields/APIs.
4. Keep identities untruncated and metrics single-owner.
5. Add/adjust seam tests when ownership crosses a boundary.
6. Update this file only if directory/component ownership changed; ordinary file additions do not require tree churn.
