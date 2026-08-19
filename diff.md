# Part 3 — Step 17d: Seek Re-Sync Engine & Discontinuity Discrimination (vs gemini/milestone-3)

**22 files changed, +813 / -86**
**Base**: `gemini/milestone-3` = `ce042e5` (17b PR #10 + 17c PR #11 merged); branch `gemini/milestone-3-step-17d`
**Commits**: `1ece3c3` (feat), `d8f44ac` (fix), `40c416b` (docs)
**Line references**: branch HEAD (`40c416b`).

---

## 1. File-by-File Analysis

### 3.1 `protocol/include/vw_protocol_types.h` / `vw_protocol_util.h` (new)

**Why change**: Step 17d protocol hardening. v1.1→v1.2: `MINOR 2U`, `VW_CLIENT/WORKER_VERSION 1.2.0`, `vw_msg_started_t { uint8_t source_active }`, `VW_MSG_STARTED_PAYLOAD_BYTES 1U`, `E_SOURCE_OPEN`. New `vw_protocol_util.h` = `vw_saturating_add_i64/sub_i64` (via `__builtin_add/sub_overflow`).
**Responsibility**: wire format + the only overflow guards for PTS math.
**Acceptance map**: plan criteria 3 (saturating) + 4 (validation) + 6 (source_active) → **Done**.

### 3.2 `protocol/src/vw_protocol_codec.c`

**Why change**: STARTED 1-byte `source_active` encode/decode; positional encode.
**Boundaries**: encode/decode symmetric; 0-byte STARTED still decodes (back-compat).

### 3.3 `protocol/src/vw_protocol_validate.c`

**Why change**: `VW_MSG_POSITION` bounds — `current_pts_us`/`input_time_us` in [−10 s, 10 yr], `isfinite` + `(0,16]` rate, flag bitmask `(SEEK|PAUSED)` only.
**Boundaries**: NaN/±Inf rejected via `!isfinite`; unknown flag bits rejected; `CAPTION_SEGMENT` UTF-8/control checks unchanged.
**Acceptance map**: criterion 4 → **Done**.

### 3.4 `worker/src/vw_worker.c`

**Why change**: Seek re-sync engine — POSITION-driven demuxer re-seek without teardown, seek coalescing, in-session START (media swap), STARTED `source_active`, `E_SOURCE_OPEN` on open failure, saturating PTS math.
**Responsibility**: main loop: POSITION(SEEK) → session_id memcmp → `vw_source_decoder_seek` + clear audio_buf + evict builder + reset `decoded_pts_us` (saturating); pause/resume; look-ahead decode (30 s lead); EOF handling.
**Happy path**: POSITION(SEEK, media) → coalesce (`target != last_playback_pts_us` guard) → re-seek → decode resumes.
**Failure path**: extreme POSITION values now rejected at the validator; `samples_read==0` is unrecoverable EOF (transient-zero gap).
**Boundaries**: `paused` set from `VW_POSITION_FLAG_PAUSED` with no else-reset; flag-pause does not clear `audio_buf` (only `VW_MSG_PAUSE` does); RESUME does not re-anchor `decoded_pts_us`; backward threshold 2 s vs 500 ms policy (see Findings).
**Acceptance map**: criteria 2, 3, 5, 6 → **Done**.

### 3.5 `plugin/src/vw_whisper_module.c`

**Why change**: Seek/seek re-anchoring, media swap poll, `source_active` PCM gating, pause backlog drop, bounded worker respawn.
**Responsibility**: sender: input state poll (100 ms) → PAUSE/RESUME on transition, POSITION pacing, media-swap detection (`input_GetItem`/`input_item_GetURI` vs `active_source_url`), discontinuity re-anchor, respawn (3×, 1 s cool-down); realtime callback: 5 s forward / 500 ms backward discontinuity gate (system-date PTS, **never stored as the seek target**); segment render gated on `!paused`.
**Happy path**: pause → blank + `PLUGIN_PAUSED_DROP`; seek → blank + `POSITION(SEEK, media_position)`; death → respawn (fresh client, filtered MRL, paused re-applied).
**Failure path**: no media position at discontinuity → blank without re-anchor (`PLUGIN_SEEK_TARGET_MISSING`); respawn exhaustion → permanent passthrough.
**Acceptance map**: criteria 1, 2, 7, 8 → **Done**.

### 3.6 `plugin/src/vw_worker_client.c` / `vw_worker_client.h`

**Why change**: `start_session` guards `session_active` (reset by `stop_session`/`drop_transport`); STARTED `source_active` exposed via `vw_worker_client_is_source_active`.
**Boundaries**: guard is defensive (all start_session calls use fresh clients); `send_position` forwards `current_pts_us` verbatim (validated upstream).

### 3.7 `plugin/src/vw_caption_presenter.c`

**Why change**: Lead computation now saturating (`vw_saturating_sub/add_i64`), rate-scaled, 60 s cap; blank/OSD fallback unchanged.
**Boundaries**: media-domain diff / rate, cap 60 s.

### 3.8 Tests (`tests/CMakeLists.txt`, `test_protocol_util.c` [new], `test_protocol_codec.c`, `test_protocol_validate.c`, `test_caption_presenter.c`, `test_worker_lifecycle.c`, `test_worker_ipc.c`)

**Why change**: `test_protocol_util` (INT64_MAX/MIN saturation boundaries); STARTED round-trip; POSITION bounds/flag cases; presenter lead rate-scaling; seek test; `vw_platform_sleep_ms` (replaced `usleep` in the fix commit — restored here).
**Coverage note**: media-swap test uses synthetic silence + live-mode STOP→START, asserts non-zero exit only (silent-pass risk — see Findings).

### 3.9 Docs (`docs/step17d_plan.md`, `docs/api-contracts.md`, `docs/architecture.md`, `docs/roadmap.md`, `docs/source-layout.md`, `diff.md`)

**Why change**: Step 17d plan (protocol v1.2, saturating helpers, validation, media swap, PCM gating, respawn item 6 + acceptance); contract/architecture/roadmap/source-layout updated (Rule 14); `diff.md` = this artifact.

---

## 2. Happy-Path Request Trace (source mode, local file)

1. Play file → sender init: capability gate + `file://`/absolute-path filter → `source_url` (module) → `start_session`.
2. Worker: START → demuxer open → `source_mode=true` → `STARTED(source_active=1)` → look-ahead decode (30 s lead, saturating math).
3. Segments → `show_segment` (media-domain lead, saturating, 60 s cap) → SPU channel 9.
4. **Pause**: poll `PAUSE_S` → blank + `pause_session`; backlog dropped (`PLUGIN_PAUSED_DROP`).
5. **Seek**: poll forward ≥5 s / backward >500 ms (or callback gate) → blank + `POSITION(SEEK, media_position)` → worker re-seeks (coalesced) → resumes.
6. **Resume**: blank + `resume_session`; paused-seek detector re-anchors if position jumped.
7. **Transport death**: receive fatal → respawn (3×, fresh client, filtered MRL, paused re-applied) → captions resume.

## 3. Most Important Failure Path

**Transport death → respawn**: `receive_frame` fatal (pipe framing desync) → `drop_transport` closes the plugin's pipe end → worker reads EOF → exits. The sender's respawn path disconnects (waits ≤5 s for worker exit, freeing the pipe name), relaunches with the same pipe/auth/model, re-extracts the MRL (filtered), blanks the presenter, restarts the session, re-applies paused state — bounded at 3 attempts before permanent passthrough. No VLC playback impact (passthrough preserved).

## 4. Boundary Summary

| Boundary             | Implementation                                                                   | Status |
| -------------------- | -------------------------------------------------------------------------------- | ------ |
| **Input validation** | POSITION bounds/rate/flags; CAPTION UTF-8/control; STARTED payload length        | Done   |
| **Authorization**    | HELLO token + first-frame enforcement (unchanged)                                | OK     |
| **Concurrency**      | Callback atomics only; sender single-threaded; respawn after disconnect          | OK     |
| **I/O**              | 3 s transport bound; 5/20 ms cadence; send-loop breaks on death                  | OK     |
| **Memory**           | Saturating helpers guard int64 PTS; chunk inline buffers; respawn `strdup` freed | Done   |
| **Lifetime**         | Respawn 3×/1 s, paused re-applied; session_id gating; vout held refs             | OK     |
| **Persistence**      | Local IPC pipe/socket; cleanup on close                                          | OK     |

## 5. Acceptance Criterion → Code Mapping (plan `step17d_plan.md`)

| #   | Criterion                                           | Status                                                                |
| --- | --------------------------------------------------- | --------------------------------------------------------------------- |
| 1   | 5 s forward-jitter gate, both detectors             | Done                                                                  |
| 2   | True seek re-sync, media-domain target, no teardown | Done                                                                  |
| 3   | Saturating arithmetic — no overflow/stall           | Done                                                                  |
| 4   | Protocol validation: bounds, rate, flags            | Done                                                                  |
| 5   | Playlist media swap                                 | Done                                                                  |
| 6   | PCM gating in source mode (`source_active`)         | Done                                                                  |
| 7   | SPU anti-ghosting (seek + pause)                    | Done                                                                  |
| 8   | Worker respawn (bounded, current MRL)               | Done                                                                  |
| 9   | Zero memory leaks                                   | 0 definite/indirect/possible; glib/libgomp still-reachable noise only |
| 10  | Documentation (Rule 14)                             | Done                                                                  |

**All 10 criteria Done — Step 17d complete.**

## 7. Code Review Findings

### Bugs (worker-side latent robustness — masked by the plugin flow)

| Priority   | Component / Location                      | Description                                                                                                                                                                               | Impact                                                                                     | Proposed Fix                                                         |
| ---------- | ----------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------ | -------------------------------------------------------------------- |
| **Medium** | `worker/src/vw_worker.c` POSITION handler | `paused=true` set from `VW_POSITION_FLAG_PAUSED` with no else-reset; flag-pause does not clear `audio_buf`/builder (only `VW_MSG_PAUSE` does); RESUME does not re-anchor `decoded_pts_us` | Ghost/duplicate captions or permanent decode suspension for a non-plugin POSITION consumer | Clear buffer on flag-pause; else-reset `paused`; re-anchor on RESUME |
| **Medium** | `worker/src/vw_worker.c` look-ahead read  | `samples_read == 0` treated as unrecoverable EOF with no transient/retry path                                                                                                             | Caption loss on transient decoder zero until a seek                                        | Retry/backoff on transient zero                                      |
| **Low**    | `worker/src/vw_worker.c`                  | Backward-jump threshold `2000000LL` (2 s) inconsistent with the 500 ms/5 s policy                                                                                                         | Late backward-seek detection                                                               | **Resolved**: aligned to `500000LL` (500 ms)                         |

### Architectural & Operational Risks

| Category              | Risk Description                                                                      | Affected Files              | Mitigation                                            |
| --------------------- | ------------------------------------------------------------------------------------- | --------------------------- | ----------------------------------------------------- |
| **Protocol drift**    | v1.2 is committed; any future branch must not reintroduce the v1.1 reversion          | protocol/\*, worker, plugin | Pin v1.2 in the plan/contracts                        |
| **Worker robustness** | Pause/resume/EOF gaps are latent — safe while the plugin drives POSITION/PAUSE/RESUME | worker.c                    | Harden worker state transitions independently         |
| **Test silent-pass**  | Media-swap/seek tests assert non-zero exit, not caption output                        | test_worker_lifecycle.c     | Add caption/re-sync assertions + in-test WAV fixtures |

### Code Style & Quality Nitpicks

| Issue Type        | File & Line                  | Description                                                                                                 | Status                                                   |
| ----------------- | ---------------------------- | ----------------------------------------------------------------------------------------------------------- | -------------------------------------------------------- |
| **Comment drift** | `vw_whisper_module.c` struct | `resume_pts_us` comment "PTS anchor of the first post-seek block" no longer accurate (callback stores none) | **Resolved**: updated to "media position set by poll detectors" |

---

_End of Part 3. Part 1 = 17b (PR #10), Part 2 = 17c (PR #11), Part 2b = 17c scout audit, Part 3 = 17d (branch `gemini/milestone-3-step-17d`, commits `1ece3c3`/`d8f44ac`/`40c416b`). All line refs target branch HEAD._
