# Task: Wire Lua Settings GUI to Plugin/Worker (Step 19b)

## Goal
A single user-visible outcome: a VLC Lua extension dialog ("VLC-Whisper Settings" under `View`) exposes four PotPlayer-parity controls — Engine, Model, Language, Threads — that persist via the plugin's config namespace and reach the running worker through an existing epoch/respawn channel; a status label shows the detected backend after the worker reports it. No translation, no auto-detect language, no lazy model download in this step.

## Context
- Relevant docs/ADR:
  - `docs/decisions.md` ADR-022 (Lua extension as primary GUI host, standalone `vlc-whisper-settings.exe` retained as rich-panel tier; `audio_filter` cannot own menu — Extensions menu is the blessed surface).
  - `docs/plans/step19a_research_dossier.md` §§1–4 (module capabilities, worker single-listener constraint, per-call vs context-init Whisper params).
  - Roadmap 19b spec + product invariants (`AGENTS.md` Rule 4: never block audio callback; Rule 5 local-first product — no cloud transcription at playback time).
  - Pinned default model `tiny.en` is English-only (`is_multilingual()==false`); DoD requires no auto-detect entry.
- VLC/worker/protocol version affected:
  - Protocol v1.2 → v1.3 (minor bump, additive tail field; same-major-wire-compatible).
- Assumptions and explicit non-goals:
  - Assumption: `vlc.config.set/get` exists for string+int config vars in VLC 3.0.23 Lua (verified via `share/lua/README.txt` + vendored headers `vlc_configuration.h:99,104,116`).
  - Non-goal: per-call live-apply without respawn for language/threads — this iteration applies all four via worker respawn (~2 s poll); live optimization is a future refinement.
  - Non-goal: in-app model downloading (deferred to 19c — `cmake/vw_provision_model.cmake` reuse), translation (21), auto-detect language (deliberately omitted per English-only default model).

## Scope
- In scope:
  - Protocol: bump `VW_PROTOCOL_MINOR` 2→3 and `VW_CLIENT_VERSION`/`VW_WORKER_VERSION` 1.2.0→1.3.0; append `char resolved_backend[16]` (tail field, "gpu"|"cpu", NUL-padded, 60 B wire) to `vw_msg_status_t`; codec encode writes +16 B, decode accepts legacy 44 B (zero-fill) and new 60 B; validate unchanged; update golden bytes + roundtrip test.
  - Worker: extend `vw_worker_config_t` with `char language[16]` (default "en") and `int n_threads` (default 4); add CLI `--language <code>` (reject literal "auto") and `--n-threads N` (integer 1..16, clamp); store in engine (`language[16]`, `n_threads`) with setters; transcribe `wparams` reads from engine fields (fallback "en", clamp); warmup reuses same fields; expose `VW_HAVE_VULKAN` compile define from `worker/CMakeLists.txt`; send one `VW_MSG_STATUS` immediately after each `STARTED` reply carrying `resolved_backend = (backend==cpu||!VW_HAVE_VULKAN)?"cpu":"gpu"`.
  - Plugin: register config vars `whisper-backend` (string "auto", choices auto|gpu|cpu), `whisper-language` (string "en", concrete codes en/ro/tr/de/fr/es — no auto), `whisper-threads` (integer 4, range 1..16), plus read-only mirror `whisper-backend-active` (string); extend `vw_worker_client_launch_and_connect` argv 8→16 to forward `--backend`/`--gpu-device`/`--language`/`--n-threads`; sender loop snapshots the four keys + `model-path`/`worker-path` every ~2 s and calls `vw_plugin_respawn_worker` on any diff (respawn_in_progress guard); `STATUS` drain mirrors `resolved_backend` into `whisper-backend-active` via `config_PutPsz` (VLC config lock protects the cross-thread write); export `config_GetInt`/`config_PutPsz`/`config_FindConfig` in `plugin/libvlccore.def` for MinGW link.
  - Lua+bundling+docs: rewrite `lua/extensions/vlc_whisper_settings.lua` to wired version (activate reads `vlc.config.get` nil-safe, preselects via id maps, 6-entry Language dropdown without auto, model dropdown 7 labels mapped to `models/*.bin` relative paths, `w_status` label showing `whisper-backend-active` pending→resolved, Apply validates threads tonumber clamp 1..16 then `vlc.config.set` ×4); `lua/README_SETTINGS.md` wired docs; `lua/README_SPIKE.md` redirect; installer NSIS `File` lua extension + `RMDir` cleanup on uninstall; CPack `lua` directory install; `README.md` Step 19b manual-test section and `docs/roadmap.md` 19b shipped note.
- Out of scope:
  - Auto-detect language setting (English-only default model makes it meaningless).
  - Lazy model downloading (19c owns the SHA-256-pinned provision flow).
  - Translation network code (21 is opt-in cloud boundary).

- Files/components expected to change:
  - `protocol/include/vw_protocol_types.h`, `protocol/src/vw_protocol_codec.c`, `tests/unit/test_protocol_codec.c`
  - `worker/CMakeLists.txt`, `worker/include/vw_worker_config.h`, `worker/src/vw_worker_config.c`, `worker/include/vw_whisper_engine.h`, `worker/src/vw_whisper_engine.c`, `worker/src/main.c`, `worker/src/vw_worker.c`, `tests/unit/test_worker_config.c`
  - `plugin/src/vw_whisper_module.c`, `plugin/src/vw_worker_client.c`, `plugin/include/vw_worker_client.h`, `plugin/libvlccore.def`
  - `lua/extensions/vlc_whisper_settings.lua`, `lua/README_SETTINGS.md`, `lua/README_SPIKE.md`, `cmake/vw_installer.nsi.in`, `cmake/vw_packaging.cmake`, `README.md`, `docs/roadmap.md`

## Design
- Inputs and outputs:
  - Inputs: user dropdown/text selections in the Lua dialog; stored in VLC config namespace `vlcrc` on clean exit.
  - Outputs: worker respawn with full argv including new flags; captions resume on new epoch (existing `session_id` machinery drops stale segments); detected backend surfaced back to GUI via `STATUS`→`whisper-backend-active` config mirror.
- Ownership/threading model:
  - Lua callbacks run cooperatively on VLC's UI thread — handlers stay O(small) (reads/writes + msg only). Audio callback (`vw_audio_capture.c`) never touched. Plugin sender loop owns polling (5 ms cadence already bounded in `vw_plugin_sender_main`) and respawn orchestration; worker owns engine reinit for model/backend vs per-call params for language/threads (future live path).
- Bounds, time units, and failure behavior:
  - Config polling bounded: compare 5 keys every ~2 s (tick counter). Respawn is gated by `atomic_bool respawn_in_progress` to avoid storms.
  - Thread count clamped `1..16` in both GUI (`tonumber` floor) and worker config (reject/clamp); language validated `1..15` chars, `auto` rejected.
  - Missing model file: dropdown selection writes a path pointing at a file not yet on disk; worker init then emits `E_MODEL_MISSING` and captions disable while media playback continues (existing UX) until 19c provisions the model.
  - Backend resolution truth: compile-flag `VW_HAVE_VULKAN` via `worker/CMakeLists.txt` plus configured `backend` value; resolved = `cpu` when `backend==cpu` or not Vulkan-compiled, else `gpu`.
- Privacy/security implications:
  - No unsolicited network at playback time; no new listener opened; model/backend changes never auto-download.
- Protocol change: compatible minor bump (v1.3, same major). Decoder tolerant of legacy 44 B STATUS (zero-fills tail); new encoder always writes 60 B. No capability flag needed — both peers ship together.

## Acceptance criteria
- [ ] Lua dialog shows Engine/Model/Language/Threads + Detected-backend label; Apply writes 4 keys and logs `[VLC-Whisper] applied …` lines; dialog preselects current values on open.
- [ ] Changing any of the four settings triggers a worker respawn within ~2 s (plugin log `PLUGIN_RESPAWN`, worker restart, new `session_id`); captions resume after brief gap. Backend `auto→gpu` on non-Vulkan build resolves to `cpu` shown in `whisper-backend-active` after first `STATUS`.
- [ ] Language `ro` with bundled `tiny.en` English-only model does NOT translate output — worker clamps/falls back to `en` and emits `WARN`; expected until a multilingual model is provisioned.
- [ ] Installer bundles the Lua file under `$INSTDIR\lua\extensions\`; uninstall removes it; CPack zip includes `lua/`.
- [ ] `STATUS` v1.3 survives round-trip: new peer encodes/decodes `resolved_backend` correctly; old peer's 44 B frame decodes on new code with empty backend.

## Test plan
Exact commands, fixtures, target OS/VLC build, and manual verification steps.

- Automated:
  ```bash
  cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug -j4
  ctest --preset linux-x64-debug --output-on-failure
  cmake --preset windows-x64-debug-cpu && cmake --build --preset windows-x64-debug-cpu -j4
  luac -p lua/extensions/vlc_whisper_settings.lua
  ```
  New/updated suites: `test_protocol_codec` (60 B golden + legacy decode), `test_worker_config` (language auto-reject, threads clamp, long-string reject).
- Manual (Windows, pinned VLC 3.0.23 64-bit):
  1. Build `windows-x64-release` + `installer` target, install or manual copy `libvlc_whisper_plugin.dll` + `vlc-whisper-worker.exe` + `lua\extensions\vlc_whisper_settings.lua` + `vlc-cache-gen`.
  2. `Tools → Messages` verbosity 2, play media, `View → VLC-Whisper Settings`: verify 4 controls + pending→resolved backend label.
  3. `en → ro` Apply → `PLUGIN_RESPAWN` within ~2 s; captions continue; tiny.en stays English (WARN logged).
  4. `auto → gpu` on CPU-only build → resolves `cpu` in label.
  5. Reopen dialog — last values persisted (re-read via `vlc.config.get`).
  - What NOT to expect: no `auto` language entry; no in-GUI model download (write path + `E_MODEL_MISSING` on absent file); no translation.

## Definition of done
- [x] C17 code; no project-authored C++ introduced
- [x] No blocking work in VLC audio callback
- [x] No unapproved network access, telemetry, transcript/PCM persistence, or sensitive logs introduced
- [x] Memory, audio queue, frame, text, and retry limits are bounded
- [x] Error path is safe: captions may stop, playback does not
- [x] Unit/contract/integration tests pass as applicable
- [x] Formatting, warnings-as-errors, and static checks pass
- [x] Protocol contract and compatibility version updated if needed
- [x] `docs/decisions.md`, roadmap, and AI context updated when assumptions change
- [x] Reviewer can reproduce the result from a clean checkout

## Evidence
- Build/test outputs or CI links: `cmake --preset linux-x64-debug && cmake --build -j4` 0 errors; `ctest 20/20 100%`; `windows-x64-debug-cpu` 0 link errors (after adding `config_GetInt`/`config_PutPsz`/`config_FindConfig` exports); `luac -p` OK.
- Measured performance (if relevant): respawn poll cadence ~2 s (tick counter at `vw_plugin_sender_main` 5 ms loop); caption gap ≈ worker restart time (existing epoch machinery, same as seek handling).
- Known limitations/follow-ups: all four settings currently apply via respawn (live per-call apply for language/threads without restart is a future optimization — no behavior difference except gap); auto-detect language deliberately omitted; model provisioning remains bundled-only in this step (installer/CPack include whatever `*.bin` + `*.json` exist under `models/` at build time; selecting a not-yet-bundled label writes the path and worker respawn surfaces `E_MODEL_MISSING` until the file is provisioned manually — see `cmake/vw_provision_model.cmake` reuse). Only **one dialog per Lua extension** is supported (`vlc.dialog` singleton; extension `activate` builds a single `dlg`; verified against `share/lua/README.txt` `vlc.dialog("Title")` pattern and existing extensions' single-global-dlg idiom), so any future on-demand download UI must be managed **inside this same dialog** and — to preserve the non-blocking GUI guarantee from `step19a_research_dossier.md` Q1 / `step19a_lua_route_feasibility.md` — the fetch **must execute in the worker process, not Lua**: Lua writes a control var (e.g. `whisper-model-download`), the plugin forwards it to the worker, which performs the sha256-pinned download and mirrors progress back through a config var the dialog polls. An `os.execute` or in-Lua HTTP fetch would block VLC's UI thread.
