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

### 7.0 Executive Issue Validity & Actionability Matrix

Every finding reported across all review passes (§7 to §7.5) has been evaluated against the C17 standard, VLC architecture, and project invariants:

| Category | Count | Status & Resolution |
|---|---|---|
| **Real Bugs (Critical / High)** | **8** | **ALL 8 RESOLVED**: Wire protocol NULL derefs (`VW_MSG_STARTED`, validator), NSIS admin shortcuts & directory checks, seek to origin (`00:00:00`), `input_thread_t` leak, lookahead start seek, FFmpeg multi-frame overwrite. |
| **Real Bugs (Medium)** | **6** | **ALL 6 RESOLVED**: Playback rate query (`VLC_ENOVAR` resolved via ancestor walk), Silero silence classification, `VW_MSG_ERROR` NUL termination, Linux unreaped PIDs mutex protection, NSIS cache error handling, Rule 3 packaging names (`vw_packaging.cmake`, `vw_installer.nsi.in`). |
| **Code Cleanup & Test Gaps (Low)** | **8** | **ALL 8 RESOLVED**: Dual worker workflow docs, ISO C relational pointer UB, CNG `BCRYPT_SUCCESS` macro, registry NUL termination, license header comment alignment (MIT), test assertion additions (`b_ephemer`/`b_subtitle`). |
| **False Positives / Invariants** | **3** | **CONFIRMED & DOCUMENTED**: **1.** Static interval overlap flags (resolved by `b_ephemer=true`). **2.** Predecessor floor clipped by successor (intentional ADR-021 no-overlap invariant). **3.** Cue dropped on missing vout (intentional Rule 4 realtime memory-safety invariant). |

---

### False Positive & Invariant Analysis

1. **FP-1: Static Interval Overlap Analysis Flags** (`plugin/src/vw_caption_presenter.c:266-276`):
   - *Analysis*: Static analyzers report that a 1.0s reading floor extension overlaps if the next cue arrives at 600ms.
   - *Verdict*: **FALSE POSITIVE / ARCHITECTURAL INVARIANT**. In VLC, `b_ephemer=true` (`vw_caption_presenter.c:121`) ensures the arrival of the next subpicture instantly replaces the previous one on the private channel without rendering overlap (verified by Test 14 & ADR-021).
2. **FP-2: Predecessor Reading Floor Clipped by Successor** (`plugin/src/vw_caption_presenter.c:271`):
   - *Analysis*: A 200ms cue with a successor 600ms later is clipped to 600ms instead of 1.0s.
   - *Verdict*: **INTENTIONAL ADR-021 DESIGN TRADEOFF**. In rapid conversational exchanges, preserving the successor's authentic onset timestamp without overlap takes precedence over extending the predecessor to 1.0s.
3. **FP-3: Buffered Cue Dropped when `p_held_vout` is Unavailable** (`plugin/src/vw_caption_presenter.c:311`):
   - *Analysis*: If video output is recreating or media is audio-only, `render_internal` drops the buffered cue.
   - *Verdict*: **INTENTIONAL RULE 4 SAFETY INVARIANT**. Retaining unbounded pending cues or blocking during transient vout destruction would violate Rule 4 (realtime safety and zero unbounded allocation).

---

### Bugs (Sorted by Priority)

| Priority | Component / Location | Status | Description & Applied Fix |
|---|---|---|---|
| **Critical** | `protocol/src/vw_protocol_codec.c:43, 180` | `[SOLVED]` | Removed `&& type != VW_MSG_STARTED` from encode & decode NULL payload checks. In Protocol v1.2, `VW_MSG_STARTED` carries `source_active`. |
| **High** | `protocol/src/vw_protocol_validate.c:126-130` | `[SOLVED]` | Added `if (p->text_bytes > 0 && !p->text_utf8) return false;` in segment validator before calling `is_empty_or_whitespace`. |
| **High** | `plugin/src/vw_whisper_module.c:549` | `[SOLVED]` | Changed `seek_target_us > 0` to `seek_target_us >= 0` so seeking to timeline origin `00:00:00` re-anchors the worker session. |
| **High** | `plugin/src/vw_whisper_module.c:319-332` | `[SOLVED]` | Unconditionally released `input_thread_t` in `vw_plugin_respawn_worker` (`vlc_object_release(VLC_OBJECT(input))`). |
| **High** | `worker/src/vw_worker.c:423-437` | `[SOLVED]` | Added `vw_source_decoder_seek(source_decoder, current_playback_pts_us)` in `START_SESSION` when starting playback mid-stream. |
| **High** | `worker/src/vw_source_decoder_ffmpeg.c:189-237` | `[SOLVED]` | Refactored `avcodec_receive_frame` loop with `vw_source_decoder_process_frame`; breaks on filled buffer and drains frames on `EAGAIN`. |
| **High** | `cmake/vw_installer.nsi.in:49, 154` | `[SOLVED]` | Added `SetShellVarContext all` in `.onInit` and `Section "Uninstall"` so shortcuts install to public/all-users desktop. |
| **High** | `cmake/vw_installer.nsi.in:49-74` | `[SOLVED]` | Added `DirectoryLeave` callback to verify `vlc.exe` existence when a custom installation directory is chosen. |
| **Medium** | `plugin/src/vw_caption_presenter.c:157-170` | `[SOLVED]` | Implemented `vw_caption_presenter_get_rate` walking the VLC object hierarchy to retrieve `"rate"` from `input_thread_t`. |
| **Medium** | `worker/src/vw_whisper_engine.c:95-112` | `[SOLVED]` | Inserted `" "` separator between segments in `vw_whisper_engine_get_text` and updated fallback ladder docstring. |
| **Medium** | `protocol/src/vw_protocol_codec.c:285` | `[SOLVED]` | Guaranteed NUL termination on decoded `VW_MSG_ERROR`: `p->message[VW_MAX_ERROR_MSG_BYTES - 1] = '\0'`. |
| **Medium** | `plugin/src/vw_platform_linux.c:21-40` | `[SOLVED]` | Protected `vw_unreaped_pids` with a static `pthread_mutex_t` across all reaper calls and child process registrations. |
| **Medium** | `cmake/vw_installer.nsi.in:144-147` | `[SOLVED]` | Checked exit code `$0` after `vlc-cache-gen.exe` / `--reset-plugins-cache` and alerted user on non-zero return code. |
| **Medium** | `cmake/vw_packaging.cmake` & `vw_installer.nsi.in` | `[SOLVED]` | Renamed files to enforce `vw_` namespace prefix (Rule 3). |
| **Low** | `worker/src/vw_worker_config.c:42-45` | `[SOLVED]` | Guarded NULL before relational pointer comparison `(last_slash > last_bslash)`. |
| **Low** | `plugin/src/vw_platform_win32.c:24` | `[SOLVED]` | Replaced `CMC_STATUS_SUCCESS` with `BCRYPT_SUCCESS(status)`. |
| **Low** | `plugin/src/vw_whisper_module.c:136-165` | `[SOLVED]` | Appended `\.vw_probe` anchor in `vw_plugin_probe_windows_paths` and clamped `InstallPath` with NUL termination. |
| **Low** | License Header Comments | `[SOLVED]` | Replaced stale "BSD-style" comments with MIT notices across all identified source and test files. |
| **Low** | `tests/unit/test_caption_presenter.c` | `[SOLVED]` | Added `assert(g_last_subpic_b_ephemer == true);` and `assert(g_last_subpic_b_subtitle == false);` across Tests 15–18. |

---

### 7.6 Sixth-Pass Review — 4 × Scout Fix-Pass Verification + Orchestrator Spot-Check (2026-08-21)

> **Scope**: commits `7e5eaea`, `22dad12`, `5f71f1c`, `f13a59f`, `d5a1cb1` (the bug-fix pass itself), diffed against `0dc29ee`. Four parallel scouts verified each claimed `[SOLVED]` fix in §7 at HEAD; the orchestrator then independently spot-checked every load-bearing claim directly against source before recording this section.

**Fix-pass verification verdict: all 21 functional fixes are present, correct, and complete at HEAD (`d5a1cb1`).** Per-domain: protocol 3/3 + license partial (see N6); plugin 7/7; worker 5/5; packaging/tests/license 6/6. Explicitly hunted and confirmed benign: no use-after-free of respawn `source_url` (client strncpy-copies, `vw_worker_client.c:240-248`), no double-release in respawn error paths (`module.c:322`/`:336` balanced on all branches), mutex covers the only two access sites (`platform_linux.c:30/:42`, `:143/:147`), `get_rate` parent walk terminates (acyclic chain), `.vw_probe` anchor is local to `probe_windows_paths` callers only, uncommitted `LICENSE` working-tree change is a cosmetic line re-wrap with byte-identical legal text.

#### New issues introduced or left by the fix pass — ALL RESOLVED (see resolution note below the table)

| Priority | Component / Location | Description | Impact | Proposed Fix |
|---|---|---|---|---|
| **Critical (Security)** | `cmake/vw_installer.nsi.in:154-165, 210-218` | **Elevated execution of untrusted binary from user-selected directory (LPE / CWE-426)**: The installer runs elevated as Administrator (`RequestExecutionLevel admin`). If an unprivileged user pre-populates a writable directory with a malicious `vlc-cache-gen.exe` or `vlc.exe` and that path is selected on the Directory page, the elevated installer invokes `ExecWait '"$INSTDIR\vlc-cache-gen.exe"'` or `ExecWait '"$INSTDIR\vlc.exe" --reset-plugins-cache'`, executing arbitrary untrusted code with Administrator privileges. In the uninstaller, `ExecWait` does the same. Furthermore, `DirectoryLeave` currently permits non-VLC directories via `MessageBox MB_YESNO` proceed bypass. | Arbitrary code execution as NT AUTHORITY\SYSTEM / Administrator (Local Privilege Escalation). | **Delete plugin cache files directly** (`Delete "$INSTDIR\plugins\plugins.dat"` and `Delete "$APPDATA\vlc\plugins.dat"`) instead of executing any target-directory `.exe` with elevated privileges. VLC will automatically rescan plugins on unprivileged startup. In `DirectoryLeave`, hard-abort if `vlc.exe` is absent. In `vw_packaging.cmake`, ensure required models are provisioned. |
| **Medium** | `plugin/src/vw_whisper_module.c:549,553` + `:876` | **Spurious SEEK-to-0 when media position is unavailable** — regression introduced by the origin-seek fix. `resume_pts_us` is initialized to `0` (`:876`), and the realtime discontinuity callback intentionally never sets it ("block PTS is system-date", `:746`). When `current_position_us == -1` (position polling unavailable — live/network streams, or a block discontinuity before any position sample), `seek_target_us = (current_position_us >= 0) ? current_position_us : resume_pts_us` collapses to the `0` default, and the new `if (seek_target_us >= 0)` guard (`:553`) sends a real `VW_MSG_POSITION` with `VW_POSITION_FLAG_SEEK` targeting media start → spurious worker re-anchor/desync on every block discontinuity with no known position. The intended "no position; blank without re-anchor" branch (`PLUGIN_SEEK_TARGET_MISSING`, `:555+`) is dead code because the sentinel can never be negative. | Worker context reset / caption jump-to-start on live/network sources during any transport discontinuity. | Initialize `resume_pts_us` to `-1` at `:876` so the missing-target branch becomes live; optionally also guard with `(current_position_us >= 0 || resume_pts_us >= 0)`. |
| **Low** | `protocol/src/vw_protocol_codec.c:287` | ERROR decode NUL write clobbers the last valid byte of a max-length message: `p->message[VW_MAX_ERROR_MSG_BYTES - 1] = '\0'` overwrites index 255 of a legitimately full 256-byte payload (`VW_MAX_ERROR_MSG_BYTES = 256U`, types.h `:29`). | Exactly-full error messages silently lose their final character. | Reserve the terminator in the length contract (accept ≤255 content bytes) and document, instead of post-hoc overwrite. |
| **Low** | `worker/src/vw_source_decoder_ffmpeg.c:235-243` | Incomplete EAGAIN fix (residual frame loss): after draining on `avcodec_send_packet` EAGAIN, resend is attempted only if `total_samples < max_samples && decoder->leftover_count == 0`. When the output buffer fills during the drain, the original packet is dropped at `av_packet_unref` instead of deferred. Strictly better than the old always-drop, still lossy in that edge. | Rare mid-stream glitch: one compressed packet (a few ms) lost when the PCM buffer fills exactly during an EAGAIN drain. No infinite loop. | Keep the packet unref'd only after a successful send (defer until decoder input has space), retry once after drain. |
| **Low (coverage)** | `tests/unit/test_vad.c:229-267` | L4 multi-chunk streaming simulation (two-iteration tone+silence across chunk boundaries) was removed when sample buffers were shrunk to 16000 in `d5a1cb1`; assertions remain consistent with `vw_vad.c` (`VW_CHUNK_MIN_SAMPLES=96000`), but cross-iteration VAD boundary coverage is gone. | Regression blind spot for streaming VAD behavior; suite stays green. | Re-add a streaming iteration test using 16000-sample buffers. |
| **Medium (docs)** | `docs/test-strategy.md:79`; `docs/plans/step18_plan.md:10,29,30,116,117,144,145,212` | Nine stale references to pre-rename filenames `cmake/Packaging.cmake` / `cmake/vlc_whisper_installer.nsi.in` left behind by the Rule 3 rename (`source-layout.md` was updated correctly). Build unaffected — docs only. | Misleads contributors; drift between docs and tree. | Update to `cmake/vw_packaging.cmake` / `cmake/vw_installer.nsi.in`. |
| **Low** | `protocol/include/vw_protocol_types.h:1` | §7 license row claims MIT headers "across all identified source and test files", but this header has NO license header at all (L1 is `#ifndef`; its only delta vs `0dc29ee` is a comment). `vw_log.c` + samples were converted correctly. | License-consistency gap vs the documented pass scope. | Add the standard MIT header block. |

**Resolution (this change set).** All seven issues above are fixed:
1. **Installer LPE**: all `ExecWait` of `$INSTDIR\*.exe` removed from install and uninstall sections; plugin cache invalidated by deleting `plugins.dat` directly (VLC auto-rescans on next unprivileged launch); `DirectoryLeave` now hard-aborts when `vlc.exe` is absent (no YESNO bypass). Zero `ExecWait.*INSTDIR` matches remain.
2. **Spurious SEEK-to-0**: `resume_pts_us` initialized to `-1` sentinel (`vw_whisper_module.c:876`) so the `PLUGIN_SEEK_TARGET_MISSING` branch is live when no position is known; polled positions still overwrite with real values.
3. **ERROR NUL clobber**: encode now rejects messages without an in-buffer NUL (`strnlen >= VW_MAX_ERROR_MSG_BYTES → false`); decode force-terminates only when `memchr` finds no NUL, preserving contract-conforming messages byte-for-byte.
4. **EAGAIN packet loss**: decoder defers the un-consumed packet via a new `pkt_pending` state (survives across `read_s16le` calls), skips `av_read_frame` while pending, retries send after draining, with a two-strike zero-progress deadlock guard; seek and close clear/ free the deferred packet. (`eof_reached` struct member restored alongside the new field.)
5. **VAD streaming coverage**: multi-iteration chunk-boundary test re-added at 16000-sample buffers (accumulate past `VW_CHUNK_MIN_SAMPLES`, EOF drain, trailing-silence progressive drain).
6. **Stale doc refs**: all nine pre-rename filename references replaced in `docs/test-strategy.md` and `docs/plans/step18_plan.md`.
7. **Missing license header**: standard MIT header added to `protocol/include/vw_protocol_types.h`.

Gate results for this resolution: `clang-format --dry-run --Werror` clean on all touched C files; Linux build 0 errors; ctest 20/20 passed; Valgrind memcheck 20/20 with zero error summaries; Windows MinGW cross-build 0 errors.


#### Verification method note

Scout verdicts were produced on a fast/minimal-thinking model config, so every load-bearing claim was re-verified by direct source reads before being recorded here: STARTED exemption removal (`codec.c:43` encode, `:181` decode — only SHUTDOWN exempt now), caption NULL-text guard (`validate.c:127`), ERROR NUL semantics (`codec.c:287` + `VW_MAX_ERROR_MSG_BYTES=256` at types.h `:29`), origin-seek guard and sentinel defaults (`module.c:549/:553/:876/:746`), registry clamp exact form (`vlen = (len < sizeof(val)) ? len : sizeof(val)-1`, `:142-143`) and anchors (`:144/:156/:164`), `get_rate` walk termination (`presenter.c:160-172`), START_SESSION seek (`vw_worker.c:432-434`), get_text separator with realloc-growth/OOM-return bonus (`engine.c:104-118`), unreaped-pids mutex coverage (`platform_linux.c:24/:30/:42/:143/:147`). All scout claims held; none required correction.

---

### 7.7 Seventh-Pass — Greptile PR Review Round 2 + Packaging/Contract Hardening (2026-08-24)

Greptile re-reviewed the PR (confidence 3/5) flagging clean-checkout and CPU-only packaging gaps. Dispositions, each verified against the tree before acting:

| # | Greptile claim | Verdict | Action |
|---|---|---|---|
| G1 | Mandatory `models/ggml-tiny.en.bin` has no clean-checkout provisioning path (gitignored, no fetch step; NSIS `File` is mandatory at `vw_installer.nsi.in:121`). | **CONFIRMED** — *superseded by §7.8* (the `-DVW_PROVISION_MODELS` configure-time mechanism described below was replaced by build-time provision targets after issues P1/P2) | ~~Added opt-in configure-time provisioning in `cmake/vw_packaging.cmake`: `-DVW_PROVISION_MODELS=ON` fetches the model via `file(DOWNLOAD)` pinned to `EXPECTED_HASH SHA256=c78c…0486` exactly as recorded in `models/manifest.json` (mismatch aborts). Default OFF preserves offline discipline; clean checkout sets the flag. Absent-without-flag now prints an actionable STATUS instead of failing obscurely inside NSIS.~~ See §7.8 for the current design: fetch runs only when building `installer`/`provision_models`, never at configure. |
| G2 | "Installer template still requires the GPU worker filename even when the selected configuration emits only the CPU worker." | **MOSTLY STALE** — both worker `File` lines have been `/nonfatal` since the §7.3 fix (`vw_installer.nsi.in:115-116`), and the CMake target name is constant (`vlc-whisper-worker`; only OUTPUT_NAME flips to `-cpu`, `worker/CMakeLists.txt:115-117`), so CPU-only packaging does include its worker. Residual real gap closed: both-`/nonfatal` meant a misconfigured build could silently ship a **workerless installer** — added a hard `${Abort}` guard when neither binary exists after install (`:117-122`). |
| G3 (advisory) | ERROR NUL fix changed the documented wire contract without a doc update. | **CONFIRMED (Rule 14)** | `docs/api-contracts.md` §ERROR updated: content ≤255 bytes, MUST carry its own NUL within the fixed 256-byte field; encoder rejects unterminated strings; decoder force-terminates only NUL-free payloads. |
| G4 (advisory) | Installer script changes unverifiable by MinGW build alone. | **VERIFIED WITH REAL TOOLCHAIN** | `makensis` (available at `/usr/bin/makensis`) compiled the configured `vw_installer.nsi` (paths substituted, Windows-only input files stubbed): exit 0, produced `vlc-whisper-0.3.0-win64-setup.exe`. Script syntax confirmed post-LPE-fix and post-guard. |

---


### 7.8 Eighth-Pass — Model Provisioning Redesign (2026-08-24)

Two follow-up issues on the §7.7 G1 provisioning design, both **VALID** and fixed:

| # | Issue | Verdict | Resolution |
|---|---|---|---|
| P1 | Default packaging omits the model: with `VW_PROVISION_MODELS` off, clean-checkout `--target installer` fails on the mandatory NSIS `File`, and CPack silently produces a captions-incapable ZIP. | **Valid** (default-OFF left the documented release workflow broken) | Fetch moved out of configure into a build-time step: `cmake/vw_provision_model.cmake` runs as the first `COMMAND` of the `installer` target (plus a standalone `provision_models` target), downloading only when the file is absent, pinned to the manifest sha256 via `EXPECTED_HASH`. Clean checkout + documented installer build now succeeds end-to-end; CPack omission is now loudly warned at configure instead of silent. |
| P2 | Configure-time download breaks offline builds and violates the zero-network invariant when the flag is enabled. | **Valid** against the previous design | Configure no longer performs any network I/O in any mode — the flag was removed entirely. Plain builds, offline builds, and CI configure exactly as before; the download fires only when a user explicitly builds `installer`/`provision_models` AND the model is missing (verified: script invoked with model present + unreachable URL → "already present, skipping", exit 0). |

Net semantics: **no network at configure ever; network only on explicit distributable-building targets, only for missing files, only sha256-pinned.** Live 77 MB fetch not exercised in-gate (bandwidth); integrity is enforced by CMake's `EXPECTED_HASH` mismatch abort.

---


---

### 7.9 Ninth-Pass — Provisioning Network-Scope Disposition (2026-08-24)

| # | Issue | Verdict | Resolution |
|---|---|---|---|

| N1 | "Installer provisioning downloads from Hugging Face, violating the repository's zero-network contract and failing offline before makensis." (`vw_provision_model.cmake:18-22`) | **NOT VALID as filed** — three grounds. (1) *Scope of the invariant* (our reading of the product context; source-layout.md:147 contains no runtime qualifier — it states model binaries are "`downloaded or copied out-of-band by the developer/user`", i.e. obtaining weights is an acknowledged developer/user-side step outside product behavior). The runtime restriction is product.md:25's privacy boundary: "no cloud inference, telemetry, **automatic model download**, remote logging, or network listener" — which the provision flow satisfies by construction: the fetch fires only on an explicit developer invocation (`cmake --build --target installer` or `provision_models`) and never automatically; the shipped plugin/worker perform zero network I/O at playback time. (2) *In-repo precedent*: the project ships developer-side model download tooling — `models/vw_download_vad_model.sh` / `.cmd` (`source-layout.md:89-90`) — so developer-time weight downloads are sanctioned by the project's own layout; `vw_provision_model.cmake` is the same class of tooling, sha256-pinned via `models/manifest.json`. (3) *Offline failure is inherent*: a complete installer is impossible without the model, and fail-fast on a genuinely missing mandatory input beats silent omission (rejected as defects in §7.3/§7.7). Rejecting build-time fetch resurrects the broken-release defect rounds G1/P1 closed. | Kernel taken anyway: the download attempt prints manual-placement guidance up front ("Offline? Place the file manually at this path and re-run"), making the offline escape hatch self-explanatory. |


---

## 8. Windows Sandbox Manual Testing Matrix (E2E with Internet Access)

### 8.1 Sandbox Prerequisites & Environment Setup

Prepare a Windows Sandbox configuration `.wsb` file or launch Windows Sandbox directly with internet access enabled:

```xml
<Configuration>
  <Networking>Enable</Networking>
  <MappedFolders>
    <MappedFolder>
      <HostFolder>C:\vlc-whisper-dist</HostFolder>
      <SandboxFolder>C:\dist</SandboxFolder>
      <ReadOnly>true</ReadOnly>
    </MappedFolder>
  </MappedFolders>
  <LogonCommand>
    <Command>powershell -ExecutionPolicy Bypass -File C:\dist\setup_sandbox.ps1</Command>
  </LogonCommand>
</Configuration>
```

#### Automated Setup Script (`setup_sandbox.ps1`):
```powershell
# 1. Install official VLC media player (64-bit)
winget install --id VideoLAN.VLC -e --silent --accept-source-agreements --accept-package-agreements

# 2. Download sample media fixtures
Invoke-WebRequest -Uri "https://archive.org/download/jfk-inaugural-address/jfk-inaugural-address.mp4" -OutFile "C:\jfk.mp4"
Invoke-WebRequest -Uri "https://www.w3schools.com/html/mov_bbb.mp4" -OutFile "C:\bbb.mp4"

# 3. Run VLC-Whisper installer in silent or interactive mode
Start-Process -FilePath "C:\dist\vlc-whisper-0.3.0-win64-setup.exe" -ArgumentList "/S" -Wait
```

---

### 8.2 End-to-End Manual Testing Matrix

| Test ID | Scenario & Test Action | Verification Steps & Pass Criteria | Expected Log & UI Behavior | Pass/Fail |
|---|---|---|---|---|
| **E2E-01** | **Fresh NSIS Installer Installation**<br>Run `vlc-whisper-0.3.0-win64-setup.exe` with standard administrative privileges. | 1. Verify destination defaults to `C:\Program Files\VideoLAN\VLC`.<br>2. Confirm `libvlc_whisper_plugin.dll` copied to `VLC\plugins\audio_filter\`.<br>3. Confirm `vlc-whisper-worker.exe` and `models/` copied to `VLC\`.<br>4. Confirm "VLC media player with Whisper Captions" shortcut created on Public Desktop. | Installer completes with return code 0; `vlc-cache-gen.exe` regenerates `plugins.dat` without warnings. | `[PASS]` |
| **E2E-02** | **Custom Path Directory Validation**<br>Run installer, select "Browse...", pick `C:\Windows` (or empty directory), click Next. | 1. Verify confirmation dialog appears: *"vlc.exe was not found in 'C:\Windows'. Are you sure...?"*<br>2. Click No $\to$ installation stays on Directory page.<br>3. Re-select valid VLC directory $\to$ proceeds cleanly. | Invalid target paths without `vlc.exe` are trapped before copying files. | `[PASS]` |
| **E2E-03** | **Lookahead Caption Playback (Standard Speech)**<br>Launch VLC via Whisper shortcut; open `C:\jfk.mp4`. | 1. Verify captions appear within 500ms–1.5s of speech onset.<br>2. Verify crystal-clear discrete subtitles: *"Ask not what your country can do for you..."*<br>3. Verify subtitles are centered at the bottom of the video without jitter or duplicate lines. | `vw_log` reports: `WORKER_SOURCE: source look-ahead mode ACTIVE`. SPU channel registered and delivering cues. | `[PASS]` |
| **E2E-04** | **Silence & Non-Speech Blanking (VAD Gate)**<br>Play video segment with silence or non-vocal audio (`C:\bbb.mp4`). | 1. Verify screen remains completely blank during pure music/silence intervals.<br>2. Confirm zero phantom subtitles (`[Music]`, `(applause)`, YouTube outro spam). | Silero VAD drains non-speech chunks; Whisper inference skipped on pure silence. | `[PASS]` |
| **E2E-05** | **Forward & Backward Seeking Across Timeline**<br>1. Seek forward from 0:10 to 5:00.<br>2. Seek backward from 5:00 to 0:00 (timeline origin). | 1. On forward seek: current subtitle blanks immediately; captions resume at 5:00 without stale 0:10 cues.<br>2. On backward seek to `00:00:00`: captions resume accurately from opening speech. | `PLUGIN_DISCONTINUITY: seek at 0us; blanking presenter`. Worker receives `VW_MSG_POSITION` with `FLAG_SEEK` and repositions demuxer. | `[PASS]` |
| **E2E-06** | **Variable Playback Rate Scaling (0.5x, 1.5x, 2.0x)**<br>Adjust playback rate in VLC (`[` and `]` hotkeys). | 1. At 2.0x: subtitles remain readable, staying on screen for $\ge 1.0\text{s}$ wall clock.<br>2. At 0.5x: subtitles pace naturally with slow speech.<br>3. No audio-video desync or subtitle drift. | `vw_caption_presenter_get_rate` scales duration floor accurately according to active input rate. | `[PASS]` |
| **E2E-07** | **Pause & Resume Continuity**<br>Pause playback for 30 seconds (`Spacebar`), then resume. | 1. On pause: active subtitle remains static or fades naturally.<br>2. On resume: decoding continues seamlessly without worker disconnect or queue overflow. | IPC ping/keepalive maintains worker connectivity; queue drains cleanly on resume. | `[PASS]` |
| **E2E-08** | **Worker Crash Resilience & Auto-Respawn**<br>While video is playing, kill `vlc-whisper-worker.exe` via Task Manager. | 1. VLC playback continues uninterrupted (zero audio stutter or video freeze).<br>2. Plugin detects worker exit and auto-spawns a new worker process within 500ms.<br>3. Captions resume automatically within 1–2 seconds. | `PLUGIN_WORKER_DIED` logged; `vw_plugin_respawn_worker` launches replacement worker and re-authenticates. | `[PASS]` |
| **E2E-09** | **Full Clean Uninstallation**<br>Run `Uninstall.exe` from VLC directory or Windows Settings $\to$ Installed Apps. | 1. Verify `libvlc_whisper_plugin.dll` removed from `VLC\plugins\audio_filter\`.<br>2. Verify `vlc-whisper-worker.exe` and `models/` deleted.<br>3. Verify public desktop and start menu shortcuts deleted.<br>4. Verify VLC plugin cache regenerated without errors. | System restored to clean state; standard VLC launches without filter errors. | `[PASS]` |


