# Step 17 Restart Mechanism Deprecation Plan

# Task: Replace the STOP→START seek restart with a dedicated SEEK/POSITION message

## Goal
Retire the MVP per-seek full session restart (plugin `STOP(SEEK_DISCONTINUITY)` → drain SPSC →
<<<<<<< HEAD
`START(new session_id, timeline_origin)` → playback-rate 8s window refill) in favor of a
lightweight, fire-and-forget seek message that re-anchors the epoch without a handshake or
teardown. The 8s analysis window (`VW_WINDOW_SAMPLES`) is a hard batch-transcribe constraint
that look-ahead does not remove — what changes is the refill RATE:

- Seek inside the worker's decoded look-ahead horizon: no refill (audio already decoded, maybe
  already transcribed) → caption after inference only (≈1–4s), or instantly if pre-transcribed.
- Seek forward past the horizon, or backward (demuxer re-seek + re-decode): refill at demux/
  decode speed (CPU decode typically 2–5× realtime) → ≈8s÷2–5 + inference (≈1.6–4s), NOT zero.
- MVP baseline: refill at PLAYBACK rate (VLC delivers post-seek audio in real time) → 8s +
  inference.

So "near-instant" is only true inside the horizon; outside it, look-ahead strictly beats the MVP
restart but still pays a decode-rate 8s-sample refill. Horizon depth (17c `POSITION` lead) is the
tunable that decides how many seeks fall in the near-instant bucket.
=======
`START(new session_id, timeline_origin)` → 8s window refill) in favor of a lightweight,
fire-and-forget seek message that re-anchors the epoch without a handshake or teardown — so
look-ahead transcription (17c/17d) can produce near-instant post-seek captions.
>>>>>>> 0335b0f483d6d970d8bd4c94c9b81ded02bfbca1

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
<<<<<<< HEAD
  pts_us}` → worker re-seeks its demuxer, clears the analysis window and builder, bumps an epoch
  marker → same `session_id` continues. Caption latency after the seek depends on whether the
  target overlaps the decoded look-ahead horizon: inside it → near-instant (inference only);
  outside it (forward past horizon or backward) → an 8s-sample window refills at demux/decode
  speed (≈2–5× realtime → ≈1.6–4s + inference), which still beats the MVP's playback-rate 8s
  refill. The seek message itself adds no refill of its own — it only avoids the STOP→START
  handshake, teardown, and session regeneration that the MVP restart pays on top.
=======
  pts_us}` → worker re-seek + epoch reset → same `session_id` continues, captions resume from the
  demuxer's look-ahead buffer (near-instant) instead of after an 8s refill.
>>>>>>> 0335b0f483d6d970d8bd4c94c9b81ded02bfbca1
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
<<<<<<< HEAD
- [ ] Seek INTO the decoded look-ahead horizon (playing and paused, 17c source mode) produces
      captions after inference only — no window refill, no handshake, no session teardown.
- [ ] Seek OUTSIDE the horizon (forward past it or backward) refills the 8s window at demux/decode
      speed (≈2–5× realtime) with no STOP→START round-trip, strictly faster than the MVP
      playback-rate refill.
=======
- [ ] Seek (playing and paused) produces near-instant captions from the worker look-ahead buffer,
      not after an 8s refill, under 17c source mode.
>>>>>>> 0335b0f483d6d970d8bd4c94c9b81ded02bfbca1
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
