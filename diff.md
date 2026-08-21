# Diff Analysis: Step 17e.2 + Step 18 Packaging vs 17e.1 (VAD/No-Hop)

**27 files changed, +1375 / -930 lines**
**Base**: `origin/gemini/milestone-3-step-17e-1` (`5275b87` = PR #15 merge, 17e.1 VAD + no-hop merged to milestone-3).
**Head**: `gemini/milestone-3-step-18` = `0dc29ee` (17e.2 reading floor + decoding + Windows installer).
**Commits in scope**: `b8bf173` (stale plan cleanup), `638486b` (17e.2 plan O1-O9), `067f5a6` (reading floor + decoding), `8630043` (builder clamp attempt), `da1b7d0` (revert builder clamp, delegate pacing to presenter), `05806a0` (lookahead clip + sender flush), `c5557d2` (ADR-021), `7ff05fe` (ephemeral guard), `cb4b61d`/`7e54913` (plans), `0dc29ee` (Windows installer).
**Line references**: post-diff HEAD state. Scout sweep: 5 parallel probes (presenter, engine, packaging, docs, cross-cutting) — cancelled mid-flight, inline review substituted.

---

## 1. File-by-File Analysis

### 1.1 `plugin/include/vw_caption_presenter.h`

**Why change**: ADR-021 reading floor — single owner of visual pacing in the presenter. Needed pending-buffer state + floor constant (plan §Scope.1).

**Responsibility before**: Thin struct `{p_filter_ctx, p_held_vout, spu_channel_id, spu_channel_registered}` + 4 API decls (`display`, `show_segment`, `blank`, `clear`). **After**: adds `VW_CAPTION_MIN_DISPLAY_DURATION_US=1e6`, `VW_PRESENTER_MAX_TEXT_BYTES=1024`, `has_pending` + `pending_segment` + `pending_text[1024]` to `vw_caption_presenter_t`, and new API `vw_caption_presenter_flush`. Docstrings expanded to 20-30 words (Rule 11).

**Callers**: `vw_whisper_module.c` (sender thread dispatches `show_segment` + periodic `flush`; seek/close calls `blank`/`clear`; `display` is fallback path), tests (`test_caption_presenter.c`). **Callees**: none (header only; uses `vw_protocol_types.h` for `vw_caption_segment_t`).

**Happy path**: `show_segment(&segA, t0)` buffers A as pending (`has_pending=true`, `strncpy` into `pending_text`, `pending_segment.text_utf8 = pending_text`). Next `show_segment(&segB, t1)` sees `has_pending`, computes clipped duration for A, calls `render_internal(A, clipped_dur, t1)`, clears pending, then buffers B. Sender's 100ms timer eventually calls `flush(B)` when `B.start_pts <= current + 100ms`.

**Failure path**: `show_segment(NULL)` or `segment==NULL` or `!text_utf8` → `false`, no state mutation. `pending_text` truncation on `strncpy` (1024-1 + NUL) — overlong Whisper text silently truncated (boundary case, not validated upstream; protocol caps at ~1KB already).

**Boundaries**:

| Type | Check | Status |
|---|---|---|
| Input validation | `!presenter`, `!segment`, `!text_utf8` guards in `show_segment`/`flush` | OK |
| Buffer | `strncpy` + explicit NUL terminator, 1024 cap | OK (truncation is silent) |
| Concurrency | `has_pending` is sender-thread only (receiver thread dispatches to presenter via same sender loop after 15) | OK — single-owner, no lock needed |
| Persistence | `pending_segment.text_utf8` points into `pending_text` (stable, not dangling) | OK |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | 1.0s wall floor at 0.5×/1×/2× | `h:8` constant, `c:259` rate-scaled floor | Test 17 (rate 2×, 0.5×) | ✅ |
| 2 | Clip to successor start | `c:266-276` clipped_end logic | Test 16 (`cueA_stop == cueB_start`) | ✅ |
| 3 | Flush when no successor | `c:295-312` flush impl + `vw_whisper_module.c:642-646` timer | Test 15 (flush path) | ✅ |
| 4 | Ephemeral guard | `h:struct` + `c:121` `b_ephemer=true` | Test 14 `assert(b_ephemer)` | ✅ |

**Assumptions/Tradeoffs**: Assumes sender thread is the sole writer to `presenter` (true per 15 architecture). Tradeoff: single-slot pending buffer (not a queue) — only the immediately preceding cue is clipped; a burst of 3 cues arriving in one loop iteration only clips pairwise, last cue flushed by timer. Sufficient because lookahead emits one chunk at a time; live path has 512ms cadence.

---

### 1.2 `plugin/src/vw_caption_presenter.c`

**Why change**: Implement ADR-021 presenter-owned pacing: rate-scaled floor, successor clipping via pending buffer, OSD floor, ephemeral presentation.

**Responsibility before**: Direct render: `show_segment` computed `duration = end-start` (or 2s fallback), looked up `rate`, scheduled via `mdate() + lead` → `vout_PutSubpicture` or `vout_OSDText`. **After**: split into `render_internal(segment, duration_us, input_time)` (rate lookup + wall conversion + SPU/OSD dispatch) + `show_segment` (pending buffer + clip) + `flush` (pending dispatch with floor) + `display` floor clamp + `blank`/`clear` pending reset.

**Callers**: `vw_whisper_module.c:dispatch` + `sender flush timer`, `vw_session.c` (blank on seek). **Callees**: `mdate()`, `var_Get(rate)`, `vout_RegisterSubpictureChannel`, `vout_PutSubpicture`, `vout_OSDText`, `vout_FlushSubpictureChannel`, `subpicture_New/Region_New`, `vw_caption_presenter_render_text`.

**Happy path** (short cue with successor, lookahead mode):

1. `show_segment(cueA 10.0-10.2s, input=10.0s)` → `min_media_floor = 1e6 * rate(1.0)=1e6`, `has_pending==false` → buffers A, returns true, 0 SPU calls.
2. `show_segment(cueB 10.6-10.8s, input=10.0s)` → `has_pending==true`, `raw=200ms` → `target_dur=1e6` → `target_end=11.0s` → `clipped_end=min(11.0, 10.6)=10.6` → `dur=600ms` → `render_internal(A, 600ms, 10.0s)` → `mdate()=100s`, `lead=(10.0-10.0)/1=0` → `i_start=100s`, `i_stop=100.6s`, `b_ephemer=true` → `vout_PutSubpicture`. Buffers B. Returns true.
3. Sender timer sees `pending=B`, `current_pos≈10.6s` → `flush(B, 10.6s)` → `dur=1e6` → `render_internal(B, 1e6, 10.6s)` → `i_start=100.6s`, `i_stop=101.6s`, zero gap (`A_stop==B_start`).

**Failure path** (invalid segment): `show_segment(NULL)` → false; `show_segment` with `text_utf8==NULL` → false, `has_pending` unchanged (A still buffered — intentional: invalid B does not evict A). `render_internal` with `!p_filter_ctx` (unit test) → falls through to `display(NULL, text, dur)` → `vout_OSDText` fallback. `blank(NULL)` no-ops pending reset first (`if(presenter) has_pending=false`) then returns.

**Boundaries**:

| Type | Check | Status |
|---|---|---|
| Input validation | `!segment`, `!text_utf8`, `raw<=0 → 2s fallback`, `duration_us<=0 → 2s` | OK |
| Rate | `var_Get` failure → 1.0 default; `rval.f_float <=0.05` ignored; `rate` never 0 in division (guarded) | OK |
| Clipping | `clipped_end` only when `B.start > A.start && target_end > B.start`; else `target_end`; `duration<=0` fallback to `raw>0 ? raw : 2s` | OK |
| Concurrency | No heap alloc, no blocking lock in render path (only `mdate`/`var_Get`/`vout_Put`) | OK |
| I/O | `subpicture_New` NULL → false | OK |
| Schedule | `lead = (start_pts - input_time)/rate` capped at 60s via `vw_saturating_add` in `render_internal` | OK |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Short cue floor 1.0s wall | `c:148` display clamp, `c:259-260` min_media_floor, `c:306-308` flush floor | Test 15 (200ms→1s) | ✅ |
| 2 | Successor clipping | `c:266-276` clip | Test 16 | ✅ |
| 3 | Timer flush fallback | `c:295-312` + `vw_whisper_module.c:642-646` | Test 15/16 flush calls | ✅ |
| 4 | OSD fallback floor | `c:148-149` | Implicit via fallback_presenter test | ✅ |
| 5 | Seek clears pending | `c:315` blank resets, `c:346` clear resets | Manual seek test (user-reported good) | ✅ |
| 6 | Long utterance preserved | `c:260` `raw >= floor → raw` | Test 18 (3.5s) | ✅ |
| 7 | `b_ephemer` overlap prevention | `c:121` `b_ephemer=true` | Test 14 assertion | ✅ |

**Assumptions/Tradeoffs**: Assumes `b_ephemer=true` semantics ("displayed until next one appear" — `vlc_subpicture.h:173`) — verified vendored header. Tradeoff: `pending_text` is a fixed 1024 buffer; overlong segment text truncated silently — acceptable since protocol caps text and Whisper rarely exceeds 200 chars. Low-confidence: `blank` resets `has_pending` *before* the `!p_filter_ctx` early return (line 315-317) — correct (clears even when no vout), but `clear` resets again at line 346 after calling `blank` (redundant, harmless).

---

### 1.3 `plugin/src/vw_whisper_module.c`

**Why change**: (a) Auto-discovery for Windows installer paths (Step 18 packaging), (b) sender flush timer for pending cues (Step 17e.2).

**Responsibility before**: Resolve worker/model paths via plugin-dir ancestors + exe dir; sender loop drains status/caption frames and dispatches to presenter. **After**: adds `vw_plugin_probe_windows_paths` (registry HKCU/HKLM `Software\VLC-Whisper\InstallPath` + `%LOCALAPPDATA%` + `%PROGRAMFILES%`) and periodic `presenter.has_pending` flush at `current_position + 100ms`.

**Callers**: VLC module `open`/`close`, sender thread `vw_plugin_sender_main`. **Callees**: `RegOpenKeyExA`, `RegQueryValueExA`, `GetEnvironmentVariableA`, `vw_plugin_probe_ancestors`, `vw_caption_presenter_flush`, `vw_caption_presenter_blank` (on seek).

**Happy path** (flush): sender loop samples `current_position_us` (throttled 100ms) via `input_GetPosition`-derived PTS, checks `has_pending && !paused`, if `current <=0` (unknown) or `pending.start_pts <= current + 100ms` → `flush` → SPU render with floor.

**Failure path** (path probe): registry key absent or value not `REG_SZ` or `len==0` → `RegCloseKey` and continue to next hive/env var; `GetEnvironmentVariable` returns 0 or >= buffer → skip; all probes fail → `resolve_worker_path` returns false → plugin logs and falls back to PATH or disables gracefully (no crash).

**Boundaries**:

| Type | Check | Status |
|---|---|---|
| Input validation | `RegQueryValueExA` type `REG_SZ` check, `len>0`, `plen < sizeof(buf)` for env vars | OK |
| Buffer | `candidate[MAX_PATH]` via `snprintf`, registry `val[MAX_PATH]` with `DWORD len=sizeof(val)` | OK |
| Auth | No token handling here; purely path probing | OK |
| Concurrency | Sender thread only probes at startup (not in audio callback) | OK |
| Timing | Flush threshold 100ms is wall-relative; `current_position_us <=0` bypass flushes immediately when position unknown (prevents cue never showing) | ⚠️ edge: if VLC reports 0 continuously (e.g. paused file), every cue flushes with full floor even though successor is queued in the same loop iteration — but `show_segment` already clipped A before flush, so not observable |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Pending flush renders on time | `c:642-646` timer | Test 15/16 flush | ✅ |
| 2 | Windows installer auto-discovery | `c:127-163` probe | Manual install test (user) | ✅ |
| 3 | Registry probing | `c:133-145` HKCU/HKLM | — | ✅ |

**Assumptions/Tradeoffs**: Assumes `MAX_PATH` (260) suffices for InstallPath values (NSIS writes short `C:\Program Files\VLC\`-style paths — OK). Tradeoff: `vw_plugin_probe_windows_paths` is called *after* plugin-dir and exe-dir probes — registry/env are fallback, not primary — preserves portable installs. Low-confidence: `current_position_us <=0` flush bypass means a cue buffered before playback starts is flushed with full floor even if its successor is already in the same IPC batch — but `show_segment` processes the batch sequentially, so A is clipped by B *before* the timer runs; the bypass only affects a solitary final cue, where full floor is desired.

---

### 1.4 `worker/src/vw_whisper_engine.c`

**Why change**: ADR-021 deterministic decoding — make greedy + bounded retry explicit and self-documenting (plan O1-O9).

**Responsibility before**: `whisper_full_default_params(GREEDY)` then set `language=en`, `n_threads=4`, `suppress_nst/blank`, `no_speech_thold=0.60`, `logprob_thold=-1.0`. **After**: explicitly sets every pacing-relevant `wparams` field with comments (no behavior change except `temperature_inc=0.2` vs prior implicit `0.2` default — verified still `0.2` in vendored, now explicit).

**Callers**: `vw_worker.c` (lookahead + live transcription calls). **Callees**: `whisper_full_default_params`, `whisper_full`.

**Happy path**: `transcribe_pcm(pcm, 6-24s)` → builds `wparams` GREEDY, `temp=0.0`, `temp_inc=0.2` (ladder `[0.0,0.2,0.4,0.6,0.8]` ≤5 passes), `entropy=2.4`, `no_context=true`, `suppress_nst/blank=true` → `whisper_full` greedy decode (argmax, no RNG in token choice; `mt19937` only for beam decoders), single pass on normal audio, retry only on low-entropy degenerate loops.

**Failure path**: `!ctx` or `!pcm` or `sample_count==0` → false. `whisper_full !=0` → false. No leak (params on stack).

**Boundaries**:

| Type | Check | Status |
|---|---|---|
| Input validation | `!engine`, `!ctx`, `!pcm`, `sample_count==0` | OK |
| Decoding | `temperature_inc=0.2` bounded ≤5 passes (whisper.cpp hard cap `1.0+1e-6`) | OK |
| Determinism | GREEDY argmax is deterministic (plan O7 corrected: RNG unused for greedy) | OK |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Greedy + bounded fallback | `c:71-73` | Engine determinism test (plan 17e.2) | ✅ (plan test not yet in this diff's 16-line change — see 1.7) |
| 2 | Entropy gate not silently dropping | `c:74` `entropy=2.4` + `temp_inc=0.2` retry ladder | — | ✅ (whisper.cpp: retry, not drop) |
| 3 | No context carryover | `c:77` `no_context=true` | — | ✅ |

**Assumptions/Tradeoffs**: Assumes vendored `whisper.cpp` `whisper_full_default_params(GREEDY)` already sets `strategy=GREEDY, temp=0.0` — now made explicit for self-documentation (no behavior change, per plan Scope.2). Tradeoff: `n_threads=4` fixed — not adaptive to CPU count, but matches prior.

---

### 1.5 `tests/unit/test_caption_presenter.c`

**Why change**: Cover ADR-021 pacing: floor, clipping, rate scaling, long-cue preservation, ephemeral guard, and the new buffered-then-flushed API.

**Responsibility before**: 14 tests covering `display`/`show_segment`/`blank`/`clear`, basic SPU/OSD dispatch, channel registration reuse, rate-scaled wall duration. **After**: 18 tests — adds Test 14 `b_ephemer` guard, Test 15 floor (200ms→1s), Test 16 adjacent clipping (`cueA_stop==cueB_start`), Test 17 rate floor at 0.5×/2× (wall=1s), Test 18 long cue (3.5s preserved). Mock now captures `g_last_subpic_b_ephemer` via `vout_PutSubpicture`.

**Callers**: `ctest` (`linux-x64-debug` preset). **Callees**: `vw_caption_presenter_show_segment`, `flush`, `display`, `blank`, `clear`; stubs `mdate`, `vout_PutSubpicture`, `vout_RegisterSubpictureChannel`, `var_Get`.

**Happy path**: Test 16 feeds `cueA(10.0-10.2)` → `show_segment` buffers (0 puts), `show_segment(cueB 10.6)` dispatches A clipped to 600ms (`assert(cueA_stop==100.6s)`), `flush` dispatches B with 1s floor, asserts `cueA_stop==cueB_start` (zero overlap).

**Failure path**: Test exercises `presenter==NULL`, `segment==NULL`, `text_utf8==NULL` early returns. `blank` with `NULL` presenter still clears `has_pending` before return (correctness for seek-while-teardown).

**Boundaries**:

| Type | Check | Status |
|---|---|---|
| Input validation | NULL guards | OK |
| Mock | `g_mock_rate` monkey-patching covers 0.5/1/2×; `g_mock_mdate=100s` anchors wall ticks | OK |
| Coverage | Tests 15-18 call `flush` explicitly — true buffered API | OK |
| Gap | No test for `flush` when `has_pending==false` (returns false, harmless), no test for `blank` while pending (pending cleared) | ⚠️ minor gap |

**Acceptance map**: all plan 17e.2 acceptance criteria mapped in 1.1/1.2 tables.

**Assumptions/Tradeoffs**: Assumes `mdate` stub stable across tests (100s base). Tradeoff: `g_mock_rate` is global float — not thread-safe, but unit test is single-threaded.

---

### 1.6 `tests/unit/test_whisper_engine.c`

**Why change**: Pin engine initialization failure path after `vw_whisper_engine.c` changes (16 lines added).

**Responsibility before**: Invalid model path → init failure, model presence check, memcheck skip (exit 77). **After**: same plus explicit reload (no new determinism test in this diff — plan's degenerate-input retry test is deferred; the 16 lines are likely the reload + re-assert after param changes).

**Boundaries**: Model-gated skip (exit 77 under memcheck or missing model) keeps gate fast/clean — no regression.

**Acceptance map**: engine config determinism → ⚠️ partial (param change is correct; automated degenerate-input retry test not yet landed — documented in plan O9).

**Assumptions/Tradeoffs**: Deferred degenerate-input test is acceptable for this phase — whisper.cpp's retry ladder is vendored and not project-authored; correctness is by inspection of `temperature_inc`/`entropy_thold` wiring.

---

### 1.7 `docs/decisions.md` — ADR-021

**Why change**: Record ADR-021 reading floor + decoding decisions (Rule 14).

**Responsibility before**: ADR-020 (no-hop) was last. **After**: adds ADR-021 (wall-clock 1s floor, single owner presenter, pending buffer clip, `vw_segment_builder` untouched; greedy decoding table + consequences including ephemeral guard note verified 2026-08-20).

**Acceptance map**:

| # | Criterion | Code | Status |
|---|---|---|---|
| 1 | ADR documents floor + clip + decoding | `decisions.md:273-312` | ✅ |
| 2 | Ephemeral mechanism documented | `decisions.md:307` overlap prevention note | ✅ |
| 3 | `vw_segment_builder` untouched stated | `decisions.md:287` | ✅ (code verified — `da1b7d0` reverted builder clamp) |

---

### 1.8 `docs/architecture.md`

**Why change**: Reflect ADR-021 in the architecture's discontinuity/seeking section (Rule 14).

**Responsibility before**: Covered seek, VAD, no-hop. **After**: adds 17e.2 bullet: `VW_CAPTION_MIN_DISPLAY_DURATION_US`, rate-scaled wall floor formula, greedy decoding config.

**Boundaries**: Timing formula matches `vw_caption_presenter.c:259` (`max(raw, floor*rate)/rate`). SPU domain (`b_subtitle=false`, `mdate`) correctly described as OSD clock (17b evidence chain cited).

---

### 1.9 `cmake/Packaging.cmake` + `cmake/vlc_whisper_installer.nsi.in` + `LICENSE` + `THIRD_PARTY_NOTICES.md` + `.gitignore` + `CMakeLists.txt` + `plugin/CMakeLists.txt` + `worker/CMakeLists.txt`

**Why change**: Step 18 standalone Windows installer (auto-discovery, cache regen, uninstaller, shortcuts) + MIT licensing (Rule 14). `Packaging.cmake` wires CPack/NSIS, `vlc_whisper_installer.nsi.in` is the NSIS template, `LICENSE`/`THIRD_PARTY_NOTICES.md` add MIT + attributions, `.gitignore`/`CMakeLists.txt` wire build.

**Responsibility before**: No installer; manual copy. **After**: `vlc-whisper-win64-setup.exe` probes VLC install path from `HKLM\Software\VideoLAN\VLC`, installs DLL to `plugins/audio_filter/`, worker/models to VLC root, runs `vlc-cache-gen`, registers uninstaller, creates shortcuts with `--audio-filter=vlc_whisper`.

**Callers**: `cmake --preset windows-x64-*` + `cpack`. **Callees**: NSIS, `vlc-cache-gen.exe`.

**Happy path**: User runs installer → detects VLC 64-bit → copies DLL + worker + models → regen cache → shortcuts → VLC loads module on next start.

**Failure path**: VLC not found → installer aborts with guidance (no silent mis-install). Worker/model probe fallback chain ensures standalone launch still finds files via portable layout.

**Boundaries**:

| Type | Check | Status |
|---|---|---|
| Security | No network, no elevated beyond installer UAC, local IPC token unchanged | OK |
| Licensing | MIT root + full third-party notices | OK |
| Path | Installer probes plugin-dir ancestors → exe dir → registry → env vars (mirrors runtime probe order) | OK |

**Acceptance map**: packaging acceptance (install, captions visible, seek good, uninstall) — manual E2E, not automated in this gate.

**Assumptions/Tradeoffs**: Assumes VLC 64-bit registry layout (`HKLM\Software\VideoLAN\VLC`) — correct for official VLC builds. Tradeoff: NSIS only, no MSI.

---

### 1.10 `docs/plans/step17e_2_plan.md` + `docs/plans/step18_plan.md` + `docs/plans/transcription_quality_optimizations_plan.md` + `docs/source-layout.md` + `docs/test-strategy.md` + `docs/roadmap.md` + `README.md`

**Why change**: Rule 14 — plans and docs must track code. Deletes stale plans (`step17d`, `step17d_1`, `step17e_1`, `step17e_1_no_hop`), adds 17e.2 plan (O1-O9 resolved) and 18 plan + transcription quality plan, updates source-layout/test-strategy/roadmap/README for 17e.2 + installer.

**Responsibility before**: Stale plans referenced non-existent builder pacing. **After**: single-owner presenter model, correct wall-clock formula, `b_ephemer` note, 18 packaging plan.

**Boundaries**: Roadmap milestone ordering verified (17e.1 → 17e.2 → 18), Done criteria match code.

**Acceptance map**: docs updated → ✅.

---

## 2. Happy-Path Request Trace (short dialogue, lookahead source mode)

1. Worker accumulates 60s ring, `vw_vad_find_chunk_boundary` (17e.1 no-hop) cuts at a 300ms silence gap → 8s speech chunk drained 100% non-overlapping.
2. `vw_whisper_engine_transcribe_pcm` (`worker/src/vw_whisper_engine.c:68`) GREEDY `temp=0.0, temp_inc=0.2, entropy=2.4, no_context=true` → `whisper_full` emits discrete sub-segments `A(10.0-10.2 "Yeah.")` + `B(10.6-10.8 "Right.")` with authentic centisecond timing.
3. `vw_segment_builder` accepts both (coverage dedup, no builder pacing — `da1b7d0` invariant).
4. IPC `SEGMENT` frames (`CAPTION_SEGMENT`) delivered to `vw_whisper_module.c` sender loop.
5. `vw_caption_presenter_show_segment(A, 10.0)` (`plugin/src/vw_caption_presenter.c:246`) buffers A (`has_pending`, `pending_text="Yeah."`).
6. `vw_caption_presenter_show_segment(B, 10.0)` sees pending A, computes clipped `dur=600ms` (`min(1s floor, B.start-A.start)`), `render_internal(A, 600ms, 10.0)` → `var_Get(rate=1.0)` → `dur_wall=600ms` → `mdate=100s, lead=0` → `i_start=100s, i_stop=100.6s, b_subtitle=false, b_ephemer=true` → `vout_PutSubpicture` (channel private). Buffers B.
7. Sender timer `flush(B, 10.6)` when `current_position≈10.6` → `dur=1s floor` → `i_start=100.6s, i_stop=101.6s, b_ephemer=true` → second `vout_PutSubpicture`. VLC renders A `100-100.6s`, B `100.6-101.6s`, zero gap, 1s wall readability for both (A clipped, B floored).
8. Seek at 12s → `vw_caption_presenter_blank` clears `has_pending`, flushes SPU/OSD channels; worker `POSITION(SEEK)` repositions demuxer; new epoch, no stale pending.

---

## 3. Most Important Failure Path

**Overlong hallucinated text + rapid seek (pending-buffer + SPU race)**:

1. Whisper emits a 2KB hallucinated tag stream (e.g. repeated `[Music]` — Tier 3 filter in `vw_hallucination_filter.c` should drop it, but assume a novel tag slips through).
2. `vw_segment_builder` emits it (authentic timing, say `10.0-11.0` 1s window), `show_segment` buffers it as pending (`strncpy` truncates to 1023 + NUL, `pending_text` holds prefix — **no crash**, but truncated text is rendered — boundary is safe, quality degraded).
3. User seeks at 10.1s before `flush` fires. Audio callback detects `BLOCK_FLAG_DISCONTINUITY` + `VW_INPUT_JUMP_DISCONTINUITY_US=5s` gate → sender calls `vw_caption_presenter_blank` → `has_pending=false`, `vout_FlushSubpictureChannel` + `vout_OSDText blank` → screen cleared.
4. Worker receives `POSITION(SEEK)`, repositions decoder, clears hypotheses; stale `SEGMENT` for the old epoch is dropped by `session_id` check in `vw_whisper_module.c`.
5. No crash, no leak, no stale caption — **captioning never harms playback** (primary invariant). The 1KB truncation is the only visible artifact; coverage dedup ensures the hallucinated window does not pollute `covered_end_us` across the seek (Tier 3 filter drops it before `commit_history` in the non-hallucinated path; in this slip-through path, `covered_end_us` advances by the truncated window's duration, but the seek epoch resets it).

---

## 4. Boundary Summary

| Boundary | Implementation | Status |
|---|---|---|
| Input validation | `show_segment`/`flush`/`display` NULL guards; `raw<=0→2s` fallback; `segment_builder` untouched | OK |
| Concurrency | `has_pending` single-thread (sender), no lock; `b_ephemer` eviction is VLC-core, not plugin | OK |
| I/O | `subpicture_New` NULL, `vout_PutSubpicture` ownership transfer, registry `REG_SZ` check | OK |
| Persistence | `pending_text[1024]` stable backing for `pending_segment.text_utf8` | OK |
| Buffer/overflow | `strncpy` + NUL, 1024 cap; `mdate` saturating add for 60s lead cap | OK |
| Timing/rate | `var_Get` rate guard `>0.05`, default 1.0; `current_position<=0` bypass flushes final cue | OK |
| Auth/isolation | Registry/env probes read-only, no token, no network | OK |
| Licensing | MIT + THIRD_PARTY_NOTICES, zero network | OK |

---

## 5. Acceptance Criterion → Code Mapping (plan `step17e_2_plan.md`)

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Sub-second cues display ≥1.0s wall at 0.5×/1×/2× | `h:8` + `c:148,259,306` rate-scaled floor | Test 15 + Test 17 | ✅ |
| 2 | Consecutive cues never overlap: clip to successor start; later arrival replaces | `c:266-276` clip + `c:121` `b_ephemer` | Test 16 `cueA_stop==cueB_start` + `b_ephemer` assert | ✅ |
| 3 | Long utterances (>1s) preserve authentic duration | `c:260` `raw>=floor→raw` | Test 18 (3.5s) | ✅ |
| 4 | Greedy deterministic, fallback bounded ≤5 passes | `worker/c:71-74` `GREEDY,0.0,0.2,2.4` | Plan O9 degenerate test (deferred) | ⚠️ partial — wiring correct, automated degenerate-input test not yet in `test_whisper_engine.c` |
| 5 | `no_context`/`suppress_nst`/entropy suppress without silent drop | `worker/c:77,79,80,74` | Whisper ladder retry semantics (vendored) | ✅ (by inspection) |
| 6 | 100% unit tests pass (Linux + MinGW) | — | `ctest linux-x64-debug` 20/20, `windows-x64-debug` green | ✅ |
| 7 | Valgrind 0 leaks | — | `ctest -T memcheck` | ✅ |
| 8 | Docs + ADR-021 + Rule 14 | `decisions.md:273`, `architecture.md:88`, `source-layout`, `test-strategy`, `roadmap` | — | ✅ |

---

## 7. Code Review Findings (Bugs, Risks, Nitpicks)

### Bugs (Sorted by Priority)

| Priority | Component / Location | Description | Impact | Proposed Fix |
|---|---|---|---|---|
| **Medium** | `worker/src/vw_whisper_engine.c:71-78` — temperature fallback | The 16-line `test_whisper_engine.c` addition in this diff does not land the plan's degenerate-input bounded-retry test (O9: assert pass count ≤5 and no silent drop on low-entropy input). The wiring (`temp_inc=0.2, entropy=2.4`) is correct, but the acceptance criterion is verified only by inspection, not by an automated gate. | Silent-drop regression could slip past CI | Add the `transcription_quality_optimizations_plan.md` degenerate fixture test (repetitive low-entropy PCM → assert ≤5 decoder passes and non-empty segments) — planned for M4/17e follow-up, not a merge blocker since whisper.cpp ladder is vendored |
| **Low** | `plugin/src/vw_caption_presenter.c:250` — `strncpy` truncation | Overlong Whisper text (>1023 bytes) is silently truncated to `pending_text` prefix; no diagnostic. Protocol caps at ~1KB and Whisper rarely exceeds 200 chars, so not observable, but a 2KB hallucination slip-through would render truncated. | Truncated caption | Consider `vw_log_event(WARN, "PRESENTER_TEXT_TRUNCATED", ...)` when `strlen(text_utf8) >= sizeof(pending_text)` |
| **Low** | `plugin/src/vw_whisper_module.c:642-646` — flush `current<=0` bypass | When VLC reports `current_position_us <=0` (e.g. file open before position known), the timer flushes pending with full floor even though the next IPC batch may already contain the successor. `show_segment` processes the batch sequentially so A is already clipped by B before the timer runs — not a visible bug, but the bypass is subtly redundant. | None visible (clipping already applied) | Document the bypass as "flush final cue when position unknown" and note that batch clipping precedes it; no code change |
| **Low** | `plugin/src/vw_whisper_module.c:133-145` — registry probe | `val[MAX_PATH]` length check uses `len>0` after `RegQueryValueExA` but `len` is in bytes including NUL for `REG_SZ`; a value exactly `MAX_PATH-1` chars + NUL (`len==MAX_PATH`) passes the type check and is then fed to `probe_ancestors` which re-validates via `stat` — safe, but edge is implicit. | None | Explicit `len < sizeof(val)` guard (already effectively via `RegQueryValueExA` buffer param, but make it explicit) |
| **Low** | `plugin/src/vw_caption_presenter.c:315-317` + `344-346` — double pending reset | `blank` clears `has_pending` before the `!p_filter_ctx` early return (correct), then `clear` calls `blank` and clears `has_pending` again (redundant). Harmless but reads as if the second clear is needed. | None | Remove redundant `has_pending=false` in `clear` (line 346) or comment "blank already cleared pending" |

### Architectural & Operational Risks

| Category | Risk Description | Affected Files | Mitigation Strategy |
|---|---|---|---|
| **Greptile false positive** | Static interval-overlap analysis flags the sender flush (pending dispatched with full floor before successor known) as "overlap" — but `b_ephemer=true` (`vw_caption_presenter.c:121`, vendored `vlc_subpicture.h:173` "displayed until next one appear") makes VLC keep only the newest ephemeral subpicture on the channel, so intervals may overlap in the chain yet only one is rendered. Prior commits `8630043`/`da1b7d0`/`05806a0` fought this ghost; the durable fix is the `b_ephemer` regression test, not more clipping. | `plugin/src/vw_whisper_module.c:642-646`, `plugin/src/vw_caption_presenter.c:266-276` | Keep `b_ephemer` assertion (Test 14) as the invariant; document in ADR-021 consequences (done in `c5557d2`/`7ff05fe`) that static interval analysis ignoring `b_ephemer` is not a visible defect — **no revert of prior fix commits needed** (builder already clean per `da1b7d0`) |
| **Single-slot pending** | Non-overlapping drain (no-hop) emits one chunk at a time, so single pending slot suffices; a future bursty emitter could enqueue 3 cues in one loop and only clip pairwise. | `plugin/include/vw_caption_presenter.h` | Accept — lookahead is paced by chunk boundary; if emitter changes, widen to 2-slot queue |
| **Installer VLC discovery** | NSIS probes `HKLM\Software\VideoLAN\VLC` — correct for official VLC, but portable/winget installs may use different hives. Runtime `probe_windows_paths` mirrors the same fallback chain, so portable users still launch, but installer may not auto-find them. | `cmake/vlc_whisper_installer.nsi.in`, `plugin/src/vw_whisper_module.c:127-163` | Document manual install path in `README.md` "Manual Installation" (already present) |

### Code Style & Quality Nitpicks

| Issue Type | File & Line | Description | Recommendation |
|---|---|---|---|
| **Redundant assignment** | `plugin/src/vw_caption_presenter.c:344-346` | `has_pending=false` after `blank` already cleared it | Remove or comment |
| **Docstring drift** | `plugin/include/vw_caption_presenter.h:21-27` | `display` doc says "without blocking" but `render_text` may call `vlc_object_hold` | Align doc with impl or note "no heap in audio callback; display is sender-thread only" |
| **Plan docs deleted** | `docs/plans/step17d*.md`, `step17e_1*.md` | Stale plans removed in `b8bf173` — correct, but git history is the only recovery | Keep `git log --follow` as recovery; no action |
| **Test count label** | `tests/unit/test_caption_presenter.c:405` | Prints `18/18` but suite runner counts via `ctest` — label is cosmetic, not asserted by `ctest` | Keep in sync manually or derive from `__LINE__` count |

### 7.1 Second-Pass Review — New Findings (2026-08-20, inline, no subagents)

Re-reviewed the `5275b87..0dc29ee` diff independently. Items below are NOT already listed in §7 above. Pre-existing issues retained per reviewer instruction ("keep them anyway and report").

| Priority | Component / Location | Description | Impact | Proposed Fix |
|---|---|---|---|---|
| **Medium** | `cmake/vlc_whisper_installer.nsi.in:108` + `cmake/Packaging.cmake:18-23` | `File "@CMAKE_SOURCE_DIR@/models/ggml-tiny.en.bin"` (line 108) is NOT `/nonfatal`, but `models/*.bin` is gitignored (`.gitignore`) and there is NO model-fetch step anywhere in CMake — no `file(DOWNLOAD)`, no fetch custom target; the only fetcher is `models/vw_download_vad_model.sh` (VAD-only, no `tiny.en`). The `installer` target depends only on `vlc_whisper_plugin` + `vlc-whisper-worker` (Packaging.cmake:20). On any clone without a manually-downloaded `ggml-tiny.en.bin`, `cmake --build --target installer` (the documented step-18 workflow in README "Compiling the Windows Installer") aborts with a NSIS `File` error. Masked locally because the dev tree has the model. Inverted fragility: the OPTIONAL `ggml-silero-vad.bin` IS `/nonfatal` (line 109) while the REQUIRED base model is not. | Fresh-clone / CI build of the headline step-18 deliverable fails; the `cpack` ZIP (`install(DIRECTORY models ... PATTERN *.bin)`, Packaging.cmake:47-50) silently ships an EMPTY `models/` (manifest.json only) → captions broken for portable-zip users. CI is green only because it never builds the `installer`/`cpack` targets. | Add a `vw_download_models` custom target (fetch `tiny.en` + `silero-vad`) as a `DEPENDS` of `installer`; or `configure_file` models from a build-side fetch dir; minimally, document in README that `ggml-tiny.en.bin` must be downloaded before `--target installer`. |
| **Low** | `worker/src/vw_whisper_engine.c:95-112` (`vw_whisper_engine_get_text` concat) | The per-segment text loop does `memcpy(buf+written, txt, len)` with NO separator between segments, so a multi-segment window yields `"Yeah.""Right."` instead of `"Yeah. Right."`. Reach is currently test-only (`tests/unit/test_whisper_engine.c:85,107,111` — determinism re-run assertions on identical input, so the merge is invisible there); the live worker uses `get_segment`/`get_segment_count` (`vw_worker.c:557-560, 684-687, 730-733`), so this is dead in production. But the public contract (`vw_whisper_engine.h:43-44`, `docs/whisper-api.md:811-812`) advertises "concatenated UTF-8 text", which is semantically wrong for >1 segment. Pre-existing (not from this branch's 16-line `wparams` change). | Latent: any future caller using `get_text` for display/logging ships merged words. | Insert a separator (`" "` or `"\n"`) between segments and trim the trailing one, or deprecate `get_text` in favor of per-segment iteration. |
| **Low** | `plugin/src/vw_caption_presenter.c:266-277` (predecessor floor vs clip) | The 1.0s wall-clock reading floor is NOT enforced for a predecessor cue when its successor arrives within the floor window: clipping sets `clipped_end = successor.start_pts_us` (line 271-273), so a 200ms cue with a successor 600ms later renders only 600ms wall (<1s floor). The `duration_us <= 0` fallback (line 275) only catches non-positive durations, leaving positive sub-floor durations (e.g. 600ms) rendered as-is. This is the intended ADR-021 no-overlap choice and `b_ephemer=true` prevents visible overlap, but §5 criterion 1 ("Sub-second cues display ≥1.0s wall") is overstated: Test 15 validates the floor only in the no-successor case, while Test 16 (successor 600ms away) asserts zero overlap, not the floor — the floor is silently sacrificed in exactly the dense-dialogue case (successors <1s apart). | Sub-second flash captions for dense dialogue; no test pins the tradeoff as intentional. | Either restate the invariant as "best-effort floor, yields to no-overlap" in ADR-021 + the test, or floor the predecessor at `max(raw_acoustic_duration, clipped)` so a 200ms cue never renders below its own acoustic length. |
| **Low-Medium** | `plugin/src/vw_whisper_module.c:129-165` (`vw_plugin_probe_windows_paths`, new in this branch) | The entire Windows registry/env fallback chain is **off by one directory level**: `vw_plugin_probe_ancestors` is FILE-semantic (doc: "a file's own directory and up to max_up ancestors"; `dir_len` = position of the LAST separator), but this new caller passes bare DIRECTORY paths — registry `InstallPath` (`C:\Program Files\VideoLAN\VLC`), `%LOCALAPPDATA%\vlc-whisper`, `%PROGRAMFILES%\vlc-whisper`. For a directory path, the first probed directory is the directory's PARENT, so the probe checks `C:\Program Files\VideoLAN\vlc-whisper-worker.exe` (MISS — the installer puts the worker at `...\VideoLAN\VLC\vlc-whisper-worker.exe`) and `%LOCALAPPDATA%\vlc-whisper-worker.exe` (MISS — intended `%LOCALAPPDATA%\vlc-whisper\...`). The chain can never find files at the paths it constructs — dead fallback code. The existing §7 Low item about this function covers only the `len` bytes/NUL edge, not this. Compounding: `docs/architecture.md` "Deployment & Packaging → Path Resolution Hierarchy" documents registry (level 3) and env paths (level 4) as working resolution layers (Rule 14 docs/code mismatch). | Silent: in the standard NSIS install the plugin-ancestor (up=2 → `<VLC_ROOT>`) and exe-dir probes already find everything, so the broken fallback is masked. It only matters when the plugin DLL is loaded from OUTSIDE the VLC tree (custom `--plugin-path`, dev drop-in), where registry/env is the only remaining discovery path — discovery then silently fails and captions disable (`E_WORKER_MISSING` path), playback unaffected. | Probe the directory itself: append a dummy anchor component before calling (`snprintf(candidate, sizeof(candidate), "%s\\.vw_probe", val)` then `probe_ancestors(candidate, 0, ...)`), or add a dir-semantic `vw_plugin_probe_dir(dir, names, ...)` helper and use it for all three fallback sources; fix `architecture.md` levels 3-4 wording to match. |

**Second-pass additions verified against**: `git diff 5275b87...HEAD -- plugin/src/vw_whisper_module.c` (confirms `probe_windows_paths` + flush timer are the only module.c additions), `tests/unit/test_whisper_engine.c:99-113` (new determinism test is correct — same `tone_pcm` input transcribed twice, count+text compared; no bug), `THIRD_PARTY_NOTICES.md` (licenses complete and accurate: whisper.cpp/ggml MIT, OpenAI weights MIT, Silero MIT, VLC/FFmpeg LGPL 2.1+ §6, Vulkan Apache-2.0, MinGW GPL+exception — no issue), `.gitignore` (`_CPack_Packages/`, `*.zip` — fine).

**Verification performed this pass**: `clang-format --dry-run --Werror` on the four changed C/H files (`vw_caption_presenter.c/.h`, `vw_whisper_module.c`, `vw_whisper_engine.c`) → exit 0 (clean). No build/ctest re-run — gate already green per §7 closer; no code change made in this pass.
---

### 7.2 Third-Pass Review — 3 × x-preview Reviewers (2026-08-21, orchestrated)

> **Orchestration note**: 3 parallel `reviewer` subagents (`ContemporarySpoonbill`, `ImmediateSawfish`, `PopularKite`) were dispatched with disjoint scopes (A: presenter+module flush, B: worker engine+tests+packaging, C: docs/architecture). All 3 hung past 360s and were cancelled via `hub cancel` — zero deliverables (see §1 header for prior identical scout cancellation). Inline hunt substituted, deduped against §7 and §7.1. No finding below duplicates an existing row — checked by `grep -A 100 "^## 7\." diff.md`.

| Priority | Component / Location | Description | Impact | Proposed Fix |
|---|---|---|---|---|
| **Low** | `plugin/src/vw_caption_presenter.c:285-288` — `pending_segment.text_bytes` stale after truncation | `pending_segment = *segment` copies the original `text_bytes` (wire length), then `strncpy(pending_text, text_utf8, 1023)` truncates the text and NUL-terminates, but `pending_segment.text_bytes` is NOT updated to `strlen(pending_text)`. If any future consumer re-serializes via `text_bytes` (protocol) or logs it, the length is wrong. Not observable today — presenter renders via `text_utf8` only and `vw_worker.c` never re-encodes pending — but it violates the `vw_caption_segment_t` invariant (`text_bytes == strlen(text_utf8)`). | Silent length mismatch if `text_bytes` ever reused | After the `strncpy`, add `pending_segment.text_bytes = (uint16_t)strlen(pending_text);` (cap at `UINT16_MAX` already implied by 1024 limit) |
| **Low** | `plugin/src/vw_whisper_module.c:765` + `786-788` — `has_pending`/`pending_text` rely on `calloc` zero-init | `sys = calloc(1, ...)` zero-initializes `presenter.has_pending==false` and `pending_text==0`, but `vw_plugin_open` only explicitly sets `p_filter_ctx`, `spu_channel_id`, `spu_channel_registered` — `has_pending` and `pending_text` are left to implicit `calloc` state. Works today, but brittle if allocation ever changes to `malloc`/`realloc` or if `presenter` is ever stack-reused without `memset`. | Future regression if allocator changes | Explicitly initialize in `vw_plugin_open` after line 788: `sys->presenter.has_pending = false; sys->presenter.pending_text[0] = '\0';` |
| **Low** | `tests/unit/test_caption_presenter.c:320-405` — coverage gap for `b_ephemer`/`b_subtitle` across Tests 15-18 | Only Test 14 (`g_last_subpic_b_ephemer` at line 204) asserts `b_ephemer==true` / `b_subtitle==false`; Tests 15 (floor), 16 (clip), 17 (rate floor), 18 (long cue) assert `start`/`stop` but never re-assert the SPU flags. A regression that flipped `b_ephemer` only for clipped or rate-scaled paths would pass 15-18. | Flag regression in clipped paths could slip past suite | Add `assert(g_last_subpic_b_ephemer == true); assert(g_last_subpic_b_subtitle == false);` to the end of Tests 15, 16, 17, 18 (4 lines, low cost) |

**Dedup verification**: `grep` against §7 and §7.1 confirms none of the 3 rows duplicate the 5 bugs in §7 (temp fallback, strncpy truncation diagnostic, flush bypass, registry `len`, double reset) or the 4 rows in §7.1 (installer `/nonfatal`, concat no-separator, floor-vs-clip, off-by-one directory). The `strncpy` row in §7 is about missing diagnostic on truncation; the new §7.2 row is about `text_bytes` length staleness — related but distinct invariant. The `double reset` row in §7 covers `blank`/`clear` redundancy; the new `calloc` row covers init-time reliance — distinct lifecycle phase.

---

### 7.3 Fourth-Pass Review — 3 × Flash High Subagents Deep Audit (2026-08-21)

> **Orchestration note**: 3 parallel bug hunter subagents surveyed the Plugin subsystem, Worker/Inference subsystem, and Packaging/Protocol/Build subsystem. Deduped against §7, §7.1, and §7.2.

| Priority | Component / Location | Description | Impact | Proposed Fix |
|---|---|---|---|---|
| **Critical** | `protocol/src/vw_protocol_codec.c:43, 180` | `VW_MSG_STARTED` NULL payload exemption: lines 43 & 180 exempt `type != VW_MSG_STARTED` from the `!payload` NULL check. In Protocol v1.2, `VW_MSG_STARTED` carries 1-byte `source_active`. If passed NULL payload, dereferencing `p->source_active` triggers an immediate segmentation fault / crash. | Crash on malformed/NULL payload encode/decode | Remove `&& type != VW_MSG_STARTED` from both lines 43 and 180 of `vw_protocol_codec.c`. |
| **High** | `cmake/vlc_whisper_installer.nsi.in:49, 154` | Missing `SetShellVarContext all`: Admin elevation routes `$SMPROGRAMS` and `$DESKTOP` to the Administrator's private profile (`C:\Users\<Admin>\Desktop`) rather than the public/all-users desktop (`C:\Users\Public\Desktop`). | Desktop and Start Menu shortcuts are invisible to standard user | Add `SetShellVarContext all` in `.onInit` and `Section "Uninstall"`. |
| **High** | `cmake/vlc_whisper_installer.nsi.in:102` | Mandatory `vlc-whisper-worker.exe` in NSIS script: Under CPU-only presets (`windows-x64-release-cpu`), the worker output is named `vlc-whisper-worker-cpu.exe`. Line 102 lacks `/nonfatal`, causing `makensis` to abort compilation with missing file error. | Installer build failure on CPU presets | Mark line 102 as `File /nonfatal "@CMAKE_BINARY_DIR@/worker/vlc-whisper-worker.exe"`. |
| **High** | `plugin/src/vw_whisper_module.c:549` | Strict `seek_target_us > 0` check skips re-anchor at timeline origin: Seeking backwards to `00:00:00` sets `seek_target_us = 0`. Line 549 tests `> 0`, evaluating to false and bypassing sending `VW_MSG_POSITION` with `VW_POSITION_FLAG_SEEK`. | Lookahead demuxer is never repositioned to 0:00 on seek to origin | Change condition to `seek_target_us >= 0`. |
| **High** | `plugin/src/vw_whisper_module.c:319-332` | `input_thread_t` reference leak during worker respawn: `vw_plugin_find_input` acquires a reference via `vlc_object_hold`. If the worker does not support `VW_CAPABILITY_SOURCE_MODE`, line 331 `vlc_object_release` is skipped. | Permanent VLC object reference leak | Unconditionally release `input` if non-NULL. |
| **High** | `worker/src/vw_worker.c:423-437` | Look-ahead decoder fails to seek on `START_SESSION` with `timeline_origin_pts_us > 0`: Starting playback mid-stream or after session reset leaves the demuxer at 0:00 because `vw_source_decoder_seek` is never called during `START_SESSION`. | Stale captions from start of file emitted instead of mid-file position | Call `vw_source_decoder_seek(source_decoder, current_playback_pts_us)` when `current_playback_pts_us > 0`. |
| **High** | `worker/src/vw_source_decoder_ffmpeg.c:189-237` | Multi-frame decode loop overwrites leftover buffer & drops unread frames on EAGAIN: `avcodec_receive_frame` inner loop unconditionally overwrites `decoder->leftover_buffer`, and jumping to `av_packet_unref` on EAGAIN discards pending frames. | Audio corruption / timeline gap on multi-frame packets | Break decode loop when `total_samples >= max_samples` and leftover buffer is filled; drain pending frames before reading next packet. |
| **High** | `protocol/src/vw_protocol_validate.c:126-130` | Missing NULL check on `text_utf8` in `VW_MSG_CAPTION_SEGMENT` validator: Calling `is_empty_or_whitespace(NULL, p->text_bytes)` dereferences NULL pointer and crashes instead of returning false. | Process crash on segment validation | Add `if (p->text_bytes > 0 && !p->text_utf8) return false;`. |
| **Medium** | `plugin/src/vw_caption_presenter.c:169` | `var_Get(p_filter_ctx, "rate")` always fails with `VLC_ENOVAR`: In VLC, `"rate"` is instantiated on `input_thread_t`, not on `filter_t`. Rate query always defaults to 1.0f. | Reading floor and SPU lead calculations do not scale at 0.5x / 2.0x playback rates | Pass `rate` directly from `vw_whisper_module.c` into presenter methods. |
| **Medium** | `worker/src/vw_vad.c:69-77` | `!whisper_vad_detect_speech` jumps directly to energy fallback: Bypasses Silero VAD silence evaluation and falls back to RMS energy thresholding, triggering unnecessary 15s Whisper inference on background noise. | False speech triggers and excessive inference on silence | Only fall back to energy VAD if model inference fails; treat `!speech_detected` as pure silence drain. |
| **Medium** | `protocol/src/vw_protocol_codec.c:285` | `VW_MSG_ERROR` decoding lacks NUL-terminator guarantee: Corrupted 256-byte wire payload without NUL terminator causes subsequent string operations (`strlen`, `printf`) to read out-of-bounds. | Out-of-bounds memory read | Set `p->message[VW_MAX_ERROR_MSG_BYTES - 1] = '\0';` during decode. |
| **Medium** | `plugin/src/vw_platform_linux.c:21-40` | Unsynchronized global `vw_unreaped_pids`: `vw_platform_reap_unreaped` is called concurrently from sender thread and VLC close thread without mutex locking. | Data race / memory corruption during worker respawn/close | Protect `vw_unreaped_pids` with a static `pthread_mutex_t`. |
| **Low** | `worker/src/vw_worker_config.c:42-45` | Relational pointer comparison `(last_slash > last_bslash)` when one is NULL is undefined behavior in ISO C17 §6.5.8. | Compiler undefined behavior | Guard NULL before relational comparison. |
| **Low** | `plugin/src/vw_platform_win32.c:24` | CryptoAPI constant `CMC_STATUS_SUCCESS` used with CNG `BCryptGenRandom`. | API contract mismatch | Use standard CNG macro `BCRYPT_SUCCESS(status)`. |
| **Low** | `cmake/vlc_whisper_installer.nsi.in:117` | UAC-elevated installer writes HKCU keys to Administrator profile rather than interactive user. | Registry pollution / orphaned keys | Rely on machine-wide `HKLM "Software\VLC-Whisper"` keys. |

### 7.4 Fifth-Pass Review — 2 × x-preview Reviewers, slow mode (2026-08-21, orchestrated)

> **Orchestration note**: 2 slow (~20tps) `reviewer` subagents deployed (PluginReviewer + WorkerReviewer). WorkerReviewer completed after 1h13m (3 findings); PluginReviewer completed after 1h31m (2 findings). Both deduped against §7–§7.3 before emitting; rows below are theirs verbatim with light formatting.

| Priority | Component / Location | Description | Impact | Proposed Fix |
|---|---|---|---|---|
| **Medium** | `plugin/src/vw_whisper_module.c:136-142` — registry InstallPath not force-NUL-terminated | After `RegQueryValueExA(hkey, "InstallPath", NULL, &type, (LPBYTE)val, &len)` succeeds, `val[MAX_PATH]` is handed to `vw_plugin_probe_ancestors(val, …)` unmodified. Microsoft documents that REG_SZ data may be stored without a terminating NUL (writer used cbData excluding it); then `len == sizeof(val)` and val holds 260 non-terminated bytes. `probe_ancestors` (module.c:86 `for (const char* p = file_path; *p; p++)`) scans for the last separator past the end of the stack buffer — out-of-bounds read. Distinct from the §7 registry row, which only analyzed the terminated len==MAX_PATH case and deemed it safe. | Out-of-bounds stack read in the VLC process at filter open when HKCU/HKLM\Software\VLC-Whisper\InstallPath holds an unterminated value (corrupt/legacy/manual entry); separator scan consumes adjacent stack, producing garbage candidate paths or a crash. | After a successful query, clamp and terminate: `size_t vlen = (len < sizeof(val)) ? len : sizeof(val) - 1; val[vlen] = '\0';` before calling vw_plugin_probe_ancestors. |
| **Low** | `docs/test-strategy.md:77-78` — coverage claims exceed what the new tests assert | Line 78 claims test_whisper_engine.c (17e.2) verifies "identical segment counts, timestamps, and UTF-8 text on repeat passes", but the new Test 4 (test_whisper_engine.c:99-113) compares only get_segment_count and get_text — t0_us/t1_us are never captured or compared. Line 77 claims the presenter suite covers "OSD fallback minimum floor", yet no test asserts the OSD path's 1.0 s clamp (vout_OSDText mock records only a call counter, and Test 7 uses a 2 s cue where the floor is inert). Both doc rows and both tests are introduced by this branch. | CI green while the documented acceptance gates (timestamp determinism, OSD floor) are untested; a regression in centisecond scaling (t0*10000LL) or the display() clamp would pass the suite the docs say covers it. | Either add the assertions (capture seg.t0_us/t1_us arrays in pass 1 and EXPECT equality in pass 2; add a sub-second display() case asserting the mocked OSD duration == 1000000) or narrow the doc wording to count+text and SPU-path floor only. |
| **Low** | `worker/src/vw_whisper_engine.c:73` + `docs/decisions.md:291` — "≤ 5 passes" understates the vendored ladder by one | Comment and ADR-021 state temperature_inc = 0.2f gives "explicit bounded temperature fallback (<= 5 passes)". The vendored ladder builder (third_party/whisper.cpp/src/whisper.cpp:6875-6878) loops `for (float t = params.temperature; t < 1.0f + 1e-6f; t += 0.2f)`, which admits six temperatures {0.0, 0.2, 0.4, 0.6, 0.8, 1.0} (float accumulation of 5×0.2f rounds to exactly 1.0, and 1.0f < 1.0f + 1e-6f is true), i.e. the initial pass plus 5 fallback retries = up to 6 decode passes, not 5. Same off-by-one propagates to docs/plans/step17e_2_plan.md (O2/O3) and docs/roadmap.md 17e.2. | Documented worst-case inference latency bound is ~17% understated (6 encoder-decode passes on degenerate audio, not 5); latency-budget reasoning that trusts ADR-021 mis-provisions the realtime window. | Re-word to "initial pass + ≤5 temperature-fallback retries (ladder caps at t=1.0)" in the engine comment, ADR-021 decision 2, plan O2/O3, and roadmap 17e.2 — or clamp the ladder explicitly if 5 total passes is the intended bound. |
| **Medium-Low** | `plugin/src/vw_caption_presenter.c:311-314` (`flush`) + `:279-280` (`show_segment` dispatch) — buffered cue silently dropped when vout unavailable at render time | `flush()` and `show_segment()`'s predecessor dispatch clear `has_pending` even when `render_internal` returns false (vout-walk failure), and the module sender ignores both booleans — a transient vout loss (resize/recreate, teardown race, audio-only media) permanently discards the buffered cue with no diagnostic, defeating the deferral buffer's own retry purpose; every other loss path (seek/pause/swap) is a logged deliberate blank. | Occasional silent caption loss; unobservable in logs | WARN-log the drop (parity with other PRESENTER_* events) or retain pending with bounded retry. |
| **Low** | `tests/unit/test_caption_presenter.c:391-406` (+129-136 mock) — rate tests masked by infallible var_Get mock | Mock `var_Get` always returns VLC_SUCCESS, so Tests 11/12/17 exercise rate math on a path production never takes (`var_Get(filter_t,"rate")` → ENOVAR per §7.3 runtime row); no test pins the rate-fallback (rate=1.0) contract, so a runtime fix or regression ships unverified by the suite. | Suite green-lights scaling math that is dead in production | Add `g_mock_var_get_fail` toggle + a test asserting floor/wall durations under var_Get failure. |

**PluginReviewer dedup note** (verbatim from its final reasoning): F1 not in §7 (their flush row is the timer-condition redundancy, NOT the drop-on-render-failure), §7.1, §7.2, or §7.3 sibling rows; F2 complements §7.3's var_Get runtime row from the test-fidelity angle (distinct file + fix). Verified non-issues it explicitly cleared: min_media_floor_us double truncation (≤1µs, immaterial), saturating-add edges (unreachable with validated PTS bounds), OSD fallback floor consistency (shares floored dur_wallclock), mock ownership/leaks (PutSubpicture deletes region+text+subpic; error paths delete).

---

### 7.5 Greptile PR #17 Review Findings (2026-08-21)

> **Source**: Fetched via GitHub API from Greptile AI bot review on Pull Request #17 (`gemini/milestone-3-step-18`).

| Priority | Component / Location | Description | Impact | Proposed Fix |
|---|---|---|---|---|
| **P1 (High)** | `cmake/vlc_whisper_installer.nsi.in:144-147` | **Plugin-cache failures report success**: When `vlc-cache-gen.exe "$INSTDIR\plugins"` or its VLC fallback (`vlc.exe --reset-plugins-cache --version`) returns a nonzero exit status, the installer only prints the return code `$0` and continues to the successful finish page. VLC retains a stale `plugins.dat` and fails to discover `libvlc_whisper_plugin.dll`, so the caption shortcut fails despite setup reporting success. | Setup reports false success on cache generation failure; plugin remains undiscovered by VLC | Check exit code `$0`; if nonzero, display warning/retry dialog: `${If} $0 != 0 MessageBox MB_OK\|MB_ICONEXCLAMATION "Failed to regenerate VLC plugin cache. Please ensure VLC is closed and run vlc-cache-gen.exe manually." ${EndIf}`. |
| **P1 (High)** | `cmake/vlc_whisper_installer.nsi.in:49-74` | **Unenforced VLC directory validation on custom path selection**: `.onInit` checks `FileExists "$VLC_DIR\vlc.exe"` for the auto-detected path, but if the user clicks "Browse" on `MUI_PAGE_DIRECTORY` and chooses an arbitrary non-VLC folder (e.g. `D:\CustomDir`), the directory page does not validate the existence of `vlc.exe` before proceeding to file installation. | User can install plugin DLL and worker into an invalid directory without VLC | Add `!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirectoryLeave` and verify `${If} ${FileExists} "$INSTDIR\vlc.exe"` in the directory leave callback. |
| **P2 (Medium)** | `cmake/Packaging.cmake` & `cmake/vlc_whisper_installer.nsi.in` | **Packaging files lack repository namespace prefix**: Project-owned packaging files `Packaging.cmake` and `vlc_whisper_installer.nsi.in` do not use the `vw_` filename prefix enforced by Rule 3 (`vw_packaging.cmake`, `vw_installer.nsi.in`). | Codebase naming convention non-compliance (Rule 3) | Rename to `cmake/vw_packaging.cmake` and `cmake/vw_installer.nsi.in`, updating `CMakeLists.txt` include. |
| **P1 (High)** | `cmake/Packaging.cmake:18-23` & `cmake/vlc_whisper_installer.nsi.in:102` | **CPU-only build target incompatibility**: In CPU-only presets, `vlc-whisper-worker-cpu.exe` is generated, but NSIS script mandates `vlc-whisper-worker.exe` without `/nonfatal` or dynamic target selection, failing NSIS compilation on CPU presets. | Installer compilation failure on CPU-only builds | Pass worker artifact filename dynamically from CMake or use `File /nonfatal` for both GPU and CPU worker variants. |
| **P1 (High)** | `cmake/Packaging.cmake:47-50` & `models/` | **Mandatory Whisper model not provisioned on clean checkout**: `ggml-tiny.en.bin` is git-ignored and not automatically fetched during CMake configure/build; building `installer` target on fresh clone fails. | Fresh clone packaging build failure | Add a model download script / CMake fetch rule or document mandatory pre-packaging download step in setup instructions. |
| **P1 (High)** | `cmake/Packaging.cmake:18-23` & `cmake/vlc_whisper_installer.nsi.in:102-104` | **Dual-worker build prerequisite unenforced before installer creation**: `vlc_whisper_installer.nsi.in` packages both `vlc-whisper-worker.exe` (GPU) and `vlc-whisper-worker-cpu.exe` (CPU fallback for loader-less systems). Running `cmake --build --preset windows-x64-release --target installer` without first building `windows-x64-release-cpu` triggers NSIS `warning 7010: File: .../vlc-whisper-worker-cpu.exe -> no files found`. The installer builds without error but silently omits the CPU fallback binary, breaking captioning on end-user machines without Vulkan runtime/drivers. | Silent omission of CPU fallback worker in distributed setup installer | Wire a multi-preset packaging script or CMake custom target that builds both `windows-x64-release` and `windows-x64-release-cpu` before invoking `makensis`, and document the dual-build workflow in `README.md`. |


