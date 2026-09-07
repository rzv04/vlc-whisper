# VLC-Whisper Agent Context

Keep this file small. Root `AGENTS.md` is the policy source of truth; `docs/README.md` routes technical references; `docs/invariants.md` defines cross-component contracts.

## Mission

Generate private, realtime captions in VLC while protecting playback reliability. Transcription runs locally in an isolated worker; optional finalized-text translation and explicit model downloads use documented worker network paths.

## Architecture in one view

`VLC audio callback -> bounded plugin queue -> plugin sender/client -> authenticated local IPC -> worker -> VAD/Whisper -> timed caption -> plugin presenter`

Seekable local media may use worker-side source decoding/look-ahead. The plugin owns VLC integration; the worker owns inference; `protocol/` owns the wire/transport contract.

## Non-negotiables

- Project C is C17; pinned third-party code stays isolated.
- Audio callback work is bounded/non-blocking: no inference, IPC/filesystem I/O, blocking lock/wait, or unbounded allocation.
- Media/caption timestamps are signed 64-bit microseconds; gaps and discontinuities remain explicit.
- Authenticated local IPC; no cloud transcription, telemetry, or transcript/PCM persistence.
- Identity values reject overflow rather than truncate.
- Session state has explicit reset/finalize ownership.
- Distinct transient/EOF/error states stay distinct.
- Caption failure may stop captions, never playback.

## Agent reading order

1. Root `AGENTS.md`.
2. Changed code + callers/consumers.
3. `docs/invariants.md`.
4. Only the relevant reference selected via `docs/README.md`.
5. Exact dependency pin/source when external semantics matter.

Do not load historical plans, the full roadmap, decision log, issue ledger, or all API references unless the task actually depends on them.
