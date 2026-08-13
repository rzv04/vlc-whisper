# Step 17 Restart Mechanism Deprecation Plan

# Task: Replace the STOP→START seek restart with a dedicated SEEK/POSITION message

## Goal
Retire the MVP per-seek full session restart (plugin `STOP(SEEK_DISCONTINUITY)` → drain SPSC →
`START(new session_id, timeline_origin)` → 8s window refill) in favor of a lightweight,
fire-and-forget seek message that re-anchors the epoch without a handshake or teardown — so
look-ahead transcription (17c/17d) can produce near-instant post-seek captions.

## Context
- **Relevant docs/ADRs**: `docs/architecture.md:75` (shipped MVP policy + placeholder note),
  `docs/plans/step17_plan.md` (assumptions note), `docs/roadmap.md:53-55` (17c `POSITION` lead
  pacing, 17d epoch restarts + `VW_INPUT_JUMP_DISCONTINUITY_US = 5s` + `is_seeking`),
  `docs/plans/milestone3_postmortem.md` Phase D (worker FFmpeg/MF demuxer, `av_seek_frame` /
  MF `SetCurrentPosition`, "re-anchor seek detector and media-system offset", Model-Once
  `ADR-015`).
- **VLC/worker/protocol version affected**: protocol goes to v1.1 (`VW_CAPABILITY_SOURCE_MODE`,
  `source_url`, `POSITION`). VLC 3.0.23 unchanged.
- **Assumptions and explicit non-goals**:
  - This deprecation ships WITH 17c/17d, not before — the MVP restart is correct for
    local-file milestone-3 acceptance and must not be destabilized by premature removal.
  - The worker-side demuxer re-seek (`av_seek_frame` / MF `SetCurrentPosition`) is assumed
    available once 17c's source decoder lands; the seek message design must not block on it.
  - Non-goal: re-designing session-ID validation (that is 17d's own item); this plan only
    changes HOW the epoch boundary is signaled.
  - Non-goal: solving jitter/flag false-positives here; the 5s jump gate is 17d's.

## Scope
- **In scope**:
  - Protocol v1.1: new `VW_MSG_SEEK` (or reuse 17c `POSITION`) — plugin→worker, carries
    `session_id` + target `pts_us`; worker re-seeks its demuxer, clears the analysis window and
    builder, bumps an epoch marker WITHOUT a STARTED handshake or new `session_id`.
  - Plugin: seek path sends the seek message instead of STOP→START; drops the ≤5s blocking
    `start_session` from the seek path; keeps OSD blank, SPSC drain, atomics detection.
  - Worker: seek handler = internal demux re-seek + window/builder discard + epoch marker bump;
    `session_active` and transport stay live.
  - Docs: roadmap 17c/17d wording already anticipates; update architecture seek policy + this
    plan to "shipped replacement" when done.
- **Out of scope**: SPU flush (17b), GPU (17a), quality (17e), session-ID validation (17d),
  MF process-wide lifecycle (17c).

## Design
- **Inputs and outputs**: seek signal (existing atomics/position poll) → `VW_MSG_SEEK {session_id,
  pts_us}` → worker re-seek + epoch reset → same `session_id` continues, captions resume from the
  demuxer's look-ahead buffer (near-instant) instead of after an 8s refill.
- **Ownership/threading model**: same as today — callback only sets atomics; sender thread sends
  the seek message (fire-and-forget, no response wait); worker main loop handles it. `session_id`
  is NOT regenerated, so in-flight stale segments remain gated by the existing memcmp (17d may
  tighten this with explicit epoch ids).
- **Bounds, time units, and failure behavior**: no handshake → no 5s stall; sender never blocks
  on seek. If the seek message write fails, reuse the existing fail-closed drop (transport dead →
  passthrough). Worker demux re-seek failure → `ERROR` (recoverable=1) → plugin keeps session.
- **Privacy/security implications**: none (local IPC).
- **Protocol change**: minor breaking → v1.1 (`VW_CAPABILITY_SOURCE_MODE` capability flag gates
  source mode; `POSITION`/`SEEK` message added).

## Migration path (kept → deprecated)
| Keep (reused by 17c/17d) | Deprecate (removed with this plan) |
|---|---|
| `session_id` epoch gating on AUDIO/SEGMENT | `STOP(SEEK_DISCONTINUITY)` per seek |
| Builder + `audio_buf` discard at epoch boundary | `START` handshake in the seek path |
| Realtime-safe atomics detection (callback) | `timeline_origin_pts_us` as seek anchor carrier (worker ignores it today) |
| Position-jump poll → 17d clock-jump detector | Unconditional restart on `BLOCK_FLAG_DISCONTINUITY` (needs 5s gate) |
| OSD blank/flush on seek | — |

## Acceptance criteria
- [ ] Seek (playing and paused) produces near-instant captions from the worker look-ahead buffer,
      not after an 8s refill, under 17c source mode.
- [ ] Rapid scrubbing causes no session teardown, no 5s stalls, no transport drops.
- [ ] Stale pre-seek segments/text still cannot cross the epoch boundary (gating intact).
- [ ] Jitter/network re-buffer does not clear captions (17d 5s gate).
- [ ] Local-file milestone-3 behavior unchanged where the MVP restart is still active.

## Test plan
- Protocol codec unit tests for `VW_MSG_SEEK`/`POSITION` encode/decode.
- Worker integration: seek message → demux re-seek → window/builder cleared → next caption at
  target PTS; session_id unchanged across seeks.
- Plugin integration: rapid seek sequence → no `PLUGIN_SESSION_RESTART_FAIL`, no worker_dead.
- Live VLC manual: seek playing/paused, rapid scrub, VOD re-buffer jitter.
- Full AGENTS.md gate (format, build, ctest, memcheck, Windows cross-build).

## Definition of done
- [ ] C17; no C++; no blocking work in VLC audio callback
- [ ] No network access, telemetry, transcript/PCM persistence, or sensitive logs
- [ ] Bounded memory/queue/frame/text/retry limits
- [ ] Error path safe: captions may stop, playback does not
- [ ] MVP restart fully removed from the seek path; kept primitives documented
- [ ] Roadmap 17c/17d marked shipped; this plan updated to reflect the replacement
