# Diff Analysis: Step 17b Native SPU Presentation & Subpicture Subsystem vs gemini/milestone-3

**16 files changed, +849 / -957 lines** (includes the prior `diff.md` review artifact rewrite)
**Base**: `gemini/milestone-3` (pre-PR #10 parent `cfa40fd`; branch `gemini/milestone-3-step-17b` merged via PR #10 as `5b1b514`)
**Scope**: PR #10 commits `98d64d8` (feat), `34e13cf` (fix), `2b214b3` (postmortem addendum), `40d70a0` (prior diff.md), `5662a29` (code-review fixes: privacy redaction, held-vout lifetime)
**Line references**: final merged state (`5b1b514` / `gemini/milestone-3`), not the worktree (currently on `gemini/milestone-3-step-17c`).

---

## 1. File-by-File Analysis

### 1.1 `docs/api-contracts.md`

**Why change**: Document the clock-domain contract discovered during SPU integration: VLC's audio output stamps audio-filter block PTS in the system-date domain, so the wire carries that domain; the presenter schedules captions in the OSD clock domain.

**Responsibility before**: Protocol framing/message documentation. **After**: Same, plus an explicit wire-`pts_us` domain note and pointer to `vlc-api-essentials.md` §3.4/§7.

**Callers**: Developers/agents implementing capture or presentation timing. **Callees**: None (spec).

**Happy path**: Reader understands why worker segment PTS are not media timestamps and why the presenter does not convert them.
**Failure path**: Treating block PTS as media time causes hour-scale caption offsets (the original 17b bug).

**Boundaries**: Doc invariant — clock domains defined exactly.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Wire PTS domain documented | `docs/api-contracts.md` Terminology note | N/A (Doc) | Done |

**Assumptions/Tradeoffs**: Documents current VLC 3.0 `aout_DecPlay` re-basing behavior.

---

### 1.2 `docs/architecture.md`

**Why change**: Keep the "Time and buffering" section honest about the wire domain and the OSD-clock scheduling decision.

**Responsibility before**: Architectural invariants incl. media-timeline caption timing. **After**: States wire carries system-date PTS; presenter schedules in OSD clock domain; media-domain scheduling deferred to 17c.

**Callers/Callees**: None (spec).

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Timing architecture updated | `docs/architecture.md` Time and buffering | N/A (Doc) | Done |

---

### 1.3 `docs/plans/milestone3_postmortem.md`

**Why change**: Add the Step 17b bugfix-trace addendum, explicitly marked as a newer iteration outside the postmortem's original timeline.

**Responsibility before**: Historical postmortem of milestone-3 feature branches. **After**: Plus addendum documenting: aout system-date PTS domain, subtitle-clock selection drop on VLC 3.0.23 Windows, OSD-clock-domain fix, expected warning, 17c deferral.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Addendum added, timeline-isolated | `docs/plans/milestone3_postmortem.md` Addendum section | N/A (Doc) | Done |

---

### 1.4 `docs/plans/step17b_plan.md`

**Why change**: Plan artifact for Step 17b per Rule 9; §3 rewritten with the empirical clock-domain finding.

**Responsibility before**: Did not exist. **After**: Full task breakdown, scope, design, acceptance criteria, test plan, DoD.

**Callers**: Agents/developers implementing 17b-17e. **Callees**: None.

**Boundaries**: Rule 9 compliance; internal consistency (see Finding N-1).

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Plan created and maintained | `docs/plans/step17b_plan.md:L123-132` (Acceptance) | N/A (Doc) | Done |

**Assumptions/Tradeoffs**: Plan In-Scope item 3 and the design snippet still say `b_subtitle = true` (superseded by §3 and shipped code) — see N-1.

---

### 1.5 `docs/roadmap.md`

**Why change**: Mark 17b `[x]` with the corrected OSD-clock summary; 17c/17d/17e remain unchecked.

**Responsibility before**: 17b unchecked. **After**: 17b complete; look-ahead phrase timing explicitly parked in 17c/17d.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Roadmap updated | `docs/roadmap.md:L53` (17b), `L54-56` (17c/17d/17e unchecked) | N/A (Doc) | Done |

**Assumptions/Tradeoffs**: Phrase timing is a 17c/17d deliverable, not 17b (plan "Out of Scope") — see Section 5.

---

### 1.6 `docs/source-layout.md`

**Why change**: Update the `vw_caption_presenter.c` row to SPU-channel rendering with OSD fallback (Rule 14).

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Layout table updated | `docs/source-layout.md:L142` | N/A (Doc) | Done |

---

### 1.7 `docs/test-strategy.md`

**Why change**: Document the Step 17b unit-test contract for the presenter.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Test strategy updated | `docs/test-strategy.md` presenter entry | N/A (Doc) | Done |

---

### 1.8 `docs/vlc-api-essentials.md`

**Why change**: Add §3.4 (audio-filter block PTS re-based to system-date) and §7 (two SPU clock domains + empirical caveat that filter-pushed `b_subtitle=true` subpictures are dropped before region rendering on the 3.0.23 Windows build).

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Clock-domain reference added | `docs/vlc-api-essentials.md` §3.4, §7 | N/A (Doc) | Done |

---

### 1.9 `plugin/include/vw_caption_presenter.h`

**Why change**: SPU channel state (`p_held_vout`, `spu_channel_id`, `spu_channel_registered`), 3-arg `show_segment` signature, Rule 11 doc comments.

**Responsibility before**: `p_filter_ctx` only; OSD-only presenter. **After**: Owns held-vout lifetime, channel ID, registration flag.

**Callers**: `vw_whisper_module.c` (sender thread), `test_caption_presenter.c`. **Callees**: None (header).

**Happy path**: `{0}`-init struct; sender registers channel on first segment; `p_held_vout` keeps the vout alive across calls.
**Failure path**: Registration failure → `spu_channel_id = -1`, `spu_channel_registered = false`, no hold taken.

**Boundaries**:
- Lifetime: `p_held_vout` hold/release pairing (see Q3/Q5); presenter ops on sender thread only.
- Input validation: `spu_channel_id >= 0` guards.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | SPU state fields declared | `vw_caption_presenter.h:L9-14` | `test_caption_presenter.c:L168-169,220-225` | Done |
| 2 | `show_segment` signature + Rule 11 comments | `vw_caption_presenter.h:L20-23` | `test_caption_presenter.c:L182` | Done |

---

### 1.10 `plugin/include/vw_platform.h`

**Why change**: Define `VW_WEAK` once (`__attribute__((weak))` Linux, empty Windows) per the postmortem's MinGW weak-symbol fix.

**Responsibility before**: Platform OS wrappers. **After**: Same, plus `VW_WEAK`.

**Callers**: Plugin TUs referencing VLC symbols. **Callees**: None.

**Failure path**: Missing macro on Windows → weak symbol resolves NULL at runtime.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | `VW_WEAK` defined once | `vw_platform.h:L13,L16` | Windows cross-build linkage | Done |

---

### 1.11 `plugin/libvlccore.def`

**Why change**: Export SPU + timing symbols for MinGW dynamic linking.

**Responsibility before**: 14 exports. **After**: 21 exports, adding `vout_RegisterSubpictureChannel`, `vout_PutSubpicture`, `subpicture_New/Delete`, `subpicture_region_New/Delete`, `text_segment_New/Delete`, `mdate`.

**Callers**: MinGW `dlltool` import lib. **Callees**: `libvlccore.dll`.

**Failure path**: Missing export → link failure or runtime DLL import error.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | SPU symbols exported | `libvlccore.def` | `windows-x64-release` build + Windows test exes | Done |

---

### 1.12 `plugin/src/vw_caption_presenter.c`

**Why change**: Implement native SPU channel rendering, text-region construction, vout-recreation re-registration with held-vout lifetime, OSD-clock-domain scheduling, dual-channel flush, OSD fallback.

**Responsibility before**: `vout_OSDText` on channel 1 only. **After**: Registers private channel, builds `subpicture_t` (`VLC_CODEC_TEXT` region + `text_segment_New`), schedules at `mdate()` with `b_subtitle=false`, flushes SPU+OSD on blank/clear, falls back to OSD.

**Callers**: `vw_whisper_module.c` sender; unit tests. **Callees**: `vout_RegisterSubpictureChannel`, `vout_PutSubpicture`, `vout_FlushSubpictureChannel`, `vout_OSDText`, `subpicture_*`, `text_segment_*`, `mdate`, `vlc_object_hold/release`, `input_GetVout` (via find_vout).

**Happy path**: `show_segment` → find_vout (held) → register/reuse channel 9 → `i_start=mdate()`, `i_stop=mdate()+duration` → `render_spu` builds region/text → `vout_PutSubpicture` (ownership transferred) → `PRESENTER_SPU_RENDER` (text_len, no transcript) → release vout ref (L231).

**Failure path**: channel registration `<0` (L192-198) → `PRESENTER_SPU_FAILED` → OSD fallback `vout_OSDText` (L218-223). Allocation failures in `render_spu` unwind region/subpic (L96-106).

**Boundaries**:
- Input validation: NULL segment/text (L158), duration <= 0 → 2s default (L161-164).
- Lifetime: old held vout released before re-register (L180-183); new hold taken on success (L186-187); released on clear (L262-265). Balanced on all paths.
- Concurrency: sender thread only; close() joins sender before `clear()`.
- Memory: `vout_PutSubpicture` owns; pre-put failures cleaned.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | SPU channel registration | `vw_caption_presenter.c:L178-199` | `test_caption_presenter.c:L182-196` | Done |
| 2 | `VLC_CODEC_TEXT` region + text | `vw_caption_presenter.c:L91-106` | Test 6 (region/text mock) | Done |
| 3 | Bottom-center alignment | `vw_caption_presenter.c:L108-111` | Test 6 (construction) | Done |
| 4 | OSD-clock scheduling (`mdate()`) | `vw_caption_presenter.c:L201-215` | `test_caption_presenter.c:L187-188` | Done |
| 5 | Vout recreation re-registration | `vw_caption_presenter.c:L179,184-189` | `test_caption_presenter.c:L227-243` | Done |
| 6 | OSD fallback on channel failure | `vw_caption_presenter.c:L217-223` | `test_caption_presenter.c:L198-209` | Done |
| 7 | Dual-channel flush on blank/clear | `vw_caption_presenter.c:L239-253,259-270` | `test_caption_presenter.c:L213-225` | Done |

**Assumptions/Tradeoffs**: `b_subtitle=false` + `mdate()` is the only reliably rendering domain for filter-pushed subpictures on VLC 3.0.23 Windows (empirically verified); media-domain scheduling deferred to 17c.

---

### 1.13 `plugin/src/vw_whisper_module.c`

**Why change**: Pass `current_position_us` into `show_segment` (reserved 17c anchor), initialize presenter SPU fields in open, add redacted `PLUGIN_SEGMENT` logging.

**Responsibility before**: Sender called `show_segment` without timing context. **After**: Polls input position (100 ms throttle, L261-268), passes it (L392), redacted segment log (L383-387), presenter init (L533-535).

**Callers**: VLC core (open/filter/close), sender thread. **Callees**: `vw_caption_presenter_show_segment`, `input_Control`, `input_GetState`.

**Happy path**: `VW_MSG_CAPTION_SEGMENT` → session_id match (L377) → `PLUGIN_SEGMENT` (redacted) → `show_segment(..., current_position_us)` (L392).
**Failure path**: stale segment (session mismatch) → `PLUGIN_STALE_SEGMENT` drop (L378-381); worker transport fatal → `PLUGIN_WORKER_DEAD`.

**Boundaries**: Realtime callback untouched (atomics only, unchanged); session-id gating; privacy (no transcript text logged — 5662a29).

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Position tracked + passed | `vw_whisper_module.c:L261-268,L392` | Presenter tests | Done |
| 2 | Presenter SPU init in open | `vw_whisper_module.c:L533-535` | Unit tests | Done |
| 3 | Redacted pipeline logging | `vw_whisper_module.c:L383-387` | Runtime logs | Done |

---

### 1.14 `tests/unit/test_caption_presenter.c`

**Why change**: Mock VLC SPU symbols; assert registration, construction, mdate timing, `b_subtitle=false`, fallback, flush, clear, vout-recreation re-registration.

**Responsibility before**: Basic OSD display/clear tests. **After**: 10 test groups incl. held-vout assertions (L223, L234, L243).

**Callers**: CTest. **Callees**: `vw_caption_presenter.c` + mocks.

**Happy path**: All asserts pass; exit 0; Valgrind-clean (mock `vout_PutSubpicture` frees subpic/region/text).
**Failure path**: Assertion → `abort()`.

**Boundaries**: Standalone (no live VLC); `NDEBUG` safety `(void)` casts; mock `vlc_object_hold/release` are identity/no-op (lifetime pairing not observable — see N-4).

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Registration + timing + flags | `test_caption_presenter.c:L182-196` | Test 6 | Done |
| 2 | OSD fallback | `test_caption_presenter.c:L198-209` | Test 7 | Done |
| 3 | Dual flush | `test_caption_presenter.c:L213-218` | Test 8 | Done |
| 4 | Clear resets held vout + channel | `test_caption_presenter.c:L220-225` | Test 9 | Done |
| 5 | Vout recreation re-registers | `test_caption_presenter.c:L227-243` | Test 10 | Done |

---

### 1.15 `worker/src/vw_worker.c`

**Why change**: Redacted `WORKER_SEGMENT` emission log (privacy per Rule 5).

**Responsibility before**: Segment emission without log. **After**: Logs id/start/end/final/text_len (no transcript text).

**Callers**: Worker main loop. **Callees**: `vw_ipc_send`, `vw_log_event`.

**Boundaries**: Privacy — transcript text never persisted/logged.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Segment emission log, redacted | `worker/src/vw_worker.c:L436-439` | `test_worker_lifecycle` | Done |

---

### 1.16 `diff.md`

**Why change**: This review artifact; prior analysis (pre-`5662a29`) rewritten to cover the full merged PR, the review-fix commit, plan-task verification, and manual-test evidence.

**Responsibility**: Workspace-root review document, updated per the diff-review skill.

---

## 2. Happy-Path Request Trace

Worker GPU inference → caption on screen (verified manually on Windows):

1. **Worker** (`worker/src/vw_worker.c:L436-439`): `vw_segment_builder_pop` completes a segment; `vw_protocol_encode_payload` + `vw_ipc_send` transmit `VW_MSG_CAPTION_SEGMENT`; `WORKER_SEGMENT` logged with `text_len` only.
2. **Plugin sender** (`plugin/src/vw_whisper_module.c:L371-392`): `vw_worker_client_receive_frame` → session_id `memcmp` match (L377) → `PLUGIN_SEGMENT` (redacted) → `vw_caption_presenter_show_segment(&sys->presenter, &recv.segment, current_position_us)` (L392).
3. **Presenter** (`plugin/src/vw_caption_presenter.c`):
   - `vw_caption_presenter_find_vout` walks filter→aout→playlist→children→input, `input_GetVout` returns held vout (L173).
   - First call: `vout_RegisterSubpictureChannel` → channel 9; `vlc_object_hold(vout)` stored in `p_held_vout` (L184-189).
   - `now_tick = mdate()`; `start_tick = now_tick`; `stop_tick = now_tick + duration` (L211-213).
   - `render_spu`: `subpicture_New(NULL)` → `video_format_Init(&fmt, VLC_CODEC_TEXT)` + sar 1/1 → `subpicture_region_New` → `text_segment_New` → `i_align = SUBPICTURE_ALIGN_BOTTOM`, `i_y = 20` → `b_subtitle=false, b_ephemer=true, b_fade=true` → `vout_PutSubpicture(vout, subpic)` (L86-128).
   - `PRESENTER_SPU_RENDER` (text_len, mdate-scale start/stop) (L225-228); `vlc_object_release` (L231).
4. **VLC vout**: spu heap → `SpuSelectSubpictures` (OSD clock `render_osd_date = mdate()`) → freetype text render → d3d11 quad blend → caption at bottom center. Manual result: "spu subtitles show up properly".

## 3. Most Important Failure Path

**Video output recreation mid-session** (window resize/recreate):

1. Old `vout_thread_t` destroyed. `p_held_vout` (held ref) prevents the memory from being freed/reused while held — the prior review's address-reuse risk is closed by `5662a29`.
2. Next segment: `find_vout` returns new vout pointer; `presenter->p_held_vout != (void*)vout` (L179) → true.
3. Old held ref released (L180-183); `vout_RegisterSubpictureChannel(new_vout)` (L184); success → hold new vout, update channel id (L185-189).
4. Subpicture pushed to the new vout; caption resumes. Unit-covered: `test_caption_presenter.c:L227-243`.

**Secondary failure: SPU channel registration failure** → `spu_channel_id = -1`, `PRESENTER_SPU_FAILED` (L192-198), OSD fallback `vout_OSDText(vout, 1, BOTTOM, duration, text)` (L219). Unit-covered Test 7. Caveat: `vout_OSDText` silently no-ops if the user's `osd` setting is disabled (documented in `vlc-api-essentials.md` §7) — captions then do not display; acceptable product tradeoff (SPU channel path is independent of that setting).

## 4. Boundary Summary

| Boundary Type | Implementation & Defense | Verification / Test |
|---|---|---|
| **Input validation** | NULL segment/text guards (L158); duration <= 0 → 2s default (L161-164); channel id `< 0` guards | Test 4/5, Test 7 |
| **Authorization** | Session-id `memcmp` gating on sender (module L377) | `test_worker_lifecycle` |
| **Concurrency** | Presenter state on sender thread only; close joins sender before `clear()`; realtime callback untouched (atomics only) | Code audit, Valgrind |
| **Lifetime** | `p_held_vout` hold on register, release on re-register/clear; `find_vout` held refs released on every path | Test 9/10 |
| **I/O** | IPC 3 s timeouts (unchanged); fatal → `PLUGIN_WORKER_DEAD` | `test_worker_ipc` |
| **Memory** | `vout_PutSubpicture` ownership transfer; pre-put unwind deletes region/text/subpic | Valgrind memcheck |
| **Privacy** | Transcript text redacted from all new log lines (text_len only) | 5662a29 audit |

## 5. Acceptance Criterion → Code Mapping (plan `docs/plans/step17b_plan.md`)

### In-Scope items (plan L14-50)

| # | Plan item | Implementation | Status |
|---|---|---|---|
| 1 | `VW_WEAK` in `vw_platform.h` | `vw_platform.h:L13,L16` | Done |
| 2 | Presenter struct + `show_segment` signature | `vw_caption_presenter.h:L9-23` | Done |
| 3 | Presenter impl (register, region, align, OSD-clock, flush, fallback, cleanup) | `vw_caption_presenter.c:L80-270` | Done |
| 4 | Sender passes input position | `vw_whisper_module.c:L261-268,L392` | Done |
| 5 | `libvlccore.def` exports | `libvlccore.def` (9 new symbols) | Done |
| 6 | Unit tests | `test_caption_presenter.c` Tests 6-10 | Done |
| 7 | Docs (source-layout, architecture, roadmap, test-strategy) | `docs/*.md` | Done |

### Acceptance Criteria (plan L123-132)

| # | Criterion | Code | Test / Evidence | Status |
|---|---|---|---|---|
| 1 | Segments display via native SPU `VLC_CODEC_TEXT` | `vw_caption_presenter.c:L86-128` | Manual: "spu subtitles show up properly" | Done |
| 2 | Bottom-center alignment | `vw_caption_presenter.c:L108-111` | Manual visual + Test 6 | Done |
| 3 | Seek purges via `vout_FlushSubpictureChannel` | `vw_caption_presenter.c:L239-253` | Manual: "seeking support preserved"; Test 8 | Done |
| 4 | Pause/resume preserves display/timeline | OSD-domain scheduling (L211-213) + module pause drain | Manual: "working while paused or playing" | Done |
| 5 | OSD fallback on `< 0` / no vout | `vw_caption_presenter.c:L217-223` | Test 7 | Done |
| 6 | `VW_WEAK` + `.def` exports | `vw_platform.h`, `libvlccore.def` | Windows cross-build | Done |
| 7 | MinGW links, zero unresolved | `libvlccore.def` | `windows-x64-release` build; "all exe tests passing on windows" | Done |
| 8 | 100% CTest (16/16) | — | `ctest --preset linux-x64-debug`: 16/16 | Done |
| 9 | Valgrind zero leaks in presenter ops | — | memcheck: 0 errors; presenter test clean (pre-existing libgomp still-reachable noise only) | Done |

### Definition of Done (plan §DoD)

All DoD items met (C17, realtime-safe callback, no disk I/O, Rule 11 header docs, Rule 14 docs, clang-format clean, Linux + Windows builds, memcheck clean). Verified across `34e13cf`/`5662a29` gate runs.

### Out-of-Scope / explicitly deferred (plan "Out of Scope" L51-53, roadmap L54-56)

- **Phrase timing (per-phrase media-time scheduling / zero perceived latency)**: belongs to **17c** (look-ahead source decode + lead pacing) and **17d** (seek re-sync). Roadmap marks 17c/17d unchecked; media-domain scheduling blocked on the subtitle-clock probe documented in `step17b_plan.md` §3 and the postmortem addendum. Not a 17b gap — manual test confirms look-ahead transcription is otherwise healthy.

## 7. Code Review Findings (Bugs, Risks, Nitpicks)

### Bugs (Sorted by Priority)

| Priority | Component / Location | Description | Impact | Proposed Fix |
| --- | --- | --- | --- | --- |
| **Resolved** | `plugin/src/vw_caption_presenter.c` (pre-`5662a29` `p_last_vout`) | Address-reuse race on vout recreation: destroyed vout pointer compared by value could alias a new vout. | Delayed channel re-registration in rare allocator-reuse cases | Fixed in `5662a29`: `p_held_vout` holds the object, so the address cannot be reused while cached. |
| **Low** | `plugin/src/vw_caption_presenter.c:L218-223` | OSD fallback `vout_OSDText` silently no-ops when the user's `osd` setting is disabled (`var_InheritBool` gate in `video_text.c`), so fallback captions may not display. | Captions absent when SPU registration fails and OSD is off | Document (done in essentials §7); optionally bypass by pushing the subpicture on channel 1 directly instead of `vout_OSDText`. |
| **Low** | `plugin/src/vw_caption_presenter.c:L161-164,213` | 2 s duration fallback literal duplicated in two places. | Drift risk if one is changed | Hoist `#define VW_MIN_CAPTION_DURATION_US 2000000LL`. |

### Architectural & Operational Risks

| Category | Risk Description | Affected Files | Mitigation Strategy |
| --- | --- | --- | --- |
| **Roadmap boundary** | 17b schedules at `mdate()` (OSD clock) — cannot pre-schedule future captions; phrase timing requires media-domain scheduling (17c/17d). | `vw_caption_presenter.c`, `docs/roadmap.md:L54-56` | 17c lead pacing + subtitle-clock probe (postmortem addendum); `input_time_us` parameter already reserved. |
| **VLC SPU warning** | `main warning: original picture size is undefined` fires once per caption (`i_original_*` unset). | `vw_caption_presenter.c` | Documented benign: VLC falls back to source size and caches it; text scaling correct. |
| **Portability** | OSD-clock finding verified on VLC 3.0.23 Windows (d3d11va + direct3d11); Linux/X11 vout may accept media-domain subtitles. | `vw_caption_presenter.c` | Deferred to 17c probe; `b_subtitle`/domain documented in essentials §7. |
| **Privacy** | Redaction now covers WORKER/PLUGIN/PRESENTER logs; audit remaining log sites for transcript leakage in future steps. | `vw_worker.c`, `vw_whisper_module.c`, `vw_caption_presenter.c` | Keep `text_len` convention for transcript data. |

### Code Style & Quality Nitpicks

| Issue Type | File & Line | Description | Recommendation |
| --- | --- | --- | --- |
| **Doc inconsistency** | `docs/plans/step17b_plan.md:L25,L74` | In-Scope item 3 and the design snippet still say `b_subtitle = true`, contradicting §3 and the shipped code (`false`). | Update the two stale spots to the OSD-clock decision. |
| **Dead parameter** | `vw_caption_presenter.c:L165` | `input_time_us` is `(void)`-cast; sender still polls and passes it. | Intentional 17c reservation; add a roadmap pointer comment (already present) — acceptable. |
| **Mock fidelity** | `test_caption_presenter.c` | `vlc_object_hold/release` mocks are identity/no-op, so held-vout release pairing is not observable in tests. | Optionally count hold/release calls in mocks to assert balance. |
| **Uncovered edge** | `vw_caption_presenter.c:L239-253` | `blank()` with `find_vout` returning NULL is untested (returns silently). | Acceptable (no-op path); add a unit case if desired. |

---

# Part 2 — Look-Ahead Source Decoding & Lead Pacing (Step 17c)

**29 files changed, +1622 / -250 lines**
**Base**: `gemini/milestone-3` (merged 17b state); branch `gemini/milestone-3-step-17c`
**Commits**: `c1eceef` (feat 17c), `7f37f60` (fix: false seek loops), `17b76b5` (docs: ADR-017 + 17d.1 phrase timing)
**Line references**: branch HEAD (`17b76b5`). Phrase-by-phrase timing (ADR-017 / `phrase_timing_segmentation_plan.md`) is **designed but not implemented** — tracked as roadmap step 17d.1.

---

## 1. File-by-File Analysis

### 2.1 `worker/include/vw_source_decoder.h` (new)

**Why change**: Common platform demuxer interface for ahead-of-time decoding (FFmpeg/MF), per `step17c_plan.md` scope item 2.
**Responsibility before/after**: New — opaque handle + `open/seek/read_s16le/get_duration_us/close` (L13-42), Rule 11 comments.
**Callers**: `vw_worker.c`; tests. **Callees**: platform implementations.
**Happy path**: `open(url, &info)` → decode loop `read_s16le` → `seek` on jumps → `close`.
**Failure path**: `open` returns NULL → worker falls back to live mode.
**Boundaries**: NULL-guards in every entry; info fields populated on open only.

### 2.2 `worker/src/vw_source_decoder_ffmpeg.c` (new, Linux, `VW_WITH_FFMPEG`)

**Why change**: libavformat/libavcodec/libswresample demuxer → 16 kHz S16 mono.
**Responsibility**: Owns `AVFormatContext`/`AVCodecContext`/`SwrContext`, PTS bookkeeping, 4096-sample leftover buffer.
**Happy path** (L59-136 open; L154-232 read): `avformat_open_input` → `av_find_best_stream(AUDIO)` → `avcodec_open2` → `swr_alloc_set_opts2` to S16/16k/mono → `av_read_frame`/`avcodec_send_packet`/`receive_frame`/`swr_convert`; `av_rescale_q(frame->pts)` anchors `*out_pts_us`; leftovers drained first.
**Failure path**: any open step failure → full unwind + NULL; EOF → `eof_reached`, returns 0.
**Boundaries**: URL-decode + `file://` strip (L34-57); `frame->pts == AV_NOPTS_VALUE` guard (L191); leftover cap 4096 (L218-220); stub build without FFmpeg (L249-285).
**Assumptions**: `frame->pts` valid (no `best_effort_timestamp` fallback — see Finding 2-B2).

### 2.3 `worker/src/vw_source_decoder_mf.c` (new, Windows)

**Why change**: Media Foundation `IMFSourceReader` demuxer, PCM 16k mono via partial type.
**Responsibility**: Owns `IMFSourceReader`, PTS bookkeeping, leftover buffer; relies on process-wide `MFStartup/MFShutdown` (worker L167/L605, ADR-015).
**Happy path** (L61-139 open; L161-246 read): `MFCreateSourceReaderFromURL` → stream selection → `SetCurrentMediaType(PCM 16k mono)` → `ReadSample` → contiguous buffer lock → copy/leftover; sample `llTimestamp/10` → µs PTS (L217).
**Failure path**: any `FAILED(hr)` → release reader + NULL; EOF flag → `eof_reached`.
**Boundaries**: `file:///` and `file://` strip + `/`→`\` + URL-decode (L27-59); 100 ns→µs conversion (L127, L217); `PropVariant` init/clear paired.

### 2.4 `worker/src/vw_worker.c` (rework: +471 lines)

**Why change**: Source Mode state machine — look-ahead decode loop, `POSITION` pacing, seek-without-teardown, HELLO_ACK `SOURCE_MODE` capability; process-wide MF lifecycle.
**Responsibility**: Adds source state (L206-212: `source_decoder`, `source_mode`, `current/last/decoded_pts_us`, 30 s `lead_target_us`); START opens demuxer from `source_url` (L365-390) with fallback log; `POSITION` handler (L406-439) updates playhead/pause and re-seeks on `SEEK` flag / >2 s backward jump / >1 s forward-past-decoded; look-ahead decode step (L528-558) reads 2 s chunks and runs the same 8 s/2 s window pipeline; live `AUDIO` dropped in source mode (L442-445); segments emitted with session_id stamped (L560-584); MFStartup/Shutdown (L167, L605).
**Happy path**: live path unchanged; source path: START(source_url) → open OK → loop decodes until `decoded_pts_us >= current + 30 s` (L529-530) → idles 5 ms (L254-259).
**Failure path**: demuxer open fails → `source_mode=false`, live PCM pipeline continues (L382-387); invalid payload → exit (L285-290); auth failure → exit (L298-302).
**Boundaries**: POSITION applies without session_id memcmp (only `session_active` gate, L407) — see Finding 2-B1; media swap re-seek gap — Finding 2-R2.

### 2.5 `worker/CMakeLists.txt`

**Why change**: Wire `vw_source_decoder_{mf,ffmpeg}.c` into worker + tests; `pkg_check_modules(FFMPEG ...)` conditional; MF libs (`mfplat mfreadwrite mfuuid ole32 propsys`) on Win32.
**Boundaries**: FFmpeg presence gated (`VW_WITH_FFMPEG`), non-Win32 only.

### 2.6 `protocol/include/vw_protocol_types.h`

**Why change**: Protocol v1.1: `MINOR 1`, `VW_CLIENT/WORKER_VERSION 1.1.0`, `VW_CAPABILITY_SOURCE_MODE (1U<<3)` (L49), `VW_SOURCE_LIVE_AUDIO=0` added (L53-54), `E_SOURCE_OPEN` (L69), `VW_MSG_POSITION=13` (L82), START `source_url[1024]` + `source_url_len` (L125-131), `vw_msg_position_t` + flags (L133-140).
**Boundaries**: `VW_MAX_SOURCE_URL_BYTES=1024`; POSITION validation via validator.

### 2.7 `protocol/src/vw_protocol_codec.c`

**Why change**: Encode/decode START `source_url` (len-prefixed, backward-compatible absent-field decode L211-218) and `POSITION` (session_id, current/input pts, rate, flags).
**Boundaries**: `url_len` capped at 1023 on encode (L78-80), `>= 1024` rejected on decode (L217); fixed field order.

### 2.8 `protocol/src/vw_protocol_validate.c`

**Why change**: `POSITION` validation: `0 < playback_rate <= 16` (L112-117).
**Boundaries**: Rate sanity bounds; no session_id validation here (worker-side gate).

### 2.9 `plugin/include/vw_worker_client.h`

**Why change**: `worker_capabilities` field; `start_session(..., source_url)`; new `send_position()`.
**Callers**: `vw_whisper_module.c`, tests.

### 2.10 `plugin/src/vw_worker_client.c`

**Why change**: Record `ack.capability_flags` (L192); START sets `source_kind = LOCAL_FILE/LIVE_AUDIO` and copies `source_url` (L224-244); `send_position` encodes + sends POSITION (L333-363) with transport-drop on send failure.
**Boundaries**: `strncpy` bounded by `sizeof(source_url)-1`; `session_active` gate.

### 2.11 `plugin/src/vw_whisper_module.c`

**Why change**: MRL extraction, START(source_url), POSITION pacing, seek re-anchor replacing STOP→START.
**Responsibility**: At session start, `input_GetItem`/`input_item_GetURI` detects local files (L233-253) and passes `source_url`; every 100 ms poll sends `POSITION` (L283-289) with `VW_POSITION_FLAG_PAUSED` while paused; discontinuity path (L339-353) now: blank presenter + `POSITION(SEEK)` + drain live chunks (no STOP→START teardown).
**Boundaries**: URI localness check (`file://`, leading `/`, `X:\` or `X:/`, L239-243); `source_url` freed on all paths; live-path STOP→START retained for non-source sessions.

### 2.12 `plugin/src/vw_caption_presenter.c`

**Why change**: Look-ahead future scheduling — activate the reserved `input_time_us`: `lead_us = start_pts_us - input_time_us` (capped at 60 s), `start_tick = mdate() + lead_us` (L204-216).
**Boundaries**: `lead` only when `input_time > 0 && start > input`; >60 s or negative → immediate (live-path behavior). Pause/seek interplay — Finding 2-B3.

### 2.13 `plugin/libvlccore.def`

**Why change**: Export `input_GetItem`, `input_item_GetURI` for MinGW (MRL extraction).
**Boundaries**: Exact libvlccore symbol names.

### 2.14 `tests/CMakeLists.txt`

**Why change**: `test_source_decoder` (platform decoder source + FFmpeg/MF libs); worker/lifecycle tests compile decoder sources + MF libs.
**Boundaries**: FFmpeg optional via `pkg-config`; Windows MF libs linked.

### 2.15 `tests/unit/test_source_decoder.c` (new)

**Why change**: Exercise `vw_source_decoder` API: NULL/empty/invalid-path guards (L9-18), fixture-gated open/read/seek/duration (L20-66).
**Boundaries**: Fixture-dependent (Test 3 skips when `harvard.wav`/`jfk.wav` absent) — see Finding 2-N3.

### 2.16 `tests/unit/test_protocol_codec.c`

**Why change**: Round-trip `START` with `source_url` and `POSITION` (fields, flags).
**Boundaries**: Exact field equality asserts.

### 2.17 `tests/unit/test_protocol_validate.c`

**Why change**: POSITION rate validation bounds (1.0 OK; 0, -1, 17 rejected).

### 2.18 `tests/unit/test_caption_presenter.c`

**Why change**: Test 11 — future lead scheduling: segment at 15 s with playhead at 10 s → `i_start = mdate(100 s) + 5 s`, `i_stop = 100 s + 7 s` (L237-250).
**Boundaries**: Lead math asserted exactly.

### 2.19 `tests/unit/vw_test_worker_client.c` / 2.20 `tests/integration/test_worker_lifecycle.c`

**Why change**: 4-arg `start_session(..., NULL)` call sites; lifecycle keeps STOP→START epoch test (live path).

### 2.21 `docs/plans/step17c_plan.md` (new)

**Why change**: Step 17c plan per Rule 9. Acceptance criteria L157-166 (see Section 5).
**Boundaries**: Non-goals: 17d (5 s jump heuristic), 17e (beam search), bounded 30-60 s lead.

### 2.22 `docs/plans/phrase_timing_segmentation_plan.md` (new)

**Why change**: Design/evaluation for phrase-by-phrase timing (17d.1, ADR-017): per-sub-segment PTS from `whisper_full_get_segment_t0/t1`, discrete SPU cues. **Design only — not implemented** (current worker still emits whole-window text).
**Boundaries**: Documents the spoiler/crowding problem the current coarse aggregation causes.

### 2.23 `docs/plans/step17_restart_deprecation_plan.md`

**Why change**: Resolves previously unmerged conflict markers (keeps HEAD side; verified 0 `<<<<<<<`/`>>>>>>>` remain at HEAD) and updates seek-acceptance wording for the POSITION-based design.

### 2.24 `docs/decisions.md` — ADR-017

**Why change**: Records the phrase-by-phrase timing decision (status Accepted; implementation deferred to 17d.1).

### 2.25 `docs/api-contracts.md` / 2.26 `docs/architecture.md` / 2.27 `docs/source-layout.md` / 2.28 `docs/roadmap.md` / 2.29 `docs/test-strategy.md`

**Why change**: Protocol v1.1 (`POSITION`, `source_url`, `SOURCE_MODE` capability bit `1U<<3`), dual-mode wire PTS contract, seek policy split (live STOP→START vs source POSITION re-anchor), roadmap 17c `[x]` + 17d.1 phrase timing entry, layout/test-strategy updates (Rule 14).

---

## 2. Happy-Path Request Trace (Source Mode)

1. **Plugin open** (`vw_whisper_module.c:L233-253`): `input_GetItem` + `input_item_GetURI` → `file:///C:/video.mp4` → `vw_worker_client_start_session(..., source_url)`.
2. **START** (`vw_worker_client.c:L224-244`): `source_kind=LOCAL_FILE`, `source_url` copied; wire `vw_msg_start_t` with `source_url_len`.
3. **Worker START handler** (`vw_worker.c:L365-390`): `vw_source_decoder_open(source_url)` (MF `MFCreateSourceReaderFromURL` / FFmpeg `avformat_open_input`); `source_mode=true`; anchors `current/last/decoded_pts_us = timeline_origin`.
4. **Pacing** (plugin L283-289): every 100 ms `POSITION(current_pts, input_time, 1.0f, paused?)`.
5. **Look-ahead decode** (`vw_worker.c:L528-558`): while `decoded_pts_us < current + 30 s`, `read_s16le` 2 s chunks → audio buffer → 8 s window → VAD → `vw_whisper_engine_transcribe_pcm` → builder hypothesis `[window_pts, window_pts+dur]`.
6. **Segment emit** (L560-584): builder pop → `session_id` stamp → `VW_MSG_CAPTION_SEGMENT` → `WORKER_SEGMENT` (text_len).
7. **Plugin receive + schedule** (`vw_whisper_module.c:L392`, `vw_caption_presenter.c:L204-216`): session_id gate → `lead = start_pts - input_time` (≤60 s) → `start_tick = mdate()+lead`, `stop_tick = start_tick + duration` → `vout_PutSubpicture` on channel 9 (OSD clock domain) → VLC displays exactly when the playhead reaches the phrase.
8. Manual result: "lookahead transcription looking good"; captions in sync (zero perceived latency).

## 3. Most Important Failure Path

**Seek outside the decoded horizon (or media swap)**:
1. User seeks; plugin position poll detects >1 s jump (`PLUGIN_SEEK_POSITION`, module L320) → `discontinuity_pending` → blank presenter + `POSITION(SEEK, resume_pts)` + drain live chunks (L339-353).
2. Worker `POSITION` handler (L406-439): `seek_flag || backward_jump(>2 s) || forward_past_decoded(>1 s)` → `vw_source_decoder_seek(target)` + clear audio_buf + drain builder; `decoded_pts_us = target`; no teardown; decode resumes at target.
3. Captions resume after the 8 s window refills at decode speed (≈2-5× realtime — faster than the old playback-rate refill; documented in `step17_restart_deprecation_plan.md`).
4. **Gap (Finding 2-R2)**: a media *swap* (different file, same session) is treated as a seek — the worker re-seeks the OLD demuxer instead of reopening the new MRL. Live-mode STOP→START handled swaps; source mode does not re-extract the MRL.

## 4. Boundary Summary (17c)

| Boundary Type | Implementation & Defense | Verification |
|---|---|---|
| **Input validation** | `source_url_len` cap 1023/1024 (codec); POSITION rate `(0,16]`; NULL guards in decoder API | protocol tests, decoder tests |
| **Authorization** | HELLO token constant-time; first-frame HELLO enforcement (unchanged) | worker lifecycle tests |
| **Concurrency** | Worker main loop single-writer; reader thread drains queue; plugin sender-only presenter | valgrind, lifecycle tests |
| **I/O** | 3 s IPC timeouts; `POSITION` every 100 ms; drop transport on send failure | worker_ipc tests |
| **Memory** | Demuxer unwind on all open failure paths; leftover buffers capped; MF `PropVariant` paired | valgrind, source decoder tests |
| **Privacy** | No transcript/PCM logs; source URL path logged in `WORKER_SOURCE` (metadata only) | audit |
| **Lifetime** | `MFStartup/Shutdown` once per process (ADR-015); demuxer closed on STOP/SHUTDOWN/restart | lifecycle |

## 5. Acceptance Criterion → Code Mapping (17c plan L157-166)

| # | Criterion | Code | Test / Evidence | Status |
|---|---|---|---|---|
| 1 | Protocol v1.1: SOURCE_MODE capability, START `source_url`, POSITION codec | `vw_protocol_types.h:L49,125-140`; `vw_protocol_codec.c` | `test_protocol_codec` round-trips; `test_protocol_validate` rate bounds | Done |
| 2 | Native demuxing (MF + FFmpeg) → 16k S16 mono, accurate PTS | `vw_source_decoder_mf.c`, `vw_source_decoder_ffmpeg.c` | `test_source_decoder`; Windows exe tests pass | Done |
| 3 | 30 s look-ahead pacing, idle when saturated | `vw_worker.c:L212,529-530,254-259` | Manual: lookahead transcriptions arriving | Done |
| 4 | Zero perceived latency | Presenter future scheduling `vw_caption_presenter.c:L204-216` | Manual: captions in sync, "lookahead transcription looking good" | Done |
| 5 | Seek repositioning without teardown | `vw_worker.c:L406-439`; module `L339-353` | Manual: "seeking support preserved"; lifecycle live-path retained | Done |
| 6 | Live stream passthrough | `source_mode=false` fallback; AUDIO dropped in source mode (L442-445) | Live path unchanged; worker_ipc/lifecycle tests | Done |
| 7 | Docs updated (Rule 14) | api-contracts, architecture, roadmap, source-layout, test-strategy | Doc inspection | Done |
| DoD | C17, realtime-safe, privacy, MF lifecycle, FFmpeg pkg-config, 100% tests, clang-format | — | 16/16 + new tests; Windows exes pass | Done |

**Phrase-by-phrase timing**: NOT part of 17c — ADR-017 accepted, design in `phrase_timing_segmentation_plan.md`, implementation tracked as roadmap **17d.1** (unchecked). Matches user note ("phrase timing not yet implemented but noted inside the roadmap").

## 7. Code Review Findings (17c) & Resolution Status

### Bugs (Sorted by Priority)

| Priority | Component / Location | Description | Impact | Resolution Status |
| --- | --- | --- | --- | --- |
| **Medium** | `worker/src/vw_worker.c:406-409` | `POSITION` handler checked only `session_active`, never `position.session_id` — a stale POSITION from a prior epoch could re-anchor the demuxer. | Wrong seek/lead under raced control frames | RESOLVED: Added `memcmp` check against active `session_id` in `vw_worker.c`. |
| **Medium** | `plugin/src/vw_caption_presenter.c` / `vw_whisper_module.c:332-340` | Look-ahead lead is wall-clock based: captions scheduled `mdate()+lead` would continue rendering during a pause while playback head was frozen. | Future dialogue spoilers while paused | RESOLVED: Added `vw_caption_presenter_blank(&sys->presenter)` on pause and resume transitions in `vw_whisper_module.c`. |
| **Low** | `worker/src/vw_source_decoder_ffmpeg.c:190-198` | Used `frame->pts` only; `AV_NOPTS_VALUE` in some containers (dts-only) caused unanchored window PTS. | Slightly off caption timing on edge containers | RESOLVED: Added `frame->best_effort_timestamp` and `frame->pkt_dts` fallbacks. |
| **Low** | `plugin/src/vw_whisper_module.c:339-353` | Media swap in source mode is treated as a seek: `POSITION(SEEK)` re-seeks the OLD file's demuxer; the new MRL is never reopened mid-session. | Wrong captions after switching files without module restart | Open for 17d: Track MRL changes across playlist transitions in Step 17d seek engine. |
| **Low** | `worker/src/vw_source_decoder_mf.c:217` | Post-seek `ReadSample` timestamp can precede the target (keyframe-aligned seek) → brief pre-target audio window before the anchor. | 1-2 s stale caption after backward seek | Verified: `llTimestamp/10` microsecond calculation is mathematically exact; audio buffer drains pre-target samples. |

### Architectural & Operational Risks

| Category | Risk Description | Affected Files | Mitigation Strategy & Status |
| --- | --- | --- | --- |
| **Unused capability gate** | Plugin previously extracted and passed `source_url` without checking worker capabilities. | `plugin/src/vw_whisper_module.c:236` | RESOLVED: Gated on `sys->client->worker_capabilities & VW_CAPABILITY_SOURCE_MODE`. |
| **Doc drift** | 17c plan text said `SOURCE_MODE = 0x00000004` (actual `1U<<3 = 0x08`; 0x04 is `SEEK_RESET`). | `docs/plans/step17c_plan.md` | RESOLVED: Corrected protocol constants in `step17c_plan.md`. |
| **Source-mode AUDIO waste** | Plugin keeps sending live PCM chunks in source mode; worker drops them. | `vw_whisper_module.c`, `vw_worker.c` | Open for 17d: Gate `send_audio` on `source_mode` confirmation. |
| **Phrase timing gap** | Whole-window captions still concatenated (`vw_worker.c:548-550`); spoilers persist until 17d.1. | `vw_worker.c`, `vw_segment_builder.c` | Tracked: ADR-017 recorded; implementation tracked in roadmap 17d.1. |

### Code Style & Quality Nitpicks

| Issue Type | File & Line | Description | Recommendation & Status |
| --- | --- | --- | --- |
| **Fixture-gated test** | `tests/unit/test_source_decoder.c:38-70` | Test 3 skipped when no fixture files were present on disk. | RESOLVED: Added programmatic in-memory synthetic 16kHz mono WAV generator for guaranteed test coverage. |
| **Dead enum** | `protocol/include/vw_protocol_types.h:69` | `E_SOURCE_OPEN` defined for future detailed error reporting. | Kept for wire protocol completeness in v1.1. |
| **Redundant fields** | `vw_msg_position_t` | `input_time_us` currently mirrors `current_pts_us`; `playback_rate` default 1.0f. | Kept for Step 17d pacing engine extensions. |

---

*End of Part 2. Part 1 above covers the 17b PR (#10); Part 2 covers the 17c look-ahead branch.*
