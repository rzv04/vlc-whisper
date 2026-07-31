# Diff Analysis: Milestone 2 — Caption Presentation Spike (merge to main)

**28 files changed, +1546 / -807 lines** (branch `gemini/milestone-2` vs `main` @ `f859fc5`)
**Base**: `git diff main...HEAD`, plus 1 unstaged change in `plugin/src/vw_caption_presenter.c` (-1)

Scope: VLC module load/unload (roadmap step 9), PCM capture + SPSC queue (step 10), caption presentation spike with OSD path (step 11), out-of-tree packaging decision (step 12, ADR-012). Plan: `docs/plans/milestone_2_11_plan.md`.

---

## 1. File-by-File Analysis

### 1.1 `.agents/AGENTS.md` and `AGENTS.md`

**Why change**: Enforce header-function documentation as rule 11, mirroring the root file into the agent environment.

**Responsibility before**: 10 rules. **After**: 11 rules (every non-third-party function in `.h` files needs a 20-30 word comment, including realtime quirks).

**Callers**: AI agents. **Callees**: none.

**Happy path**: Rule 11 drives the doc comments added across `vw_queue.h`, `vw_audio_capture.h`, `vw_audio_buffer.h`, `vw_caption_presenter.h`. **Failure path**: N/A (policy).

**Boundaries**: N/A. **Acceptance map**: Rule 11 present — `.agents/AGENTS.md:17`, `AGENTS.md:17`. Status: done.

**Assumptions/Tradeoffs**: Duplication between the two AGENTS.md files remains a sync risk (they already diverged in whitespace).

---

### 1.2 `.gitignore`

**Why change**: Ignore `*.def`, MCP config JSON, and editor config (`opencode.json`).

**Responsibility before**: Ignored build artifacts, `diff.md`/`review.md`. **After**: Also `*.def`, `.agents/mcp_config.json` (comment: move to env vars), `opencode.json*`.

**Callers**: git. **Callees**: none.

**Happy path**: Secret-bearing configs stay untracked. **Failure path**: **`*.def` swallows the hand-written `plugin/libvlccore.def`** — a build input, not a build artifact (see Finding H-1).

**Boundaries**: Pattern specificity. **Acceptance map**: ignore secrets — `.gitignore:187-189`. Status: done; `*.def` rule: **broken by side effect**.

**Assumptions/Tradeoffs**: `*.def` was likely intended for MinGW-generated `.def` files; no negation for `plugin/libvlccore.def` was added.

---

### 1.3 `README.md`

**Why change**: Update Valgrind instructions to preset-based workflow, add stricter leak-check flags, and document manual Windows plugin installation (folded in from deleted `milestone_2_9_plan.md`).

**Responsibility before**: Build/test/coverage docs. **After**: Also manual install, cache-reset, and log-inspection walkthrough for Windows.

**Callers**: Developers. **Callees**: none.

**Happy path**: `ctest --test-dir build/linux-x64-debug -T memcheck` per new docs. **Failure path**: N/A.

**Boundaries**: N/A. **Acceptance map**: memcheck commands `README.md:60-75`; install guide `README.md:141-164`. Status: done.

**Assumptions/Tradeoffs**: Windows verification remains manual (VM note about `--avcodec-hw=none`).

---

### 1.4 `diff.md`

**Why change**: Superseded by this review. The previous review covered an earlier 5-file slice of the same milestone; this one covers the full branch.

**Responsibility before**: Milestone 2 partial review (base `b31f6b1..98a9b5e`). **After**: Full branch review vs `main`.

**Callers**: Humans. **Callees**: none. **Acceptance map**: replaced. Status: done.

---

### 1.5 `docs/architecture.md`

**Why change**: Reconcile buffering numbers with the implemented queue (16 chunks / 8 s), document chunk granularity, align ADR-005 wording ("drop new" not "drop old"), extend the data model with the fixed inline `pcm_data` chunk, add ADR-012.

**Responsibility before**: System architecture, timing, session/IPC specs. **After**: Same, plus exact chunk/queue parameters and the S16LE-inline chunk contract.

**Callers**: All implementers. **Callees**: none.

**Happy path**: Reader derives 512 ms chunk, 16-chunk queue, 8 s window from one table (`architecture.md:43-55`). **Failure path**: N/A.

**Boundaries**: N/A. **Acceptance map**: backlog 8 s (`:41`, `:53`), chunk struct (`:104-117`), drop-new policy (`:55`). Status: done — consistent with `vw_spsc_queue_create(16)` and `vw_queue.c` drop-incoming behavior.

**Assumptions/Tradeoffs**: "Drop newest" is a deliberate ADR-005 amendment: captions may lag up to the backlog instead of dropping stale audio. Consistent across code, diagrams, and strategy docs.

---

### 1.6 `docs/decisions.md`

**Why change**: Amend ADR-008 (drop newest audio under overload) and add ADR-012 (out-of-tree packaging over custom VLC build).

**Responsibility before**: ADRs 1-11. **After**: Amended ADR-008 + ADR-012.

**Callers**: Planning. **Callees**: none.

**Happy path**: ADR-012 anchors the Windows `libvlccore.def` import-library approach and the pinned-ABI constraint. **Failure path**: N/A.

**Acceptance map**: ADR-008 amendment `decisions.md:60`; ADR-012 `:85-94`. Status: done.

---

### 1.7 `docs/diagrams.md`

**Why change**: Queue-overload flow says "drop newest", cap corrected to 8 s.

**Responsibility before**: Diagrams. **After**: Same, corrected.

**Callers**: None. **Callees**: none. **Happy path**: Flowchart matches `vw_spsc_queue_push` drop behavior. **Acceptance map**: `diagrams.md:105,119`. Status: done.

---

### 1.8 `docs/plans/milestone_2_11_plan.md` (new)

**Why change**: Plan for the caption-presenter spike: native SPU primary, OSD fallback, mode enum.

**Responsibility before**: N/A. **After**: Acceptance criteria and DoD for the presenter work.

**Callers**: Agents executing milestone 2 step 11. **Callees**: none.

**Happy path**: Plan drives implementation. **Failure path**: Acceptance criteria remain **all unchecked** (`[]`) while the code is committed — the boxes were never updated post-implementation (see Finding M-4).

**Boundaries**: N/A. **Acceptance map**: criteria at `milestone_2_11_plan.md:31-33`; DoD `:35-38`. Status: criteria unchecked.

---

### 1.9 `docs/plans/milestone_2_9_plan.md` (deleted)

**Why change**: Superseded: its manual-install content moved to `README.md`; steps 9-12 are marked done in `roadmap.md`.

**Responsibility before**: Step 9 module load/unload plan. **After**: Deleted.

**Callers**: N/A. **Acceptance map**: content preserved in `README.md:141-164`. Status: done.

---

### 1.10 `docs/roadmap.md`

**Why change**: Mark Milestone 2 steps 9-12 complete.

**Responsibility before**: Open items. **After**: `[x]` on 9-12.

**Callers**: Planning. **Callees**: none.

**Happy path**: Roadmap reflects shipped work. **Failure path**: Step 11 wording says "native timed subtitle route preferred" — the native SPU route was **not implemented** (OSD only); marking the step done while the plan's primary route is missing overstates completion (see Finding M-5). Milestone exit status honestly remains **PLANNED** (`roadmap.md:32`).

**Acceptance map**: `roadmap.md:27-30`. Status: partial (step 11 overstates).

---

### 1.11 `docs/test-strategy.md`

**Why change**: Backlog hard limit 8 s to match code.

**Responsibility before**: Test gates. **After**: Same, corrected number.

**Callers**: Test planning. **Callees**: none. **Acceptance map**: `test-strategy.md:49`. Status: done.

---

### 1.12 `docs/vlc-api-essentials.md` (new, 366 lines)

**Why change**: Authoritative VLC 3.0.23 C API reference for the plugin: structures, realtime contract, clock/timeline, discontinuity, capability detection, object tree + vout retrieval, OSD rendering, module ABI.

**Responsibility before**: N/A. **After**: Vendor-verified reference; every claim cross-checked against the vendored headers (and the previously hallucinated `input_Control(INPUT_CAN_*)` section corrected to `var_GetBool` input variables + `demux_Control(DEMUX_CAN_*)` internals).

**Callers**: Plugin maintainers. **Callees**: none.

**Happy path**: Reader finds vout-retrieval algorithm matching `vw_caption_presenter_find_vout`, hold/release ownership table, and OSD no-op caveats. **Failure path**: N/A.

**Boundaries**: N/A. **Acceptance map**: Section 5 corrected APIs, Section 6 object tree + refcounting, Section 7 OSD semantics, Section 8 ABI. Status: done (verified against `worker/third_party/vlc-3.0.23/include/`).

**Assumptions/Tradeoffs**: `video_text.c` internals (osd var check, `strdup`) verified against VLC 3.0.x source, not vendored (src/ is not vendored).

---

### 1.13 `plugin/CMakeLists.txt`

**Why change**: Define `__PLUGIN__` + `MODULE_STRING` (entry-point ABI symbol), `_GNU_SOURCE`; generate a Windows import library (`libvlccore.dll.a`) from a hand-written `.def` so the DLL resolves VLC core symbols without linking a real libvlccore.

**Responsibility before**: Build the shared plugin. **After**: Same, plus ABI defines and Win32 import generation.

**Callers**: CMake/CTest, `test_plugin_load`. **Callees**: `dlltool`, `libvlccore.def`.

**Happy path**: Linux `.so` links; Windows `dlltool -d libvlccore.def -l libvlccore.dll.a` then links the plugin against the import lib. **Failure path**: **`libvlccore.def` is untracked (gitignored by `*.def`) — on any fresh checkout the custom command fails at `DEPENDS` and the Windows build breaks** (Finding H-1).

**Boundaries**: WIN32-guarded. **Acceptance map**: definitions `:11-15`; import generation `:30-38`. Status: Linux done; Windows build input not reproducible.

**Assumptions/Tradeoffs**: The `.def` exports `subpicture_New`/`subpicture_region_New`/`text_segment_New`/`subpicture_Delete` that no code uses (SPU path deferred); harmless but signals the gap. No `-Werror` on this target despite the plan's "warnings-as-errors" DoD (Finding M-2).

---

### 1.14 `plugin/include/vw_audio_capture.h`

**Why change**: Replace the stub `vw_audio_capture_on_pcm_block` API with the real capture contract: chunk constants, format enum, input descriptor, chunk struct with inline PCM, `vw_audio_capture_process_block`.

**Responsibility before**: Stub struct + stub function. **After**: Full zero-allocation capture interface (Rule 4).

**Callers**: `vw_audio_capture.c`, `vw_queue.h`, `vlc_whisper_module.c`, `test_audio_capture.c`, `test_queue.c`. **Callees**: none (declarations).

**Happy path**: `vw_plugin_filter` builds `vw_audio_input_t` from `fmt_in` + block and calls `process_block`. **Failure path**: N/A.

**Boundaries**: `VW_AUDIO_CHUNK_MAX_PCM_BYTES` 16384 (`:8`); `VW_AUDIO_TARGET_RATE` 16000 (`:9-11`); inline buffer guarantees zero heap in callback. **Acceptance map**: chunk struct (`:28-37`), input struct (`:42-52`), process entry (`:57-59`). Status: done.

---

### 1.15 `plugin/include/vw_caption_presenter.h`

**Why change**: Public presenter API: mode enum, `vw_caption_presenter_display`, doc comments per rule 11.

**Responsibility before**: Stub declarations. **After**: Typed API surface.

**Callers**: `vlc_whisper_module.c`, `test_caption_presenter.c`. **Callees**: none.

**Happy path**: `display` called per 100 blocks. **Failure path**: N/A.

**Boundaries**: `void* p_filter` trades type safety for zero VLC-header coupling in the header (Finding M-3). **Acceptance map**: enum (`:8-12`), display (`:18`), show_segment (`:22`), clear (`:25`). Status: done.

**Assumptions/Tradeoffs**: Enum and doc comments promise a native SPU channel that is never used (Finding M-5).

---

### 1.16 `plugin/include/vw_queue.h`

**Why change**: Real SPSC ring: chunk-slot capacity (capacity+1 sentinel), C11 atomics, documented push/pop/dropped semantics.

**Responsibility before**: Byte-capacity stub fields. **After**: Lock-free chunk ring.

**Callers**: `vlc_whisper_module.c`, `vw_audio_capture.c`, tests. **Callees**: none.

**Happy path**: `push` publishes with release store; `pop` consumes with acquire load. **Failure path**: N/A.

**Boundaries**: capacity 0 rejected at create; full → drop incoming chunk + accumulate `audio_dropped_us`. **Acceptance map**: struct (`:12-19`), push/pop semantics (`:29-38`). Status: done — memory ordering is the correct Vyukov pattern (release-store head, acquire-load head; acquire-load tail on push).

---

### 1.17 `plugin/src/vlc_whisper_module.c`

**Why change**: Replace stubs with the real VLC module: log sink bridge, per-block filter callback capturing PCM, SPSC queue (16 chunks), PoC periodic caption display, module registration with `vlc_entry__3_0_0f`.

**Responsibility before**: Stub entry points (`vlc_whisper_Open`/`Close` returning 0). **After**: Working audio filter module.

**Callers**: VLC pipeline (`vw_plugin_filter`). **Callees**: `vw_audio_capture_process_block`, `vw_spsc_queue_*`, `vw_caption_presenter_display`, `vw_log_*`.

**Happy path**: `vw_plugin_open` (`:91-113`) allocates sys, queue (16), sets `pf_audio_filter`, `fmt_out.audio = fmt_in.audio` (`:108`); `vw_plugin_filter` (`:48-83`) taps PCM, captures, and on block 1, 101, 201... calls `vw_caption_presenter_display` (`:77-78`), returns block untouched. **Failure path**: unsupported codec → passthrough (`:63-65`); no vout → display returns false, playback unaffected.

**Boundaries**: null sys/block guards (`:50`); zero-allocation in callback (chunk is stack, queue preallocated); `block_count` non-atomic but single aout thread per instance. **Acceptance map**: ABI entry (`:129-136`), passthrough invariant (`:108`), non-blocking (`:48-83`). Status: done.

**Assumptions/Tradeoffs**: PoC caption text is hardcoded; every-100-blocks cadence is a spike, not production. `msg_*` resolves against the Windows import lib via `vlc_Log` export.

---

### 1.18 `plugin/src/vw_audio_capture.c`

**Why change**: Implement resample/downmix-to-16k-mono-S16 + chunking with zero allocation.

**Responsibility before**: Stub returning false. **After**: Real capture converter.

**Callers**: `vw_plugin_filter`. **Callees**: `vw_spsc_queue_push`.

**Happy path**: 48 kHz stereo FL32 block → boxcar-averaged 16 kHz mono S16 chunks of ≤8192 frames with continuous `start_pts_us` (`chunk.duration_us` accumulated, `:74-77`). **Failure path**: invalid input → false (`:13-14`); `output_frames == 0` → early true (`:19-21`).

**Boundaries**: chunk cap (`:27`); OOB clamp `in_end > frame_count` (`:45-47`); sample clamp [-1,1] (`:62-63`). **Gaps**: `input->sample_rate == 0` divides by zero (no guard — VLC always sets `i_rate`, but the API is public; Finding L-1); `VLC_TICK_INVALID` PTS (`INT64_MIN`) blocks are enqueued with invalid timestamps (Finding L-2); odd-sized chunks truncate 62.5 us/sample durations (<1 us drift per odd chunk, negligible).

**Acceptance map**: chunking math (`:17-28`), downmix/resample (`:31-65`), PTS continuity (`:71-77`). Status: done; tested by `test_audio_capture` incl. exact chunk boundaries (8192/1808) and downmix value.

---

### 1.19 `plugin/src/vw_caption_presenter.c`

**Why change**: Implement vout discovery (parent walk + name search + children scan) and OSD rendering.

**Responsibility before**: Stubs. **After**: Real presenter; see this session's earlier deep dive — OSD (`vout_OSDText` at `:95`), not the SPU API; hold/release pairing correct per `vlc_input.h` (`input_GetVout` returns held).

**Callers**: `vlc_whisper_module.c`, tests. **Callees**: `input_GetVout`, `vlc_object_find_name` (deprecated), `vlc_object_hold/release`, `vlc_list_children`, `vout_OSDText`.

**Happy path**: `display` (`:100-112`) → `render_text` (`:89-98`) → `find_vout` (`:21-87`) finds `input` ancestor → `input_GetVout` → `vout_OSDText` → release. **Failure path**: no vout → WARN log, false, passthrough unaffected.

**Boundaries**: null text / `duration_us <= 0` rejected (`:101-102`); NULL filter = standalone test mode returns true (`:105-108`); mode ignored (see below). **Acceptance map**: find (`:21-87`), OSD render (`:95-96`), validation (`:102-103`), segment fallback 2 s (`:118-120`), clear (`:126-131`). Status: done, with exceptions below.

**Unstaged change in this file**: `(void)mode;` removed from `vw_caption_presenter_display` (HEAD had it at `:101`). The parameter is still unused → **`-Wunused-parameter` warning in the current tree** (confirmed in build output; Finding M-1). Nothing else in the working tree differs.

**Assumptions/Tradeoffs**: SPU mode silently maps to OSD; `vlc_subpicture` field name is a misnomer (holds the filter pointer, `:122`); `vlc_object_find_name` is `VLC_DEPRECATED` (documented in `vlc-api-essentials.md` Section 6, warning in every build).

---

### 1.20 `plugin/src/vw_queue.c`

**Why change**: Implement the lock-free ring.

**Responsibility before**: Stub push/pop. **After**: SPSC chunk ring with capacity+1 sentinel.

**Callers**: `vw_audio_capture.c`, module, tests. **Callees**: none.

**Happy path**: push writes slot, release-publishes head; pop acquire-reads, release-publishes tail. **Failure path**: full → drop incoming, count `duration_us` (`:44-47`); empty → NULL (`:64-66`).

**Boundaries**: `capacity_chunks == 0` → NULL (`:10-12`); ring allocation failure → NULL (`:18-21`). Memory ordering is correct (single-producer/single-consumer invariant). **Acceptance map**: create/destroy (`:9-33`), push (`:36-54`), pop (`:58-77`), dropped (`:80-82`). Status: done; covered by `test_queue` (creation, push/pop, overflow accounting, wraparound).

---

### 1.21 `protocol/CMakeLists.txt`

**Why change**: `POSITION_INDEPENDENT_CODE ON` — the plugin `.so` links `vw_protocol` statically.

**Responsibility before**: Static lib without PIC. **After**: PIC.

**Callers**: CMake. **Callees**: none. **Happy path**: plugin links on Linux. **Acceptance map**: `:18-19`. Status: done.

---

### 1.22 `tests/CMakeLists.txt`

**Why change**: Add VLC include path to unit-test macro; compile real plugin sources into tests (`vw_queue.c`, `vw_audio_capture.c`, `vw_caption_presenter.c`); register `test_plugin_load` with the built plugin as argument.

**Responsibility before**: Protocol/worker tests only. **After**: Plugin unit tests + dynamic-load test.

**Callers**: CTest. **Callees**: test executables.

**Happy path**: `ctest` runs 10 suites (verified: 10/10 pass, Valgrind clean). **Failure path**: N/A.

**Boundaries**: `test_caption_presenter` gets `__PLUGIN__`/`MODULE_STRING`/`_GNU_SOURCE` (`:24`). **Acceptance map**: `:21-25`, `:31-34`. Status: done.

**Assumptions/Tradeoffs**: `test_plugin_load` passes on Linux with `RTLD_LAZY` because the entry symbol is only resolved, never called. On Windows the DLL imports `libvlccore.dll` — the test would fail to load without it; no Windows test preset exists yet.

---

### 1.23 `tests/integration/test_plugin_load.c` (new)

**Why change**: Verify `vlc_entry__3_0_0f` resolves in the built plugin (roadmap step 9 acceptance).

**Responsibility before**: N/A. **After**: dlopen/LoadLibrary + symbol resolution test.

**Callers**: CTest. **Callees**: `dlopen`/`dlsym` or `LoadLibraryA`/`GetProcAddress`.

**Happy path**: loads `.so`, resolves entry, prints success. **Failure path**: missing arg → usage + exit 1; load/symbol failure → exit 1.

**Boundaries**: argc check (`:10-13`). **Acceptance map**: `:31-41` (POSIX branch). Status: done; passes under Valgrind (10/10).

---

### 1.24 `tests/unit/test_audio_capture.c` (new)

**Why change**: Unit-test resample/downmix/chunking against exact numbers.

**Responsibility before**: N/A. **After**: Deterministic capture tests.

**Callers**: CTest. **Callees**: `vw_audio_capture_process_block`, queue pop.

**Happy path**: 30000 frames 48k stereo FL32 → exactly 10000 output frames → chunks 8192 + 1808 with correct PTS chaining and 5461±1 downmix value. **Failure path**: assertion abort.

**Boundaries**: chunk sizes (`:39-40`, `:51-53`), PTS continuity (`:52`), empty-pop (`:56`). **Acceptance map**: `:31-56`. Status: done.

**Assumptions/Tradeoffs**: Test encodes the boxcar behavior (first output frame averages 6 input samples) — brittle if the resampler algorithm changes.

---

### 1.25 `tests/unit/test_caption_presenter.c` (new)

**Why change**: Unit-test presenter validation and standalone mode without live VLC; VLC symbols stubbed.

**Responsibility before**: N/A. **After**: Presenter edge-case coverage.

**Callers**: CTest. **Callees**: `vw_caption_presenter_*` + stubs.

**Happy path**: null text/duration rejected; NULL-filter display returns true in all three modes; segment display/clear. **Failure path**: assertion abort.

**Boundaries**: null/negative-duration (`:59-62`), null segment/text (`:75-77`), stub linkage via `#undef` of VLC macros (`:19-49`). **Acceptance map**: `:59-85`. Status: done, with caveats.

**Assumptions/Tradeoffs**: The `#undef`-then-redefine stub pattern is brittle if VLC headers change macros; the vout-search path is never exercised (stubs return NULL); test 3 asserts `VW_PRESENTER_MODE_SPU` succeeds — codifying the fiction that SPU mode works (Finding M-5); leftover `(void)segment; (void)empty_seg;` suppressions for variables that are used (`:77-78`, Finding L-3).

---

### 1.26 `tests/unit/test_queue.c`

**Why change**: Full SPSC coverage: creation, push/pop, overflow accounting, wraparound.

**Responsibility before**: Smoke test. **After**: Behavioral tests of the ring.

**Callers**: CTest. **Callees**: queue API.

**Happy path**: 10 push/pop cycles across capacity-3 ring verify wraparound. **Failure path**: assertion abort.

**Boundaries**: capacity 0 (`:8-10`), overflow accounting 50000+50000 us (`:50-61`). **Acceptance map**: `:8-64`. Status: done.

---

### 1.27 `worker/include/vw_audio_buffer.h`

**Why change**: Rule 11 doc comments; explain the intentional plugin/worker PCM representation split (inline S16LE vs heap float32).

**Responsibility before**: Undocumented buffer API. **After**: Documented contract.

**Callers**: Worker (future inference loop). **Callees**: none.

**Happy path**: Reader understands the two representations. **Failure path**: N/A. **Acceptance map**: `:31-34`, `:72-82`. Status: done.

**Assumptions/Tradeoffs**: Worker inference loop remains stubbed (`vw_whisper_engine.c` ignores input) — this milestone is plugin-side only.

---

## 2. Happy-Path Request Trace

Plugin load → audio block → capture → queue → caption OSD:

1. VLC loads `libvlc_whisper_plugin.so` and calls `vlc_entry__3_0_0f` → `vw_plugin_open` — `plugin/src/vlc_whisper_module.c:91-113`
2. `vw_plugin_open`: `calloc` sys; `vw_spsc_queue_create(16)` (8 s, 512 ms chunks) — `:96`; sets `pf_audio_filter = vw_plugin_filter`, `fmt_out.audio = fmt_in.audio` — `:107-108`
3. VLC calls `vw_plugin_filter` per audio block — `:48-83`; codec mapped S16/S32/FL32 — `:53-62`; unsupported codec → passthrough — `:63-65`
4. `vw_audio_capture_process_block(&sys->capture, &input)` — `:73` → `plugin/src/vw_audio_capture.c:12-80`: downmix/resample to 16 kHz mono S16, split into ≤8192-frame chunks, contiguous PTS
5. Each chunk pushed to the ring — `vw_audio_capture.c:75` → `plugin/src/vw_queue.c:36-54` (release-store head)
6. `block_count` hits 1, 101, ... — `vlc_whisper_module.c:76-78` → `vw_caption_presenter_display(..., 2000000, VW_PRESENTER_MODE_AUTO)` — `plugin/src/vw_caption_presenter.c:100-111`
7. `find_vout` walks `obj.parent`: filter → audio output → decoder → input; `input_GetVout` returns held vout — `:21-87`
8. `vout_OSDText(vout, 1, SUBPICTURE_ALIGN_BOTTOM, 2000000, text)` — `:95`; release — `:96`
9. Filter returns the original block untouched — `vlc_whisper_module.c:83`

Not yet wired: the worker/IPC consumer of the queue (sender + worker inference loop are outside this milestone; the queue is produced into but nothing drains it yet).

## 3. Most Important Failure Path

**Merge blocker — Windows build cannot reproduce on a fresh checkout:**

1. Merge to main; fresh clone/CI checkout
2. `.gitignore:144` (`*.def`) excludes `plugin/libvlccore.def` from the tree
3. Windows preset configure: `add_custom_command` has `DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/libvlccore.def` — `plugin/CMakeLists.txt:31-38`
4. File missing → `dlltool` invocation fails (or Ninja errors on missing dependency) → `libvlccore.dll.a` never generated → `vlc_whisper_plugin.dll` link fails
5. Only this machine's worktree (file present but untracked) builds; every other machine is broken. Fix: `git add -f plugin/libvlccore.def` or add `!plugin/libvlccore.def` negation before committing.

**Runtime failure path (graceful by design):** audio-only media → `find_vout` finds no vout → WARN `PRESENTER_VOUT_NOT_FOUND` (`vw_caption_presenter.c:85-86`) → false → passthrough continues; OSD disabled by user settings or `duration <= 0` → `vout_OSDText` silent no-op (documented).

## 4. Boundary Summary

| Boundary type | What to check | Code location | Status |
| --- | --- | --- | --- |
| **Input validation** | Null text / duration <= 0 | `vw_caption_presenter.c:101-102` | Validated |
| **Input validation** | Null segment / text_utf8 | `vw_caption_presenter.c:115-116` | Validated |
| **Input validation** | Queue capacity 0 / alloc failure | `vw_queue.c:10-21` | Validated |
| **Input validation** | Null sys / block in callback | `vlc_whisper_module.c:50` | Validated |
| **Input validation** | `sample_rate == 0` in capture | `vw_audio_capture.c:17-18` | **Gap — division by zero** |
| **Input validation** | `VLC_TICK_INVALID` PTS blocks | `vw_audio_capture.c:22` | **Gap — enqueued with INT64_MIN** |
| **Input validation** | `(void*)` → `filter_t*` cast | `vw_caption_presenter.c:104` | No runtime type check (documented) |
| **Authorization** | In-process module; token auth is worker-side (prior milestone) | N/A | N/A here |
| **Concurrency** | SPSC memory ordering | `vw_queue.c:41,43,49,61,63,75` | Correct (release/acquire pairs) |
| **Concurrency** | `block_count` non-atomic | `vlc_whisper_module.c:76` | Safe — single aout thread per instance |
| **Concurrency** | `input_GetVout`/`find_name` lock acquisition on audio thread | `vw_caption_presenter.c:33,44,57` | Accepted (short-held), low risk |
| **I/O** | None in callback (no pipe writes this milestone) | `vlc_whisper_module.c:48-83` | Compliant (Rule 4) |
| **Persistence** | `libvlccore.def` tracked | `.gitignore:144` | **Gap — build input untracked** |

## 5. Acceptance Criterion → Code Mapping

| # | Criterion | Plan Source | Code | Test | Status |
| --- | --- | --- | --- | --- | --- |
| 1 | Single DLL contains both SPU and OSD display logic | M2.11 plan `:31` | OSD only; SPU enum exists (`vw_caption_presenter.h:8-12`) | `test_caption_presenter.c:63-65` | **Missing** (SPU) |
| 2 | Automatic fallback to OSD if SPU unavailable | M2.11 plan `:32` | Always OSD; "fallback" never exercised | N/A | Partial |
| 3 | Tested against sample video without crashes | M2.11 plan `:33` | README manual Windows run | Not automated | Partial |
| 4 | `vlc_entry__3_0_0f` ABI entry point | Roadmap step 9 | `vlc_whisper_module.c:129-136` | `test_plugin_load.c:31-41` | Done |
| 5 | Capture PCM + PTS, non-blocking, canonical 16k S16 | Roadmap step 10 | `vw_audio_capture.c:12-80` | `test_audio_capture.c:31-56` | Done |
| 6 | Bounded queue, playback wins, measurable drops | Roadmap step 10 / ADR-008 | `vw_queue.c:36-54` | `test_queue.c:44-61` | Done |
| 7 | Out-of-tree packaging decision | Roadmap step 12 / ADR-012 | `docs/decisions.md:85-94` | N/A | Done |
| 8 | C17, no project C++ | M2.11 DoD | All files | build | Done |
| 9 | No blocking in audio callback | M2.11 DoD | `vlc_whisper_module.c:48-83` | invariant | Done |
| 10 | Warnings-as-errors and formatting pass | M2.11 DoD | — | `clang-format --Werror` clean; build emits 2 warnings; no `-Werror` on plugin | Partial |

## 7. Code Review Findings

### Bugs (Sorted by Priority)

| Priority | Component / Location | Description | Impact | Proposed Fix |
| --- | --- | --- | --- | --- |
| **High** | `.gitignore:144` + `plugin/CMakeLists.txt:31-38` | `*.def` rule ignores the hand-written `plugin/libvlccore.def`, which is a `DEPENDS` of the Windows import-library generation. File exists only in this worktree, untracked. | Windows cross-build fails on any fresh clone/CI; milestone 2 Windows deliverables unbuildable | `git add -f plugin/libvlccore.def` (or add `!plugin/libvlccore.def` negation) before merge |
| **Medium** | `plugin/src/vw_caption_presenter.c:100` (unstaged) | `(void)mode;` deleted; parameter still unused (warning at declaration line 100) | `-Wunused-parameter` warning in current tree; build no longer warning-clean | Restore `(void)mode;` or implement mode dispatch (AUTO/OSD → OSD; SPU → OSD + WARN) |
| **Medium** | `plugin/src/vw_audio_capture.c:17-18` | `output_frames = (frame_count * 16000) / input->sample_rate` with no `sample_rate == 0` guard | Division by zero (crash) if any caller passes rate 0; API is public and testable | Early-reject `input->sample_rate == 0` |

### Architectural & Operational Risks

| Category | Risk Description | Affected Files | Mitigation Strategy |
| --- | --- | --- | --- |
| **Build reproducibility** | Windows import lib depends on an untracked file (see H-1) | `plugin/CMakeLists.txt`, `plugin/libvlccore.def` | Track the .def; add a CI job for the windows-x64 preset to catch this class of issue |
| **SPU promise vs OSD reality** | Enum, header comments, unit test, roadmap all assert a native SPU channel that does not exist; the `.def` even exports unused SPU symbols | `vw_caption_presenter.h`, `test_caption_presenter.c`, `roadmap.md:29`, `libvlccore.def` | Either implement `subpicture_New`/`vout_PutSubpicture` path in M3 and keep the enum, or strip SPU from enum/docs/tests now |
| **Unenforced warning gate** | Plan DoD claims warnings-as-errors, but plugin target has no `-Werror`; two live warnings (deprecated `vlc_object_find_name`, unused `mode`) | `plugin/CMakeLists.txt:41-45`, `vw_caption_presenter.c` | Add `-Werror`; address or document both warnings |
| **Consumer gap** | Queue is produced into but nothing drains it; worker inference loop stubbed — milestone is plugin-side only | `vw_audio_capture.c`, `worker/src/vw_whisper_engine.c` | Explicitly out of scope for M2; first M3 task is the sender |
| **Windows test gap** | `test_plugin_load` would fail on Windows (DLL imports libvlccore.dll, not present in test env); no windows test preset | `tests/CMakeLists.txt:31-34` | Ship real libvlccore.dll in CI or skip test on Windows with a documented reason |
| **OSD-only captions** | `vout_OSDText` overlays may be disabled by user `osd` setting, styled differently, or coexisting poorly with native subtitles | `vw_caption_presenter.c:95` | Documented in `vlc-api-essentials.md` Section 7; revisit SPU in M3 |

### Code Style & Quality Nitpicks

| Issue Type | File & Line | Description | Recommendation |
| --- | --- | --- | --- |
| **Dead Code** | `tests/unit/test_caption_presenter.c:77-78` | `(void)segment; (void)empty_seg;` suppressions for variables that are used | Remove both lines (previously flagged, still open) |
| **Naming** | `vw_caption_presenter.h:16` / `.c:122` | Field `vlc_subpicture` stores a filter pointer, never a subpicture | Rename to `p_filter_ctx` |
| **Misleading Test** | `test_caption_presenter.c:63-65` | Asserts SPU mode succeeds when SPU is unimplemented (NULL-filter early-return masks it) | Drop the SPU assertion or gate on mode semantics |
| **Unused Include** | `vw_caption_presenter.c:10` | `<vlc_block.h>` included, no `block_t` used | Remove (keep `vlc_common.h` ordering rule) |
| **Docs/Plan Drift** | `milestone_2_11_plan.md:31-33` | Acceptance criteria unchecked while code is committed | Check off or annotate each box |
| **Roadmap Overstatement** | `roadmap.md:29` | Step 11 marked done though native subtitle route unimplemented | Reword to "OSD overlay proven; native SPU deferred to M3" |
| **Negative duration** | `vw_caption_presenter.c:118-120` | Negative segment duration silently becomes 2 s default; invalid data should be rejected or warned | WARN on negative; reject instead of defaulting |

---

**Bottom line**: The Linux-side milestone is in good shape — 10/10 tests pass, Valgrind clean, memory ordering and hold/release ownership are correct, and the docs are now consistent (8 s backlog everywhere). The merge is **blocked by H-1** (untracked `libvlccore.def` breaks the Windows build on any other machine); fix that, then decide on the `(void)mode` warning and the SPU-vs-OSD promise before merging.
