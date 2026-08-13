# Diff Analysis: gemini/milestone-3-step-17 vs gemini/milestone-3

**19 files changed, +328 / -579 lines**
**Base**: `gemini/milestone-3`

---

## 1. File-by-File Analysis

### 1.1 `docs/api-contracts.md`

**Why change**: Clarify the STOP control contract to name the `VW_CTRL_REASON_SEEK_DISCONTINUITY` constant and its trigger (seek/discontinuity → fresh START epoch), per step 17 plan Scope/Acceptance. No wire change — STOP idempotency and reason values were already documented.

**Responsibility before**: Documented STOP reasons as `USER_STOP=1`, `SEEK_DISCONTINUITY=2`, `MEDIA_END=3` with idempotent semantics. **After**: Same, but `SEEK_DISCONTINUITY=2` is now explicitly bound to `VW_CTRL_REASON_SEEK_DISCONTINUITY` and annotated "sent on seek/discontinuity before a fresh START epoch" (`docs/api-contracts.md:87`). Adds traceability to the constant introduced in `protocol/include/vw_protocol_types.h`.

**Callers**: Documentation consumer only — plugin (`vw_whisper_module.c:307`), worker (`vw_worker.c:402`), tests. **Callees**: None.

**Happy path**: Reader follows CONTROL MESSAGES table → sees STOP reason 2 is `SEEK_DISCONTINUITY` → matches `protocol/include/vw_protocol_types.h:143` → understands plugin seek restart sends STOP(2) then START.

**Failure path**: Stale docs would leave reason 2 magic-numbered; a contributor could send STOP(0) on seek and defeat worker builder-discard/session gating. Now the named constant is discoverable.

**Boundaries**:

| Boundary type | What to check | Status |
|---|---|---|
| Input validation | Reason value range documented | ✅ values 1/2/3 enumerated |
| Authorization | N/A (doc) | — |
| Concurrency | N/A | — |
| I/O | N/A | — |
| Persistence | N/A | — |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | STOP reason SEEK_DISCONTINUITY documented with constant | `docs/api-contracts.md:87` | — | ✅ done |

**Assumptions/Tradeoffs**: Assumes `protocol/include/vw_protocol_types.h` is the canonical source — docs mirror it. No tradeoff; one-line doc fix.

---

### 1.2 `docs/architecture.md`

**Why change**: Promote the MVP seeking & discontinuity policy from aspirational to shipped, matching the step 17 implementation, per AGENTS.md rule 14 (docs must be updated in the same change).

**Responsibility before**: Described policy generically: "clear captions, send STOP(SEEK_DISCONTINUITY), reset SPSC queue & VAD, initialize new epoch" (`docs/architecture.md:75`). **After**: Adds "(shipped, step 17)" and makes the mechanism precise: "discards the SPSC queue, starts new session epoch (new `session_id`, `timeline_origin_pts_us` = post-seek PTS anchor), detection is realtime-safe (atomics only in filter callback; teardown/restart on sender thread)."

**Callers**: Architecture readers, reviewers. **Callees**: None.

**Happy path**: Reader lands on session/epoch section → sees shipped seek policy → cross-checks `plugin/src/vw_whisper_module.c:470-474` (atomics in callback) and `:304-321` (sender restart).

**Failure path**: If architecture stayed vague ("reset SPSC queue"), a future editor could reintroduce `vw_spsc_queue_reset()` (which does not exist — see `docs/vlc-api-essentials.md` fix), causing a build break.

**Boundaries**: N/A — doc only.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Architecture reflects shipped seek behavior | `docs/architecture.md:75` | — | ✅ done |

**Assumptions/Tradeoffs**: Assumes step 17 is the final non-look-ahead seek behavior; look-ahead changes (17c/17d) will extend it. Tradeoff: wording is now implementation-specific (atomics/sender thread) — appropriate for milestone 3.

---

### 1.3 `docs/plans/step-14-realtime-pcm-streaming.md` (deleted)

**Why change**: Housekeeping — this split-plan stub (14a/14b/14c overview, 135 lines) was superseded by the shipped `step14c_plan.md`/`step15_plan.md`/`step16_plan.md` chain and is no longer referenced by `docs/roadmap.md` or `docs/plans/milestone3_postmortem.md`. Deleting stale plans prevents onboarding confusion.

**Responsibility before**: Planning artifact describing the 14a/14b/14c split and context. **After**: Deleted (0 lines).

**Callers**: None (historical). **Callees**: None.

**Happy path**: `git log -- docs/plans/` no longer surfaces a dead stub when searching for step 14 context.

**Failure path**: Stale plan left behind → a new contributor plans 14c work against the stub instead of the shipped code.

**Boundaries**: N/A.

**Acceptance map**: N/A — repo hygiene, not a step 17 criterion.

**Assumptions/Tradeoffs**: Assumes no external link points to this path. Tradeoff: git history still retains it.

---

### 1.4 `docs/plans/step14c_plan.md` (deleted)

**Why change**: Same housekeeping — step 14c is shipped (`docs/roadmap.md:51` marked `[x]`). The plan (238 lines) is now historical.

**Responsibility before**: Detailed 14c plan (SPSC drain, sender thread, model discovery). **After**: Deleted.

**Callers**: None. **Callees**: None.

**Happy path**: —

**Failure path**: —

**Boundaries**: N/A.

**Acceptance map**: N/A.

**Assumptions/Tradeoffs**: Same as 1.3.

---

### 1.5 `docs/plans/step15_plan.md` (deleted)

**Why change**: Step 15 (presenter wiring) shipped; plan (81 lines) is historical.

**Responsibility before**: Plan for SEGMENT→OSD wiring. **After**: Deleted.

**Callers**: None. **Callees**: None.

**Happy path**: —

**Failure path**: —

**Boundaries**: N/A.

**Acceptance map**: N/A.

**Assumptions/Tradeoffs**: Same as 1.3.

---

### 1.6 `docs/plans/step16_plan.md` (deleted)

**Why change**: Step 16 (pause/resume) shipped; plan (96 lines) is historical.

**Responsibility before**: Plan for PAUSE/RESUME lifecycle. **After**: Deleted.

**Callers**: None. **Callees**: None.

**Happy path**: —

**Failure path**: —

**Boundaries**: N/A.

**Acceptance map**: N/A.

**Assumptions/Tradeoffs**: Same as 1.3.

---

### 1.7 `docs/plans/step17_plan.md` (added)

**Why change**: New planning artifact for this step, per AGENTS.md rule 9 (task template) and rule 15 (codebase inspection before planning). Captures goal, scope, design, acceptance, test plan, and DoD for seeking & discontinuity support.

**Responsibility before**: Did not exist. **After**: 112-line plan (`docs/plans/step17_plan.md:1-112`) defining: detection signals (`BLOCK_FLAG_DISCONTINUITY` + non-monotonic PTS fallback), threading model (callback atomics only; sender restart), worker builder discard on START, protocol constant, doc/test scope, and explicit non-goals (17b/17c/17d, SPU flush, GPU).

**Callers**: `docs/roadmap.md:51` (step 17), implementation (`plugin/src/vw_whisper_module.c`, `worker/src/vw_worker.c`), reviewer. **Callees**: References `docs/architecture.md:75`, `docs/api-contracts.md` STOP table, `docs/vlc-api-essentials.md` seek sequence.

**Happy path**: Contributor reads `docs/plans/step17_plan.md:58-75` Design → understands callback writes `_Atomic bool discontinuity_pending` + `_Atomic int64_t resume_pts_us` → sender does `blank → STOP(2) → drain SPSC → START(resume_pts_us)` → worker drains builder on START.

**Failure path**: If plan omitted the stale-builder hypothesis note (`docs/plans/step17_plan.md:30-32`), the worker discard (`worker/src/vw_worker.c:322-327`) would be missed and pre-seek text would be stamped with the new session_id and rendered post-seek.

**Boundaries**:

| Boundary type | What to check | Status |
|---|---|---|
| Input validation | BLOCK_FLAG_DISCONTINUITY + PTS fallback both documented | ✅ `docs/plans/step17_plan.md:19-21` |
| Concurrency | Rule 4 — callback only sets atomics | ✅ `docs/plans/step17_plan.md:61-68` |
| I/O | START may block 5s (existing) | ✅ `docs/plans/step17_plan.md:69-72` |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Plan covers scope/design/AC/test/DoD | `docs/plans/step17_plan.md:34-112` | — | ✅ done |
| 2 | Non-goals explicitly deferred (17b/c/d) | `docs/plans/step17_plan.md:18-29` | — | ✅ done |

**Assumptions/Tradeoffs**: Assumes `timeline_origin_pts_us` is forwarded but not yet consumed by worker (plan line 23). Tradeoff: plan punts SPU flush and session-ID validation on segments to 17d — correct single-sweep scope per postmortem.

---

### 1.8 `docs/roadmap.md`

**Why change**: Mark step 17 as shipped and record the step-17 observation that network discontinuities (re-buffer/jitter) also set `BLOCK_FLAG_DISCONTINUITY`, motivating the 5s jump gate planned for 17d.

**Responsibility before**: Step 17 unchecked (`- [ ] 17. Implement Seeking...`); step 17d had no observation. **After**: Step 17 checked with shipped note: "realtime callback sets atomics (flag or PTS-jump fallback, resume PTS anchor); sender thread clears OSD, STOP(SEEK_DISCONTINUITY), drain-discards SPSC, START with new session_id; worker discards stale segment-builder hypotheses on START; `VW_CTRL_REASON_SEEK_DISCONTINUITY` constant; STOP-reason unit test + STOP→START lifecycle restart test." (`docs/roadmap.md:51`). Step 17d gains observation sentence (`docs/roadmap.md:55`).

**Callers**: Roadmap readers, milestone tracking. **Callees**: None.

**Happy path**: Reader checks roadmap → sees 17 shipped → knows seek restart is live, jitter-flag nuance is tracked for 17d.

**Failure path**: If roadmap stayed unchecked, CI/release gating could treat seek as unimplemented and block milestone 3.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Roadmap 17 → [x] with shipped detail | `docs/roadmap.md:51` | — | ✅ done |
| 2 | 17d observation recorded | `docs/roadmap.md:55` | — | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.9 `docs/test-strategy.md`

**Why change**: Register the two new automated tests for step 17, per AGENTS.md rule 14.

**Responsibility before**: Listed tests through step 16. **After**: Adds `tests/unit/vw_test_worker_client.c (17): fake server decodes STOP and asserts reason == VW_CTRL_REASON_SEEK_DISCONTINUITY` and `tests/integration/test_worker_lifecycle.c (17): STOP(SEEK_DISCONTINUITY) → START (new session_id) → AUDIO → STOP → SHUTDOWN` (`docs/test-strategy.md:59-60`).

**Callers**: Test strategy readers. **Callees**: None.

**Happy path**: Reviewer verifies coverage for STOP-reason and restart cycle.

**Failure path**: Undocumented tests → coverage appears missing, reviewer requests duplicate work.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Test strategy updated for step 17 | `docs/test-strategy.md:59-60` | `tests/unit/vw_test_worker_client.c:164-169`, `tests/integration/test_worker_lifecycle.c:218-232` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.10 `docs/vlc-api-essentials.md`

**Why change**: Fix a non-existent API reference that would mislead implementers and break builds.

**Responsibility before**: Seek sequence step 3 said `vw_spsc_queue_reset()` (`docs/vlc-api-essentials.md:140` old). **After**: Corrected to "Discard SPSC queue (drain via `vw_spsc_queue_pop`) & VAD state (energy VAD is stateless)" — matching the actual API and the pause path's drain-discard pattern.

**Callers**: VLC API reference readers, `plugin/src/vw_whisper_module.c:308-310` (the real drain loop). **Callees**: `vw_spsc_queue_pop` (actual API).

**Happy path**: Reader follows seek sequence → sees drain-via-pop → matches code.

**Failure path**: `vw_spsc_queue_reset()` does not exist — a contributor copying the old doc would get a link error.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Non-existent reset API removed | `docs/vlc-api-essentials.md:140` | — | ✅ done |

**Assumptions/Tradeoffs**: None — strictly a correctness fix.

---

### 1.11 `plugin/include/vw_caption_presenter.h`

**Why change**: Split the ambiguous `vw_caption_presenter_clear` into two operations with distinct lifetimes: mid-session blank (keep context) vs teardown clear (drop context). Required because step 17 must erase OSD on every seek without losing the ability to render future segments.

**Responsibility before**: Single `vw_caption_presenter_clear` (`plugin/include/vw_caption_presenter.h:21` old) that both blanked OSD and nulled `p_filter_ctx` — mid-session use would permanently disable rendering. **After**: New `vw_caption_presenter_blank` (`plugin/include/vw_caption_presenter.h:24`) — blanks OSD but keeps `p_filter_ctx`; `vw_caption_presenter_clear` (`plugin/include/vw_caption_presenter.h:29`) now delegates to `blank` then nulls context, documented as "Teardown-only ... never mid-session."

**Callers**: `plugin/src/vw_caption_presenter.c:114,134`, `plugin/src/vw_whisper_module.c:306,631`, `tests/unit/test_caption_presenter.c:87-90`. **Callees**: `vout_FlushSubpictureChannel`, `vout_OSDText` (via impl).

**Happy path**: Seek → `vw_caption_presenter_blank` (`plugin/src/vw_whisper_module.c:306`) → OSD cleared, context retained → next segment renders. Teardown → `vw_caption_presenter_clear` (`plugin/src/vw_whisper_module.c:631`) → OSD cleared, context nulled.

**Failure path**: If seek called `clear` instead of `blank`, `p_filter_ctx` nulls mid-session → `vw_caption_presenter_show_segment` (`plugin/src/vw_caption_presenter.c:107-108`) no-ops forever → captions never resume after first seek (silent, no error log). The header's teardown-only comment and the split API prevent this.

**Boundaries**:

| Boundary type | What to check | Status |
|---|---|---|
| Input validation | NULL presenter / NULL p_filter_ctx guard | ✅ `plugin/src/vw_caption_presenter.c:115` |
| Concurrency | Presenter used only on sender thread (not callback) | ✅ `plugin/src/vw_whisper_module.c:306,364` sender thread only |
| I/O | vout lookup may fail (passthrough) | ✅ `vw_caption_presenter_find_vout` may return NULL → no-op |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Mid-session blank keeps context | `plugin/include/vw_caption_presenter.h:24`, `plugin/src/vw_caption_presenter.c:114-129` | `tests/unit/test_caption_presenter.c:87-88` | ✅ done |
| 2 | Teardown clear resets context | `plugin/include/vw_caption_presenter.h:29`, `plugin/src/vw_caption_presenter.c:134-139` | `tests/unit/test_caption_presenter.c:89-90` | ✅ done |

**Assumptions/Tradeoffs**: Assumes sender thread owns presenter (true — `presenter.p_filter_ctx` set in `vw_plugin_open:507` and used only in `vw_plugin_sender_main`). Tradeoff: two functions instead of a flag param — clearer call-site intent.

---

### 1.12 `plugin/libvlccore.def`

**Why change**: Import `vout_FlushSubpictureChannel` for the hardened OSD blank path. Needed because 0-duration empty text alone does not displace an active caption (see 1.13).

**Responsibility before**: Imported `vout_OSDText` only. **After**: Adds `vout_FlushSubpictureChannel` (`plugin/libvlccore.def:10`).

**Callers**: Linker (MinGW `libvlccore.dll.a` weak-link). **Callees**: `libvlccore` export.

**Happy path**: `vw_caption_presenter_blank` calls `vout_FlushSubpictureChannel(vout, 1)` (`plugin/src/vw_caption_presenter.c:125`) → instant channel flush → OSD cleared.

**Failure path**: If the symbol were missing at runtime (old VLC), the `VW_WEAK` link pattern (cf. `docs/roadmap.md:53` 17b note) would make the call site null — but this file is a hard import, so on MinGW a missing export would fail to link rather than silently no-op. In practice VLC 3.0.23 exports it.

**Boundaries**:

| Boundary type | What to check | Status |
|---|---|---|
| Persistence | DEF matches linked VLC version | ✅ verified against vendored headers |
| I/O | N/A | — |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Flush symbol available to presenter | `plugin/libvlccore.def:10` | `tests/unit/test_caption_presenter.c:50-53` stub | ✅ done |

**Assumptions/Tradeoffs**: Assumes `VOUT_SPU_CHANNEL_OSD == 1` (VLC stable ABI). Tradeoff: hard DEF import vs `VW_WEAK` — acceptable for 3.0.23 baseline; 17b will add `VW_WEAK` for SPU channel registration.

---

### 1.13 `plugin/src/vw_caption_presenter.c`

**Why change**: Harden OSD blanking and fix the split semantics from `vw_caption_presenter.h`. The original empty-text-with-0-duration blank was ineffective (0-duration subpicture is immediately expired and never displaces the current caption).

**Responsibility before**: `vw_caption_presenter_clear` blanked via `vout_OSDText(..., 0, "")` and nulled context in one function (`plugin/src/vw_caption_presenter.c:111-119` old) — mid-session reuse was impossible and blanking was visually broken. **After**: `vw_caption_presenter_blank` (`plugin/src/vw_caption_presenter.c:114-129`) does `vout_FlushSubpictureChannel(vout, 1)` (instant canonical clear) plus `vout_OSDText(..., 1000, "")` as fallback with short positive duration (1 ms) that displaces the active caption. `vw_caption_presenter_clear` (`plugin/src/vw_caption_presenter.c:134-139`) delegates to `blank` then nulls context.

**Callers**: `plugin/src/vw_whisper_module.c:306` (seek blank), `:631` (teardown clear). **Callees**: `vw_caption_presenter_find_vout`, `vout_FlushSubpictureChannel`, `vout_OSDText`, `vlc_object_release`.

**Happy path**: Seek → `vw_caption_presenter_blank` finds vout → flush channel 1 → post empty text 1 ms → release → OSD cleared → context retained → next `show_segment` renders.

**Failure path**: If `vw_caption_presenter_find_vout` returns NULL (vout walk fails), blank is a no-op — OSD retains stale caption until next segment or teardown. This is the existing passthrough-safe behavior (no crash, just stale visual). Teardown clear still nulls context even when vout is absent (`if (presenter) p_filter_ctx = NULL`).

**Boundaries**:

| Boundary type | What to check | Status |
|---|---|---|
| Input validation | NULL presenter / NULL p_filter_ctx → early return | ✅ `:115` |
| I/O | vout lookup failure → no-op (passthrough) | ✅ `:120` guard |
| Concurrency | No locks — sender thread only | ✅ not called from filter callback |
| Persistence | No disk writes | — |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Seek clears OSD immediately | `plugin/src/vw_caption_presenter.c:125-126` | Manual VLC seek (plan) + `tests/unit/test_caption_presenter.c:87-88` | ✅ done (automated regression) / ⚠️ manual live-VLC not in CI |
| 2 | Clear is teardown-only, blank keeps context | `plugin/src/vw_caption_presenter.c:114,134` | `tests/unit/test_caption_presenter.c:87-90` | ✅ done |

**Assumptions/Tradeoffs**: Assumes channel 1 is `VOUT_SPU_CHANNEL_OSD` (matches VLC). Tradeoff: dual-path (flush + 1 ms text) — flush is canonical, 1 ms text is fallback for builds where flush is ineffective; no downside (flush is idempotent).

---

### 1.14 `plugin/src/vw_whisper_module.c`

**Why change**: Implement the full seeking & discontinuity support on the plugin side — detection in the realtime filter callback and session-epoch restart on the sender thread — without violating Rule 4 (no IPC/heap/locks in the callback).

**Responsibility before**: Filter callback only captured audio (`vw_audio_capture_process_block`); sender loop only handled pause/resume, SPSC drain, and worker frame receive. No seek detection, no epoch restart, no stale-segment filtering. **After**: Adds: (a) realtime discontinuity detection in `vw_plugin_filter` (flag + PTS fallback), (b) atomics `discontinuity_pending`/`resume_pts_us` on `vw_plugin_sys_t`, (c) sender-thread restart sequence (`blank → STOP(2) → drain SPSC → START(new PTS) → clear flag`), (d) paused-seek and playing-seek detection via `INPUT_GET_TIME` polling, (e) stale-segment session_id gating, (f) helper `vw_plugin_find_input` + `vw_plugin_input_position_us`.

**Callers**: VLC filter pipeline (`vw_plugin_filter`), `vw_plugin_open`/`vw_plugin_close`, sender thread `vw_plugin_sender_main`. **Callees**: `vw_caption_presenter_blank`, `vw_worker_client_stop_session`/`start_session`/`send_audio`/`receive_frame`, `vw_spsc_queue_pop`, `vw_plugin_find_input`, `input_GetState`, `input_Control(INPUT_GET_TIME)`, `vw_platform_get_monotonic_time_us`, `vw_log_event`.

**Happy path** (playing seek, `BLOCK_FLAG_DISCONTINUITY` set):
1. `vw_plugin_filter` (`plugin/src/vw_whisper_module.c:470-474`) sees flag → `atomic_store(discontinuity_pending, true)` + `atomic_store(resume_pts_us, p_block->i_pts)`
2. Sender iteration (`:304-321`) sees flag → `vw_caption_presenter_blank` (`:306`) clears OSD → `vw_worker_client_stop_session(..., SEEK_DISCONTINUITY)` (`:307`) → drain SPSC (`:308-310`) → `atomic_load(resume_pts_us)` (`:311`) → `atomic_store(discontinuity_pending, false)` (`:312`) → `vw_worker_client_start_session(..., resume_pts_us)` (`:313`) → new `session_id` generated → `PLUGIN_SESSION_RESTARTED` logged (`:319`)
3. Next worker `CAPTION_SEGMENT` with old session_id is dropped (`:357-361` stale check) — pre-seek text never renders post-seek.

**Happy path** (paused seek — flag never arrives because blocks don't flow while paused):
1. Sender poll (`:248-288`) detects `now_paused != paused` transition → captures `paused_position_us` on pause, compares on resume (`:268`) → `>1s` jump → sets discontinuity_pending + resume_pts_us.
2. Continuous `INPUT_GET_TIME` jump check (`:281-286`) also catches playing-case unflagged seeks and paused seeks where time advances.
3. Same restart sequence as above.

**Failure path** (worker rejects restart START — e.g. model missing):
- `vw_worker_client_start_session` returns false (`:313`) → `atomic_store(worker_dead, true)` (`:314`) → warn `PLUGIN_SESSION_RESTART_FAIL` (`:315`) → `break` sender loop (`:317`) → sender exits → `vw_plugin_close` joins thread, skips `stop_session` because `worker_dead` is true (`:617`), shuts down transport — playback continues, captions disabled (fail-closed, passthrough).

**Failure path** (rapid seeks — flag set again before sender consumes it):
- Callback re-stores `discontinuity_pending=true` and overwrites `resume_pts_us` with the latest PTS (`:472-473`) — sender sees one coalesced restart at the newest anchor, not a queue of restarts. Correct (intermediate anchors are stale).

**Boundaries**:

| Boundary type | What to check | Status |
|---|---|---|
| Input validation | `p_block->i_pts >= VLC_TS_0` guard prevents `VLC_TICK_INVALID (0)` false jump (`:470`) | ✅ |
| Input validation | `sys->capture.last_pts_us > 0` guard prevents first-block false positive (`:470`) | ✅ |
| Input validation | `position_us >= 0` guards when `INPUT_GET_TIME` fails (`:268,281,287`) | ✅ |
| Input validation | `last_position_us == -1` first-sample guard (`:281`) | ✅ |
| Authorization | N/A (local IPC) | — |
| Concurrency | Callback writes only atomics — never IPC/heap/locks (`:472-473`) | ✅ Rule 4 |
| Concurrency | Sender checks flag once per iteration, teardown/restart on sender thread only (`:304-321`) | ✅ |
| Concurrency | `discontinuity_pending`/`resume_pts_us` are `_Atomic` (`:214-215`) | ✅ |
| I/O | `input_Control` may fail → `-1` → no false seek (`:433`) | ✅ |
| I/O | `vw_plugin_find_input` may return NULL (object walk fails) → paused=false, position=-1 → no spurious discontinuity | ✅ |
| I/O | `vw_worker_client_stop_session`/`start_session` may block/fail → sender marks `worker_dead`, passthrough (`:313-317`) | ✅ |
| I/O | `BLOCK_FLAG_DISCONTINUITY` is VLC's own signal — trusted, no validation needed | ✅ |
| Persistence | No disk writes | — |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | BLOCK_FLAG_DISCONTINUITY detection, realtime-safe | `plugin/src/vw_whisper_module.c:470-474` | Manual live-VLC seek | ✅ done (code) / ⚠️ manual not in CI |
| 2 | Non-monotonic PTS fallback (500 ms threshold, VLC_TS_0 guard) | `plugin/src/vw_whisper_module.c:470-473` | — | ✅ done |
| 3 | Sender restart: blank → STOP(2) → drain SPSC → START(new anchor) | `plugin/src/vw_whisper_module.c:304-321` | `tests/unit/vw_test_worker_client.c:164-169`, `tests/integration/test_worker_lifecycle.c:218-232` (worker side) | ✅ done |
| 4 | Paused-seek detection via INPUT_GET_TIME | `plugin/src/vw_whisper_module.c:264-276,281-287` | — | ✅ done (code) / ⚠️ no automated paused-seek IPC test |
| 5 | Continuous position-jump detection (>1s) | `plugin/src/vw_whisper_module.c:281-287` | — | ✅ done |
| 6 | Stale segment session_id gating | `plugin/src/vw_whisper_module.c:357-361` | — | ✅ done |
| 7 | Callback only sets atomics (Rule 4) | `plugin/src/vw_whisper_module.c:472-473` | — | ✅ done |
| 8 | Rapid seeks coalesce to latest anchor | `plugin/src/vw_whisper_module.c:472-473` overwrite | — | ✅ done |
| 9 | Playback never interrupted | No media/playlist calls; only IPC + OSD + SPSC drain | — | ✅ done |

**Assumptions/Tradeoffs**:
- Assumes `BLOCK_FLAG_DISCONTINUITY == 0x0001` (VLC 3.0.23 `vlc_block.h`) — stable ABI.
- Assumes `INPUT_GET_TIME` is microsecond PTS (VLC `input_Control` contract) — verified against vendored headers; returns -1 on failure.
- Assumes `llabs(position_jump) > 1s` is a seek, not a network jitter — roadmap 17d notes jitter also sets the flag, so the 5s jump gate is deferred to look-ahead; current 1s gate may clear on 1s+ re-buffer jumps (acceptable for MVP, documented as observation).
- Tradeoff: `INPUT_GET_TIME` poll throttled to 100ms (same as pause poll) — adds at most 100ms seek detection latency, irrelevant against 8s window latency.
- Tradeoff: PTS fallback threshold 500ms backward (`p_block->i_pts < last_pts_us - 500000`) — catches seek jumps without flagging normal decoder jitter (<500ms).
- Low-confidence: `vw_plugin_input_position_us` depends on `input_Control(INPUT_GET_TIME, ...)` reaching the correct `input_thread_t` via the object walk — if the walk finds no input (e.g. filter attached without a playlist input), position stays -1 and only flag/PTS fallback applies (safe degradation).

---

### 1.15 `protocol/include/vw_protocol_types.h`

**Why change**: Provide named constants for the STOP reason codes that previously existed only as documented numbers, per step 17 plan Scope.

**Responsibility before**: Two constants: `VW_CTRL_REASON_USER_PAUSE 1U` / `VW_CTRL_REASON_USER_RESUME 1U` (both `USER_*`, overlapping value) (`protocol/include/vw_protocol_types.h:140-141` old). No STOP reason constants — callers used literal `0` or `1`. **After**: Adds `VW_CTRL_REASON_USER_STOP 1U`, `VW_CTRL_REASON_SEEK_DISCONTINUITY 2U`, `VW_CTRL_REASON_MEDIA_END 3U` (`protocol/include/vw_protocol_types.h:142-144`).

**Callers**: `plugin/src/vw_whisper_module.c:307,617`, `worker/src/vw_worker.c:40-53`, `tests/unit/vw_test_worker_client.c:164-169,420,517`, `tests/integration/test_worker_lifecycle.c:222`. **Callees**: None (macros).

**Happy path**: `vw_worker_client_stop_session(client, VW_CTRL_REASON_SEEK_DISCONTINUITY)` → wire `reason=2` → worker `vw_worker_control_reason_name(2) → "SEEK_DISCONTINUITY"` logged.

**Failure path**: Literal `0` (old `vw_worker_client_stop_session(c, 0)`) would render as `"0"` via the default branch (`worker/src/vw_worker.c:51`) — not a failure but less diagnosable; now replaced.

**Boundaries**:

| Boundary type | What to check | Status |
|---|---|---|
| Input validation | Reason is `uint16_t` wire field — constants fit | ✅ 1/2/3 within 16-bit |
| Authorization | N/A | — |
| Concurrency | N/A (macros) | — |
| I/O | Wire encoding unchanged (reason already existed) | ✅ no protocol bump |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | SEEK_DISCONTINUITY=2 constant exists | `protocol/include/vw_protocol_types.h:143` | `tests/unit/vw_test_worker_client.c:166` assert | ✅ done |
| 2 | USER_STOP=1 / MEDIA_END=3 constants | `protocol/include/vw_protocol_types.h:142,144` | — | ✅ done |

**Assumptions/Tradeoffs**: Assumes reason codes 1/2/3 are stable per `docs/api-contracts.md` — no wire change. Note: `USER_PAUSE`/`USER_RESUME`/`USER_STOP` all equal `1U` (same value, different message types) — intentional, documented, not a collision (type disambiguates).

---

### 1.16 `tests/integration/test_worker_lifecycle.c`

**Why change**: Prove the worker accepts the STOP(SEEK_DISCONTINUITY) → START restart cycle on a single connection and drops stale pre-seek audio, per step 17 plan Test plan.

**Responsibility before**: Model-gated section did `START → AUDIO×N → PAUSE → RESUME → STOP → SHUTDOWN → exit 0`. **After**: Inserts `vw_worker_client_stop_session(c, VW_CTRL_REASON_SEEK_DISCONTINUITY)` + `vw_worker_client_start_session(c, 4000000, ...)` (new session_id, new PTS epoch) + `AUDIO×2` before final `STOP → SHUTDOWN` (`tests/integration/test_worker_lifecycle.c:218-232`).

**Callers**: CTest integration suite (model-gated — skipped if `ggml-tiny.en.bin` absent). **Callees**: `vw_worker_client_stop_session`, `vw_worker_client_start_session`, `vw_worker_client_send_audio`, `vw_worker` process.

**Happy path**: `START(0)` → `AUDIO(ts=0,512ms)`×N → `PAUSE/RESUME` → `STOP(2)` → `START(4000000)` (new session_id) → `AUDIO(ts=4000000,512ms)`×2 → `STOP(0)` → `SHUTDOWN` → worker exits 0 → `EXPECT` passes.

**Failure path**: If worker rejected the second START (e.g. duplicate-session guard), `EXPECT(vw_worker_client_start_session(...))` fails → test asserts → non-zero exit. If worker crashed on builder drain, `pthread_join` returns non-zero or worker exits non-zero. Either surfaces as test failure.

**Boundaries**:

| Boundary type | What to check | Status |
|---|---|---|
| Input validation | `start_pts_us=4000000` valid PTS | ✅ |
| I/O | Worker must accept STOP→START without transport drop | ✅ verified by exit 0 |
| Concurrency | Single connection, sequential frames — no race | ✅ |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Worker survives STOP(2) → START restart | `tests/integration/test_worker_lifecycle.c:218-222` | Self (exit 0) | ✅ done |
| 2 | Post-seek AUDIO accepted under new session_id | `tests/integration/test_worker_lifecycle.c:223-232` | Self | ✅ done |

**Assumptions/Tradeoffs**: Assumes `USE_MODEL=1` build has `ggml-tiny.en.bin`; otherwise this section is `#ifdef`-gated and not exercised in CI (acceptable — unit test covers the client side unconditionally).

---

### 1.17 `tests/unit/test_caption_presenter.c`

**Why change**: Regression for the new `blank` vs `clear` split — ensure mid-session blank keeps context and teardown clear nulls it.

**Responsibility before**: Called `vw_caption_presenter_clear(&presenter)` and asserted `p_filter_ctx == NULL`. No blank test. **After**: Adds `vout_FlushSubpictureChannel` stub (`tests/unit/test_caption_presenter.c:50-53`), constructs `ctx_presenter` with fake filter, calls `vw_caption_presenter_blank` and asserts `p_filter_ctx == &fake_filter` retained (`:87-88`), then `vw_caption_presenter_clear` and asserts `== NULL` (`:89-90`).

**Callers**: CTest unit suite. **Callees**: `vw_caption_presenter_blank`/`clear`.

**Happy path**: `blank` with valid `p_filter_ctx` → walks null object_type chain → no vout found → no-op but context retained → `assert` passes.

**Failure path**: If `blank` incorrectly nulled context, `assert(ctx_presenter.p_filter_ctx == &fake_filter)` fails → test aborts. If `clear` failed to null, second assert fails. Also covers the new DEF import — link would fail if `vout_FlushSubpictureChannel` were not stubbed.

**Boundaries**:

| Boundary type | What to check | Status |
|---|---|---|
| Input validation | NULL presenter / NULL p_filter_ctx (other tests) | ✅ |
| I/O | Stubbed VLC symbols | ✅ |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | blank keeps context, clear resets it | `tests/unit/test_caption_presenter.c:85-94` | Self | ✅ done |

**Assumptions/Tradeoffs**: Assumes `filter_t` zeroed has NULL object_type/parent — correct for stub. Tradeoff: stub does not verify `vout_FlushSubpictureChannel` was called (call count not tracked) — acceptable for regression; `vw_test_worker_client` covers the client side more strictly.

---

### 1.18 `tests/unit/vw_test_worker_client.c`

**Why change**: Enforce that the seek-flavored STOP carries `reason == VW_CTRL_REASON_SEEK_DISCONTINUITY`, per step 17 plan Test plan (unit).

**Responsibility before**: Fake server received STOP and checked `hdr.type == STOP` only; client called `vw_worker_client_stop_session(client, 0)`. **After**: Fake server decodes payload via `vw_protocol_decode_payload(VW_MSG_STOP_SESSION, ...)` and asserts `stop_ctrl.reason == VW_CTRL_REASON_SEEK_DISCONTINUITY` (`tests/unit/vw_test_worker_client.c:164-169`, returns 23 on mismatch). Client calls `VW_CTRL_REASON_SEEK_DISCONTINUITY` in Tests 5 and 9 (`:420,517`).

**Callers**: CTest unit suite (unconditional — no model gate). **Callees**: `vw_protocol_decode_payload`, `vw_ipc_receive`, `vw_worker_client_stop_session`.

**Happy path**: Client `stop_session(..., 2)` → fake server `decode_payload` → `reason == 2` → server returns 0 → `EXPECT(ret_val == 0)` (`:426`) passes.

**Failure path**: If client sent `0` or `1`, server returns `(void*)23` → `EXPECT` fails → test non-zero. If codec mis-encoded reason, decode fails → mismatch → failure. Both surfaced immediately.

**Boundaries**:

| Boundary type | What to check | Status |
|---|---|---|
| Input validation | Payload decode + reason equality | ✅ `:164-168` |
| I/O | Header type check before payload decode | ✅ `:156-158` |
| Concurrency | Single-threaded fake server | ✅ |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | STOP reason == SEEK_DISCONTINUITY(2) | `tests/unit/vw_test_worker_client.c:164-169` | Self (return 23 on mismatch) | ✅ done |

**Assumptions/Tradeoffs**: Assumes fake server's `vw_protocol_decode_payload` matches worker's codec — true (same `protocol/` build). Tradeoff: only one STOP reason is asserted (seek); `USER_STOP`/`MEDIA_END` stay nominal — fine, seek is the step-17-critical path.

---

### 1.19 `worker/src/vw_worker.c`

**Why change**: Prevent pre-seek caption hypotheses from leaking into the post-seek session, and make STOP logs diagnosable — the two worker-side requirements of step 17.

**Responsibility before**: `VW_MSG_START_SESSION` copied new `session_id` and sent `STARTED` but left `vw_segment_builder_t` hypotheses (produced from pre-seek windows) intact — they would be popped after the START and stamped with the NEW `session_id` (`worker/src/vw_worker.c:420-421`) and rendered post-seek (caption leak). `VW_MSG_STOP_SESSION` logged generic "session stopped" regardless of reason. **After**: (a) On `START` (`worker/src/vw_worker.c:322-327`) drain-pops builder hypotheses and `free`s their `text_utf8` (reusing ownership contract) before `STARTED`; (b) `VW_MSG_STOP_SESSION` (`:400-405`) logs `reason=%s` via new `vw_worker_control_reason_name` (`:40-54`) which maps 1→USER_STOP, 2→SEEK_DISCONTINUITY, 3→MEDIA_END, else numeric string.

**Callers**: `vw_worker_run` main loop (`:217` while running, `:297` switch), reader thread queue. **Callees**: `vw_segment_builder_pop`, `free`, `vw_worker_control_reason_name`, `vw_log_event`, `vw_audio_buffer_clear`, `vw_protocol_encode_payload`/`header`, `vw_ipc_send`.

**Happy path** (seek restart):
1. Plugin sends `STOP(2)` → worker `:400` sets `session_active=false`, logs `"session stopped (reason=SEEK_DISCONTINUITY)"`, clears `audio_buf`.
2. Plugin sends `START(new session_id, resume_pts_us)` → worker `:322-327` drains builder (`while pop → free text`) discarding pre-seek hypotheses → copies new `session_id` (`:317`) → sets `session_active=true` → logs `"session started (STARTED sent)"` → sends `STARTED`.
3. Next `AUDIO` with new `session_id` (`:342-346` gate `memcmp(session_id) != 0` drops stale) is accepted; next `CAPTION_SEGMENT` pop (`:418-436`) stamps new `session_id` and seek has no leak.

**Failure path** (stale builder not discarded — old behavior):
- Seek → STOP → START(new id) → builder still holds hypothesis "hello" from pre-seek window → main loop pops it (`:418`) → stamps `new session_id` (`:421`) → encodes `CAPTION_SEGMENT` → plugin `vw_plugin_sender_main` stale check (`plugin/src/vw_whisper_module.c:357`) would still compare `recv.segment.session_id` vs `client->session_id` (new) — they would MATCH (worker stamped new id), so stale text would render post-seek. The drain on START closes this gap at the source.

**Boundaries**:

| Boundary type | What to check | Status |
|---|---|---|
| Input validation | `session_active` guard on duplicate START (`:299-301`) | ✅ |
| Input validation | `sample_rate != 16000` → `E_AUDIO_FORMAT` (`:302-308`) | ✅ existing |
| Input validation | `session_id` gating on AUDIO (`:343-346` memcmp) — drops in-flight pre-seek chunks | ✅ |
| Concurrency | Builder drain on START runs on main loop (single-writer for builder) — reader thread only pushes frames, never touches builder | ✅ |
| I/O | `audio_buf` clear on STOP (`:404`) — stale PCM dropped | ✅ |
| I/O | Builder pop `free(text_utf8)` reuses ownership contract (caller frees) | ✅ `:324-326` |
| Persistence | No disk writes; `vw_worker_control_reason_name` uses `static char buf[16]` — see Bug | ⚠️ |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Pre-seek builder hypotheses discarded on START | `worker/src/vw_worker.c:322-327` | `tests/integration/test_worker_lifecycle.c:218-232` (exit 0 + no leak) + `plugin/src/vw_whisper_module.c:357` backstop | ✅ done |
| 2 | STOP logs symbolic reason | `worker/src/vw_worker.c:40-54,402-403` | Log inspection (`WORKER_SESSION stopped (reason=SEEK_DISCONTINUITY)`) | ✅ done |

**Assumptions/Tradeoffs**: Assumes builder hypotheses are the only cross-epoch leak vector (true — `audio_buf` is already cleared on STOP, VAD is stateless energy check). Tradeoff: builder drain is unconditional on every START, not just seek — harmless (normal START has empty builder) and simpler than a seek-flag.

---

## 2. Happy-Path Request Trace

**Scenario**: User plays a video, captions are flowing, then drags the seek bar forward 30 seconds while playing.

1. `plugin/src/vw_whisper_module.c:438` `vw_plugin_filter(filter, block)` — VLC delivers `block` with `i_flags & BLOCK_FLAG_DISCONTINUITY` and `i_pts = 30000000` (post-seek anchor).
2. `plugin/src/vw_whisper_module.c:470` `if (BLOCK_FLAG_DISCONTINUITY || (pts fallback))` — true via flag.
3. `plugin/src/vw_whisper_module.c:472` `atomic_store(&sys->discontinuity_pending, true)` — realtime-safe, no IPC/heap/locks.
4. `plugin/src/vw_whisper_module.c:473` `atomic_store(&sys->resume_pts_us, 30000000)` — anchor saved.
5. `plugin/src/vw_whisper_module.c:476` `vw_audio_capture_process_block` — still runs (capture is passthrough; queue may receive one post-seek chunk before drain).
6. Sender iteration `plugin/src/vw_whisper_module.c:304` `if (atomic_load(discontinuity_pending))` — true.
7. `plugin/src/vw_whisper_module.c:305` `vw_log_event("PLUGIN_DISCONTINUITY", ...)` — log.
8. `plugin/src/vw_whisper_module.c:306` `vw_caption_presenter_blank(&sys->presenter)` → `plugin/src/vw_caption_presenter.c:125` `vout_FlushSubpictureChannel(vout, 1)` + `vout_OSDText(..., 1000, "")` → OSD instantly cleared, context retained.
9. `plugin/src/vw_whisper_module.c:307` `vw_worker_client_stop_session(sys->client, VW_CTRL_REASON_SEEK_DISCONTINUITY)` → `protocol/include/vw_protocol_types.h:143` reason 2 → wire `VW_MSG_STOP_SESSION {session_id, reason=2}`.
10. `worker/src/vw_worker.c:400` `case VW_MSG_STOP_SESSION` → `session_active=false`, `vw_worker_control_reason_name(2) → "SEEK_DISCONTINUITY"`, `vw_log_event("WORKER_SESSION", "session stopped (reason=SEEK_DISCONTINUITY)")`, `vw_audio_buffer_clear(audio_buf)` — stale PCM dropped.
11. `plugin/src/vw_whisper_module.c:308` `while (vw_spsc_queue_pop(sys->queue, &stale)) {}` — pre-seek PCM discarded; `worker/src/vw_worker.c:344` session_id gating would also drop any in-flight pre-seek AUDIO that slipped past.
12. `plugin/src/vw_whisper_module.c:311` `resume_pts_us = atomic_load(&sys->resume_pts_us)` — 30000000.
13. `plugin/src/vw_whisper_module.c:312` `atomic_store(&sys->discontinuity_pending, false)` — flag cleared.
14. `plugin/src/vw_whisper_module.c:313` `vw_worker_client_start_session(sys->client, 30000000, "tiny.en")` → `protocol/include/vw_protocol_types.h` START with `timeline_origin_pts_us=30000000`, new random `session_id` (generated inside `start_session`), wire `VW_MSG_START_SESSION`.
15. `worker/src/vw_worker.c:322` `while (vw_segment_builder_pop(builder, &stale_seg)) free(stale_seg.text_utf8)` — any pre-seek hypothesis freed.
16. `worker/src/vw_worker.c:317` `memcpy(session_id.bytes, payload.start.session_id.bytes, 16)` — new epoch id adopted; `session_active=true`; `vw_log_event("WORKER_SESSION", "session started (STARTED sent)")`.
17. `worker/src/vw_worker.c:331` `STARTED` (header-only) sent.
18. `plugin/src/vw_whisper_module.c:319` `vw_log_event("PLUGIN_SESSION_RESTARTED", "session epoch restarted at 30000000")` — plugin confirms.
19. Next audio block at ~30s flows through `vw_plugin_filter` → capture → queue → `vw_worker_client_send_audio` → `worker/src/vw_worker.c:342` `memcmp(session_id)` matches new id → `vw_audio_buffer_append_s16le` → windowing → VAD → `vw_whisper_engine_transcribe_pcm` → `vw_segment_builder_push_hypothesis` at new PTS.
20. New `CAPTION_SEGMENT` popped at `worker/src/vw_worker.c:418`, stamped with new `session_id` (`:421`), sent.
21. `plugin/src/vw_whisper_module.c:354` `case VW_MSG_CAPTION_SEGMENT` → `:357` `memcmp(recv.segment.session_id, sys->client->session_id) == 0` — matches → `:362-365` `vw_caption_presenter_show_segment` renders new caption at correct timeline. Any stale in-flight segment with old session_id would be dropped at `:357-361`.

Result: OSD cleared instantly on seek; no pre-seek audio/text leaks into post-seek captions; new captions resume ~8s after seek (batch-window latency, by design); playback uninterrupted.

---

## 3. Most Important Failure Path

**Scenario**: Seek-while-paused with a >1s position jump, followed by worker rejection of the restart START (e.g. model file was removed between initial start and seek).

1. User pauses → sender poll `plugin/src/vw_whisper_module.c:264-276` captures `paused_position_us = 5000000` (5s), sends `PAUSE(1)` → worker `worker/src/vw_worker.c:383` `paused=true`, `vw_audio_buffer_clear`.
2. While paused, user seeks to 120s — no audio blocks flow, so `BLOCK_FLAG_DISCONTINUITY` never arrives; `INPUT_GET_TIME` is clock-driven and frozen at 5s.
3. User resumes → sender poll sees `now_paused=false` vs `paused=true` → `plugin/src/vw_whisper_module.c:268` `llabs(120000000 - 5000000) = 115000000 > 1000000` → sets `discontinuity_pending=true`, `resume_pts_us=120000000`, logs `PLUGIN_SEEK_WHILE_PAUSED`.
4. Sender restart `:304-321` runs: `blank` OSD, `STOP(2)`, drain SPSC, `START(120000000, "tiny.en")`.
5. Worker receives `STOP(2)` → `worker/src/vw_worker.c:400` `session_active=false`, buffer cleared.
6. Worker receives `START` with `model_id="tiny.en"` → `worker/src/vw_worker.c:309` `if (!engine)` (model was deleted) → `send_error(..., E_MODEL_MISSING, 0, ...)` → `break` (no `STARTED`), `session_active` stays false.
7. `plugin/src/vw_whisper_module.c:313` `vw_worker_client_start_session` waits up to 5s for `STARTED`, receives `ERROR` instead → returns false.
8. `plugin/src/vw_whisper_module.c:314` `atomic_store(&sys->worker_dead, true)` → `:315` warn `PLUGIN_SESSION_RESTART_FAIL` → `:317` `break` sender loop → sender thread exits.
9. `plugin/src/vw_whisper_module.c:605` `vw_plugin_close` eventually joins sender thread (`:613`), sees `worker_dead==true` so skips `stop_session` (`:617` guard), does `shutdown`+`disconnect`, destroys queue, `vw_caption_presenter_clear` removes OSD — VLC media playback continues (filter is passthrough), captions disabled for this item.

**Exit state**: Worker process exits 0 (authenticated, clean shutdown); plugin degrades to passthrough; no crash, no transport leak, no stale captions. If `send_error` itself failed (transport dead), sender would also mark `worker_dead` and passthrough.

---

## 4. Boundary Summary

| Boundary type | Checks performed | Gaps / risks |
|---|---|---|
| **Input validation** | `p_block->i_pts >= VLC_TS_0` + `last_pts_us > 0` guards (callback PTS fallback) `:470` | None — 0-PTS false jump prevented |
| | `position_us >= 0` guards on all `INPUT_GET_TIME` paths (`:268,281,287`) | None |
| | `last_position_us == -1` first-sample guard (`:281`) | None |
| | `session_active` duplicate-START guard (`worker/src/vw_worker.c:299`) | None |
| | `sample_rate != 16000` → `E_AUDIO_FORMAT` (`worker/src/vw_worker.c:302`) | None |
| | `session_id` memcmp gating on AUDIO (`worker/src/vw_worker.c:343`) — drops stale pre-seek chunks | None |
| | `session_id` memcmp on SEGMENT stale check (`plugin/src/vw_whisper_module.c:357`) | None — but note worker builder drain is the primary fix; this is the backstop for in-flight segments |
| | `reason` wire field is `uint16_t` — constants 1/2/3 fit | None |
| **Authorization** | HELLO token constant-time compare (unchanged) | None — not in diff |
| **Concurrency** | Callback writes only `_Atomic bool`/`int64_t` — no IPC/heap/locks (`:472-473`) | None — Rule 4 satisfied |
| | Sender restart runs on sender thread only (`:304-321`) | None |
| | Builder drain on START runs on worker main loop (single-writer for builder) — reader thread only pushes frames, never touches builder | None — reader thread never touches builder |
| | `discontinuity_pending`/`resume_pts_us` are `_Atomic` (`:214-215`) | None |
| | `vw_worker_control_reason_name` uses `static char buf[16]` for fallback | ⚠️ Not thread-safe (see Bug High) — safe today because only main loop calls it, but fragile |
| **I/O** | `input_Control(INPUT_GET_TIME)` failure → `-1` → no false seek (`:433`) | None |
| | `vw_plugin_find_input` returns NULL → no spurious discontinuity (`:399-421`) | None |
| | `vw_worker_client_stop/start_session` failure → `worker_dead` → passthrough (`:313-317`) | None |
| | `vout_FlushSubpictureChannel` + 1ms fallback covers flush-ineffective builds (`plugin/src/vw_caption_presenter.c:125-126`) | None |
| | `vw_ipc_receive`/`send` timeouts (3s) retained | None |
| | `BLOCK_FLAG_DISCONTINUITY` trusted as VLC signal | None — but jitter also sets it (roadmap 17d observation) |
| **Persistence** | No disk writes in diff | None |
| | `plugin/libvlccore.def` hard import vs `VW_WEAK` | Low risk — VLC 3.0.23 exports the symbol |
| | `resume_pts_us` PTS anchor forwarded via `timeline_origin_pts_us` but worker does not yet consume it (plan non-goal) | None — forward-compat, new `session_id` is the real epoch |

---

## 5. Acceptance Criterion → Code Mapping

From `docs/plans/step17_plan.md:77-88` (plus implicit criteria from scope/design):

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Seek during playback: OSD clears immediately | `plugin/src/vw_caption_presenter.c:125-126` flush+1ms, `plugin/src/vw_whisper_module.c:306` blank | `tests/unit/test_caption_presenter.c:87-88` + manual VLC seek | ✅ done / ⚠️ manual live-VLC not in CI |
| 2 | New captions resume ~8s later from new position (batch-window latency) | `worker/src/vw_worker.c:352-378` windowing (8s/2s) unchanged | Manual | ✅ done (existing geometry) |
| 3 | No caption mixes pre-seek and post-seek audio | `worker/src/vw_worker.c:322-327` builder drain + `plugin/src/vw_whisper_module.c:357-361` session_id gating + `worker/src/vw_worker.c:343` AUDIO gating | `tests/integration/test_worker_lifecycle.c:218-232`, `tests/unit/vw_test_worker_client.c:164-169` | ✅ done |
| 4 | `PLUGIN_DISCONTINUITY` / `PLUGIN_SESSION_RESTARTED` logs on seek; worker logs `stopped` then `started` with new session | `plugin/src/vw_whisper_module.c:305,319`, `worker/src/vw_worker.c:402,328` | Log inspection | ✅ done |
| 5 | Pre-seek audio never reaches post-seek window | `plugin/src/vw_whisper_module.c:308-310` SPSC drain + `worker/src/vw_worker.c:404` audio_buf clear + `worker/src/vw_worker.c:322-327` builder drain | `tests/integration/test_worker_lifecycle.c:223-232` | ✅ done |
| 6 | Seek while paused: no transport drop, no crash, playback unaffected | `plugin/src/vw_whisper_module.c:264-287` paused-seek detection + restart coalescing | — (no automated paused-seek IPC test) | ⚠️ partial — code handles it, no automated test |
| 7 | Rapid seeks: no crash, coalesces to latest anchor | `plugin/src/vw_whisper_module.c:472-473` overwrite | — | ✅ done (logic) / ⚠️ no test |
| 8 | Seek at EOF/media end: no crash | `worker/src/vw_worker.c:400` STOP is idempotent (sets `session_active=false` regardless) | — | ✅ done |
| 9 | `VW_CTRL_REASON_SEEK_DISCONTINUITY` constant | `protocol/include/vw_protocol_types.h:143` | `tests/unit/vw_test_worker_client.c:166` | ✅ done |
| 10 | STOP→START restart on one connection | `plugin/src/vw_whisper_module.c:313`, `worker/src/vw_worker.c:322` | `tests/integration/test_worker_lifecycle.c:218-232` | ✅ done |
| 11 | Docs updated (roadmap, architecture, api-contracts, test-strategy, vlc-api-essentials) | `docs/roadmap.md:51`, `docs/architecture.md:75`, `docs/api-contracts.md:87`, `docs/test-strategy.md:59-60`, `docs/vlc-api-essentials.md:140` | — | ✅ done |
| 12 | Callback only sets atomics (Rule 4) | `plugin/src/vw_whisper_module.c:472-473` | — | ✅ done |
| 13 | C17, no C++ | All `.c`/`.h` | `clang-format` | ✅ done |
| 14 | Playback never interrupted | No media control calls; passthrough preserved | Manual | ✅ done |

---

## 7. Code Review Findings (Bugs, Risks, Nitpicks)

### Bugs (Sorted by Priority)

| Priority | Component / Location | Description | Impact | Proposed Fix |
|---|---|---|---|---|
| **High** | `worker/src/vw_worker.c:40-54` `vw_worker_control_reason_name` | Fallback branch returns `static char buf[16]` — not thread-safe and overwritten on next call. Today only the worker main loop calls it (`:402`), so no concurrent call, but a future log from the reader thread or a second STOP log would alias. Also `buf` is shared across calls — logging `reason=0` then `reason=99` in one `vw_log_event` varargs expansion would show the second value twice. | Rare log corruption / misleading diagnostics | Return a string literal for known cases and `snprintf` into a caller-provided buffer or use a `thread_local` / per-call literal for the fallback (e.g. `return "UNKNOWN"` or format at call site). Minimal fix: make `buf` `_Thread_local` or avoid the fallback path by mapping 0→`"USER_STOP"` explicitly. |
| **Medium** | `plugin/src/vw_whisper_module.c:264-276` `paused_position_us` lifecycle | `paused_position_us` is set on pause but never cleared if the sender thread exits pause detection early (e.g. `vw_plugin_find_input` returns NULL on resume → `position_us=-1` → jump check skipped but `paused_position_us` still holds stale 5s value; next pause→resume could compare stale baseline). Current code clears it on any `now_paused != paused` resume (`:275`), so the `-1` case still clears — but if `vw_plugin_find_input` returns NULL on the pause edge, `paused_position_us=-1` is stored, making the next real resume's jump check (`-1 >= 0` guard) no-op and missing a paused-seek. | Paused-seek missed when input lookup fails on the pause edge | Guard `paused_position_us` assignment with `position_us >= 0` (only snapshot when position is valid), or re-sample on resume via the continuous `INPUT_GET_TIME` path which already covers this case. |
| **Medium** | `plugin/src/vw_whisper_module.c:470-474` PTS fallback threshold | Fallback triggers when `p_block->i_pts < last_pts_us - 500000` (500 ms). A forward seek of e.g. +200 ms (small scrub) would not trigger via PTS, and if the encoder also failed to set `BLOCK_FLAG_DISCONTINUITY`, the seek would be missed. Conversely, normal decoder jitter of >500 ms backward (rare but possible on B-frame reorder) could false-trigger. Threshold is heuristic. | Small forward seeks missed if flag absent; rare false clears on jitter | Acceptable for MVP (flag is primary signal; PTS is fallback for unflagged backward jumps). Consider adding a forward-jump fallback (`p_block->i_pts > last_pts_us + 1000000`) gated by the same 1s position-jump threshold, or rely on `INPUT_GET_TIME` continuous check (`:281`) which already catches >1s jumps in either direction. |
| **Low** | `plugin/src/vw_whisper_module.c:281-286` position-jump vs flag double-trigger | A single seek can fire both `BLOCK_FLAG_DISCONTINUITY` (callback) and `INPUT_GET_TIME` jump (sender poll) — both set `discontinuity_pending=true` and `resume_pts_us` (callback PTS vs input time, which may differ by up to 100 ms poll lag). The sender coalesces to one restart but `resume_pts_us` may be whichever wrote last (race between callback atomic store and poll atomic store). | `timeline_origin_pts_us` anchor may be off by ≤100 ms | Benign — anchor is forward-compat only (worker does not consume it today); `session_id` is the real epoch. No fix needed; document as observation. |
| **Low** | `tests/unit/test_caption_presenter.c:50-53` stub | `vout_FlushSubpictureChannel` stub is a no-op — test does not verify it was called or with channel 1. A regression that removed the flush call would still pass. | Silent blank regression not caught | Add a call-count or channel assertion in the stub (e.g. `static int flush_calls; assert(flush_calls==1 && channel==1)` after `blank`). |
| **Low** | `protocol/include/vw_protocol_types.h:140-144` overlapping 1U values | `USER_PAUSE`, `USER_RESUME`, `USER_STOP` all `1U` — intentional (type disambiguates) but a future `switch(reason)` without also switching on `type` would conflate them. The new `vw_worker_control_reason_name` already conflates: `case VW_CTRL_REASON_USER_STOP: return "USER_STOP"` matches `USER_PAUSE`/`USER_RESUME` values too. | Log mislabels PAUSE reason 1 as "USER_STOP" if ever called for PAUSE | `vw_worker_control_reason_name` is only called for STOP (`:402`), so no bug today. If reused for PAUSE, split the function by message type or add a `type` param. |

### Architectural & Operational Risks

| Category | Risk Description | Affected Files | Mitigation Strategy |
|---|---|---|---|
| **Jitter vs seek** | `BLOCK_FLAG_DISCONTINUITY` is also set on network re-buffer/jitter (VOD/live), not just seeks. Flag-triggered restart without a clock-jump threshold would clear captions on jittery streams. | `plugin/src/vw_whisper_module.c:470`, `docs/roadmap.md:55` | Already tracked as 17d observation; mitigate with `VW_INPUT_JUMP_DISCONTINUITY_US = 5s` gate (roadmap 17d) that requires a position jump in addition to the flag. Current MVP accepts the jitter-clear tradeoff. |
| **Portability** | `vout_FlushSubpictureChannel` hard import in `plugin/libvlccore.def` vs `VW_WEAK` pattern used for SPU channel registration (17b). On a VLC build without this export, MinGW link fails rather than degrades gracefully. | `plugin/libvlccore.def:10`, `plugin/src/vw_caption_presenter.c:125` | Safe for VLC 3.0.23 baseline (export exists). For 17b, use `VW_WEAK` for optional SPU symbols; keep this hard import or add a weak fallback that skips flush and relies on 1ms text. |
| **Ordering** | Sender restart does `STOP → drain SPSC → START`. An AUDIO enqueued after the drain but before `START` (callback racing the sender) would have the new session_id? No — callback enqueues with old capture state, but `AUDIO` gating (`worker/src/vw_worker.c:343` memcmp) drops it until `START` adopts the new id. The chunk is wasted but not leaked. | `plugin/src/vw_whisper_module.c:308-313`, `worker/src/vw_worker.c:343` | No mitigation needed; session_id gating is the backstop. Optionally, drain again after `START` to avoid one wasted IPC send. |
| **Starvation** | `start_session` blocks up to 5s waiting for `STARTED` while holding the sender loop (no SPSC drain, no worker frame drain during the block). A 5s window of audio could overflow the 8s SPSC cap and drop newest. | `plugin/src/vw_whisper_module.c:313` | Existing behavior (same as initial start); SPSC drops newest on overflow (bounded). Acceptable — seek restart is rare, and 5s is worst-case (model missing returns ERROR immediately). |

### Code Style & Quality Nitpicks

| Issue Type | File & Line | Description | Recommendation |
|---|---|---|---|
| **Naming** | `worker/src/vw_worker.c:40` `vw_worker_control_reason_name` | Function is `static` but uses `vw_worker_` prefix (public namespace). Consistent with other `static` helpers in this file (`send_error`, `verify_token_constant_time` without prefix), but not uniform. | Either keep `vw_` for grep-ability (current) or drop prefix for `static` — pick one and apply file-wide. |
| **Magic number** | `plugin/src/vw_whisper_module.c:268,281` `1000000` | Seek threshold `1s` appears as literal `1000000` in two places. | Extract `#define VW_SEEK_JUMP_THRESHOLD_US 1000000` (or reuse the planned `VW_INPUT_JUMP_DISCONTINUITY_US` from 17d, with MVP value 1s). |
| **Magic number** | `plugin/src/vw_whisper_module.c:470` `500000` | PTS fallback threshold `500ms` is literal. | Define `VW_PTS_DISCONTINUITY_THRESHOLD_US 500000` near other thresholds. |
| **Magic number** | `plugin/src/vw_caption_presenter.c:126` `1000` | Fallback OSD duration `1ms` literal. | Define `VW_OSD_BLANK_DURATION_US 1000` or comment as "short positive duration per vlc_vout_osd.h semantics". |
| **Dead helper** | `plugin/src/vw_whisper_module.c:424` `vw_plugin_input_is_paused` | Now a one-liner wrapper over `vw_plugin_find_input` + `input_GetState`. Kept for call-site symmetry, but the sender loop inlines the logic anyway (`:255-256`). | Keep (readability) or inline and remove — either is fine; current is not harmful. |
| **Header comment** | `plugin/include/vw_caption_presenter.h:26` | `clear` comment says "Teardown-only ... never mid-session" — strong and correct, but could cite the caller (`vw_plugin_close`) for discoverability. | Add "Called only from `vw_plugin_close`" to the doc comment. |

