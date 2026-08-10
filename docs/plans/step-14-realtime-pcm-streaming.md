# Step 14: Real-Time PCM Streaming (split into 14a, 14b, 14c)

## Goal

Roadmap step 14 — "Feed captured PCM chunks from SPSC queue across IPC transport to worker process in real time." Because the worker audio core is currently **all stubs** (`vw_whisper_engine.c`, `vw_audio_buffer.c`, START/AUDIO handlers), step 14 is split per the 17a–d precedent:

- **14a — Worker audio pipeline** (branch `gemini/milestone-3-step-14`, current branch): worker consumes AUDIO frames over IPC and emits CAPTION_SEGMENT frames. Verifiable without plugin changes.
- **14b — Plugin client API and transport** (branch `gemini/milestone-3-step-14b` after 14a merges): adds receive timeouts, process wait handles, and the worker client state machine.
- **14c — Plugin real-time streaming** (branch `gemini/milestone-3-step-14c` after 14b merges): plugin drains the SPSC queue on a background sender thread and implements model discovery.

Both must satisfy the postmortem rule: one step per dedicated branch, merged before the next (docs/plans/milestone3_postmortem.md:230).

## Context

- Relevant docs/ADR: `docs/architecture.md` (threading table, chunking, backpressure = ADR-008), `docs/decisions.md` ADR-013 (worker reader thread), ADR-015 (model-once lifetime), `docs/api-contracts.md` (START/STARTED/AUDIO/ERROR table), `docs/test-strategy.md` (bounded-queue + drop-counter cases, 8s/2s windowing), `docs/plans/milestone3_postmortem.md` (Phase A).
- Current state (verified): capture→queue producer path works; `vw_spsc_queue_pop` has **zero callers**; plugin has **no threads**; `vw_worker_client` only does HELLO handshake; worker START/AUDIO/STOP handlers are empty stubs; `vw_whisper_engine_transcribe_pcm` is a no-op; `vw_audio_buffer_append_s16le` is a no-op. Real: VAD (whisper.cpp + energy fallback), segment builder (push-only, no drain), codec for all frame types, whisper.cpp linked into worker.
- Model file `models/ggml-tiny.en.bin` (77 MB, sha256 pinned in `models/manifest.json`) is **not** in the repo → model-dependent tests must skip cleanly (return 77) when absent.
- Assumptions / non-goals: no PAUSE/RESUME (step 16), no seek/discontinuity (step 17), no SPU/GPU/source mode (17a–c), no presenter wiring (step 15). Windowing decision: keep **8 s window / 2 s hop** (matches `vw_segment_builder` constants, architecture.md, test-strategy.md); record the postmortem's 4 s/2 s mention as a doc discrepancy in `docs/decisions.md`.

## Scope

### 14a — Worker audio pipeline

In scope:
1. **`worker/src/vw_whisper_engine.c`** (real implementation):
   - `vw_whisper_engine_init`: `whisper_init_from_file_with_params` + one warmup inference on a silent buffer (ADR-015: model loads once per process); NULL on failure.
   - `vw_whisper_engine_transcribe_pcm`: `whisper_full` (language "en", translate=false, n_threads = clamped hardware concurrency). Use 64-bit/size_t sample math — avoid the historical `VW_WINDOW_SAMPLES` int32 overflow bug (postmortem:44-49).
   - New accessor `const char* vw_whisper_engine_get_text(engine)` — concatenated text of last run, valid until next transcribe (avoids heap churn).
2. **`worker/src/vw_audio_buffer.c`** (real implementation): heap float32 ring, `append_s16le` converts S16→float (÷32768) and appends; bounded by `max_samples` (use 10 s = 160 000 samples); drop-oldest on overflow updating `start_pts_us`; expose `samples()/count()` for window slicing.
3. **`worker/src/vw_worker.c`** session + pipeline:
   - Startup: load engine from `config->model_path`; on failure keep serving and answer START with `ERROR` (`E_MODEL_MISSING` / `E_MODEL_INVALID`, recoverable=0) per api-contracts.md:110-111.
   - Threading per ADR-013: main thread = frame reader only; new inference thread consumes a bounded internal audio queue (~32 chunks), **drop-oldest** policy + `dropped_audio_us` counter. Control events (START/STOP/SHUTDOWN) pass via atomics + queue flush.
   - START → validate, store session_id/timeline origin, reply `STARTED` (header-only). Reject AUDIO with mismatched session id.
   - AUDIO → append to buffer; while buffered ≥ window (8 s): energy-VAD gate (`vw_vad_detect_speech_energy`, `VW_VAD_ENERGY_THRESHOLD`) → transcribe → `vw_segment_builder_push_hypothesis(text, window_start_pts, window_end_pts)` → advance 2 s hop.
   - Drain builder → send `CAPTION_SEGMENT` frames (worker sequence continues from HELLO_ACK, starts at 2). Rate-limited `STATUS` with `dropped_audio_us` when drops occur (E_BACKPRESSURE diagnostic).
   - STOP → flush buffer, reset session state. SHUTDOWN → stop flags, join inference thread, exit 0.
4. **`worker/src/vw_segment_builder.c`**: add `bool vw_segment_builder_pop(builder, vw_caption_segment_t* out)` (oldest-first, transfers text ownership).
5. **Platform threads (shared with 14b)**: add to `plugin/include/vw_platform.h` + both platform files: `vw_platform_thread_create/join`, `vw_platform_sleep_ms` (pthread / Win32). Production code uses these; tests may keep raw pthread.
6. **Tests**:
   - `test_audio_buffer` (new, unit): append/convert/overflow-drop/PTS bookkeeping.
   - `test_segment_builder`: extend with pop/drain tests.
   - `test_whisper_engine` (new, unit, **skip if model absent**): init on `models/ggml-tiny.en.bin`, transcribe deterministic sine/silence fixture → expect success + empty/no-crash output; NULL path for bad model path.
   - Extend `tests/integration/test_worker_lifecycle.c` (or new `test_worker_streaming`): START→STARTED with model, START→ERROR(E_MODEL_MISSING) without; send AUDIO chunks → no crash → STOP → SHUTDOWN exit 0. SEGMENT emission proven at codec+builder level; speech E2E is step 18 acceptance.
7. **Docs**: roadmap entry `14.` → `14a`/`14b`/`14c` (17-style lettering) with 14a checked on completion; `docs/decisions.md` (window-size discrepancy note); `docs/architecture.md` (worker threads now implemented); `docs/source-layout.md` if files added.

### 14b — Plugin client API and transport (after 14a merges; branch `gemini/milestone-3-step-14b`)

In scope:
1. **`plugin/src/vw_worker_client.c` API**:
   - State: `session_id`, per-direction sequence counters, `session_active`, worker pid/process handle.
   - `vw_worker_client_start_session(client, timeline_origin_pts_us, model_id)`: random 16-byte session id, START payload (16000/1/S16LE/"en"/`VW_SOURCE_LOCAL_FILE`), wait for STARTED (≤5 s deadline; treat ERROR as failure).
   - `vw_worker_client_send_audio(client, chunk)`: AUDIO payload from `vw_audio_chunk_t` (pcm_bytes must equal `duration_us*32/1000` — already guaranteed by capture).
   - `vw_worker_client_stop_session(client, reason)`, `vw_worker_client_shutdown(client)`.
   - Transport: add `vw_ipc_receive_timeout(handle, buf, len, timeout_us)` to `protocol/` (both `vw_ipc_socket_linux.c` and `vw_ipc_pipe_win32.c`); existing `vw_ipc_receive` keeps the 3 s default. Protocol change: **none** (transport-internal API).
   - Process lifecycle: `vw_platform_spawn_process` gains an out handle (pid / `HANDLE`); add `vw_platform_wait_process(handle, timeout_ms)`; disconnect waits the worker after SHUTDOWN (kills the fire-and-forget zombie/leak).
2. **Tests**: client-API unit tests against a fake server socket (START/STARTED, AUDIO framing + sequence, STOP, receive-timeout path); memcheck clean.
3. **Docs**: roadmap 14b checked; `docs/api-contracts.md` (if transport API noted), `docs/source-layout.md`.

### 14c — Plugin real-time streaming (after 14b merges; branch `gemini/milestone-3-step-14c`)

In scope:
1. **`plugin/src/vw_whisper_module.c`** sender thread:
   - sys gains thread handle, `_Atomic bool running`, `_Atomic bool worker_dead`, counters.
   - Loop (~20 ms cadence when queue empty): pop chunk → `send_audio`; failure → `worker_dead=true` + one rate-limited `PLUGIN_WORKER_UNAVAILABLE`-style log, stop sending (playback untouched). Same loop drains worker frames via `receive_timeout(50 ms)`: SEGMENT counted + discarded (step 15 wires the presenter), STATUS/ERROR logged, FATAL → `worker_dead`.
   - Open: launch+connect → `start_session` → start thread; any failure → passthrough-only (existing behavior).
   - Close: `running=false` → join → best-effort STOP + SHUTDOWN → wait process → disconnect.
   - Model discovery: resolve `ggml-tiny.en.bin` (probe plugin-dir ancestors ≤4, VLC exe dir, `models/` subdir of each) and pass `--model <abs>` in spawn argv; plus `add_loadfile("model-path", …)` module option override, same pattern as `worker-path`.
2. **Tests**: integration: full connect→START→stream→teardown, and worker-death-mid-stream detach test; memcheck clean.
3. **Docs**: roadmap 14c checked; `docs/architecture.md` (plugin sender thread), README usage section (model placement, `--vlc-whisper-model-path`).

Out of scope (all): PAUSE/RESUME (16), seek/discontinuity (17), SPU/GPU/source (17a–c), presenter display (15), VAD model-based gating (energy only), partial segments capability.

Files expected to change:
- 14a: `worker/src/vw_whisper_engine.c`, `worker/src/vw_audio_buffer.c`, `worker/src/vw_worker.c`, `worker/src/vw_segment_builder.c`, `worker/include/*.h`, `plugin/include/vw_platform.h`, `plugin/src/vw_platform_linux.c`, `plugin/src/vw_platform_win32.c`, `tests/**`, docs.
- 14b: `plugin/src/vw_worker_client.c`, `plugin/include/vw_worker_client.h`, `protocol/include/vw_ipc_transport.h`, `protocol/src/vw_ipc_socket_linux.c`, `protocol/src/vw_ipc_pipe_win32.c`, `tests/**`, docs.
- 14c: `plugin/src/vw_whisper_module.c`, `plugin/libvlccore.def` (if new VLC symbols needed), `tests/**`, docs.

## Design

- Inputs/outputs: 14a — IPC AUDIO frames in, CAPTION_SEGMENT out. 14b — `vw_audio_chunk_t` from SPSC queue in, AUDIO frames out; worker frames drained/discarded until step 15.
- Ownership/threading: only the plugin sender thread writes the pipe (architecture.md threading table); realtime callback keeps enqueue-only. Worker: reader thread never blocks on inference (ADR-013) — bounded drop-oldest internal queue between reader and inference thread.
- Bounds & failure: 16 KB/512 ms chunks; queue 16 chunks (8 s) drop-newest plugin-side (ADR-008); worker internal queue ~32 chunks drop-oldest; 3 s I/O timeouts; all limits bounded; on any pipe failure captions degrade to passthrough, playback never affected.
- Privacy/security: no PCM/transcript disk logging; token auth unchanged; error messages redacted.
- Protocol change: **none** (all frame types exist; codec validated). Transport gets a timeout-param API (internal).

## Acceptance criteria

- [ ] 14a: START→STARTED round-trip over IPC; AUDIO chunks accepted and transcribed with real model; CAPTION_SEGMENT frames emitted for speech windows; ERROR(E_MODEL_MISSING) without model; SHUTDOWN exit 0.
- [ ] 14b: client API unit tests pass (socket mocking); timeouts and process lifecycle behave correctly.
- [ ] 14c: playback of a local file with model streams AUDIO continuously (logs), no queue-full drops in steady state, worker death ⇒ clean passthrough, close leaves no zombie worker.
- [ ] Failure behavior preserves VLC playback in every path.
- [ ] Automated tests cover success and failure; model-gated tests skip (77) when model absent.
- [ ] Docs/roadmap/decisions updated in the same change.

## Test plan

Per AGENTS.md checklist, for each sub-step:
```
clang-format --dry-run --Werror <touched files>
cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug --output-on-failure
ctest --test-dir build/linux-x64-debug -T memcheck
```
Model setup (local dev): download `ggml-tiny.en.bin` (sha256 `c78c8657…` per `models/manifest.json`) into `models/`; without it, gated tests return 77 (skipped).
Manual (14b): `vlc -vvv --audio-filter=vlc_whisper <video>` → logs show START/STARTED then steady AUDIO sends; no audio glitches; Ctrl+C/close → worker exits.

## Definition of done

- [ ] C17 only; `vw_` prefix; no blocking/allocation in VLC audio callback
- [ ] No network, telemetry, or PCM/transcript persistence
- [ ] All queues/windows/retries bounded; drop counters observable
- [ ] Unit + integration tests pass; Valgrind memcheck 100% clean
- [ ] clang-format clean; conventional commits with co-author trailer
- [ ] Roadmap (14a/14b/14c), architecture, decisions, source-layout, test-strategy updated
- [ ] Reproducible from clean checkout

## Evidence

- Build/test/memcheck outputs appended per sub-step.
- Known follow-ups: step 15 presenter wiring for drained SEGMENT frames; 4 s vs 8 s window discrepancy recorded in decisions.md; CI model-download job belongs to step 19.

---

## Execution order (14a, this branch)

0. Plan bookkeeping — done: this plan is persisted in-project at `docs/plans/step-14-realtime-pcm-streaming.md` (commit `7aa66cf`); the roadmap `14` → `14a`/`14b` split is restored in the working tree (uncommitted, to be folded into the first 14a commit).
1. Platform thread helpers (`vw_platform.h` + linux/win32) + unit tests.
2. Real `vw_audio_buffer` + tests.
3. Segment builder `pop` accessor + tests.
4. Real `vw_whisper_engine` + model-gated tests.
5. Worker session pipeline: START/STARTED, AUDIO ingest, reader/inference threads, windowing + VAD, SEGMENT emission, ERROR/STATUS paths.
6. Integration tests (START/AUDIO/STOP/SHUTDOWN; model-gated vs ERROR paths).
7. Docs (architecture, decisions, source-layout, roadmap checkbox), full verification, commit, push.

Then open/merge 14a before starting 14b on `gemini/milestone-3-step-14b`, and 14b before 14c.
