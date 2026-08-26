# Task: Implement Step 19c — Lazy Model Provisioning & Download UX (Option B: Worker Runtime Download)

## Goal
One user-visible outcome: the installer bundles the universal multilingual `ggml-tiny.bin`; selecting any other catalog model (tiny.en, base.en, base, small, medium, large) in the VLC-Whisper settings extension offers an explicit **Download selected model** action; the worker downloads it (sha256-pinned, per-user directory), the plugin shows live dedicated-SPU progress over the active video, and captions resume on the new model once the download completes — playback never interrupted, transport-recovery budget untouched.

## Context
- Relevant docs/ADR:
  - `docs/plans/model_provisioning_ux_research.md` — design basis (bundle choice, Option B, per-user dir, verified real-time GUI surfaces).
  - `docs/decisions.md` ADR-022 (Lua GUI host); this step adds **ADR-023** (user-initiated worker-only network carve-out). Roadmap step 21a's ADR renumbers 023→024.
  - `AGENTS.md` Rule 5 (privacy) — carve-out documented in ADR-023; Rule 4 (audio-callback realtime safety) untouched.
- VLC/worker/protocol version affected:
  - Protocol v1.3 → **v1.4** (compatible minor): two new message types. `VW_CLIENT_VERSION`/`VW_WORKER_VERSION` 1.3.0 → 1.4.0.
- Assumptions and explicit non-goals:
  - The plugin itself performs **zero network I/O**; only the worker process egresses, and only on an explicit user action relayed over the existing authenticated local IPC.
  - Resume/Range downloads are out of scope (v1 accepts full re-download; largest model ≈ 3.1 GB).
  - No second Lua dialog (VLC allows one per extension); all progress renders inside the existing settings dialog.
  - GUI "auto" language entry stays out (step 20); worker CLI accepts `--language auto` as the prerequisite.
  - MS Store VLC installs are supported because models go to a per-user directory (Program Files/WindowsApps never written).

## Scope
- In scope:
  - Protocol v1.4: `VW_MSG_MODEL_CTRL` (14, plugin→worker) and `VW_MSG_MODEL_PROGRESS` (15, worker→plugin) with codec encode/decode/validate and golden-byte tests.
  - Worker: streaming SHA-256 (`vw_sha256`), committed model catalog header (`vw_model_catalog.h`), download engine (`vw_model_download`: dedicated thread, WinHTTP on Windows / `curl` subprocess on Linux, `.part` → verify → atomic rename into the per-user dir, single-flight, abort), `--model-dir` CLI, `--language auto` acceptance, MODEL_CTRL handling + 1 Hz MODEL_PROGRESS emission.
  - Plugin: `whisper-model-download` control var (6th snapshot key), `whisper-model-progress`/`whisper-model-status` read-only mirrors, MODEL_CTRL send + MODEL_PROGRESS drain, auto-respawn on `done` when the downloaded model is the selected one, model resolve probe extended (catalog names incl. `ggml-tiny.bin`, then per-user dir).
  - Lua extension: `Download selected model` / `Abort model download` menu entries, command-only callbacks with no polling or busy wait, default model selection → bundled `tiny`; the plugin sender renders progress on a dedicated SPU overlay.
  - Bundle switch: `models/manifest.json` full catalog (7 models, pinned sha256), provision/installer defaults → `ggml-tiny.bin`.
  - Docs: ADR-023, api-contracts v1.4, roadmap (19c shipped; 21a ADR renumber), source-layout, README.
- Out of scope:
  - Resume/Range downloads; translation (21a/b); GUI auto-language entry (20); installer-time model checkboxes (T1 of research §6); MS Store-specific packaging changes.
- Files/components expected to change:
  - `protocol/include/vw_protocol_types.h`, `protocol/src/vw_protocol_codec.c`, `tests/unit/test_protocol_codec.c`
  - `worker/include/vw_sha256.h`, `worker/src/vw_sha256.c`, `worker/include/vw_model_catalog.h`, `worker/include/vw_model_download.h`, `worker/src/vw_model_download.c`, `worker/src/vw_worker.c`, `worker/src/vw_worker_config.c`, `worker/src/main.c`, `worker/include/vw_worker_config.h`, `worker/CMakeLists.txt`, `tests/unit/test_model_download.c`, `tests/unit/test_worker_config.c`, `tests/unit/CMakeLists.txt`
  - `plugin/src/vw_whisper_module.c`, `plugin/libvlccore.def`
  - `lua/extensions/vlc_whisper_settings.lua`
  - `models/manifest.json`, `cmake/vw_provision_model.cmake`, `cmake/vw_installer.nsi.in`
  - `docs/decisions.md`, `docs/api-contracts.md`, `docs/roadmap.md`, `docs/source-layout.md`, `README.md`

## Design

### Wire contract (pinned — all layers build against this)
- `VW_MSG_MODEL_CTRL = 14` (plugin → worker). Payload 49 bytes:
  `vw_session_id_t session_id` (16, zeros when no session) · `uint8_t action` (`VW_MODEL_ACTION_DOWNLOAD = 1`, `VW_MODEL_ACTION_ABORT = 2`) · `char model_id[32]` (NUL-padded catalog id: `tiny.en|tiny|base.en|base|small|medium|large`; `abort` action ignores id).
- `VW_MSG_MODEL_PROGRESS = 15` (worker → plugin). Payload 66 bytes:
  `vw_session_id_t session_id` (16) · `uint8_t stage` (`VW_MODEL_STAGE_IDLE=0, DOWNLOADING=1, VERIFYING=2, DONE=3, FAILED=4, ABORTING=5`) · `uint8_t pct` (0–100) · `uint64_t bytes_done` · `uint64_t bytes_total` · `char model_id[32]`.
- Codec: exact-size validate (49/66), encode/decode symmetric, NUL-padding rules same as `resolved_backend`. Unknown types still rejected. Minor bump only — no capability flag (peers ship together, precedent: STATUS v1.3).

### Catalog (pinned — authoritative sha256 from Hugging Face LFS oids, 2026-08-25)
Base URL `https://huggingface.co/ggerganov/whisper.cpp/resolve/main/`:

| id | filename | sha256 | bytes | multilingual |
|---|---|---|---|---|
| tiny.en | ggml-tiny.en.bin | `921e4cf8686fdd993dcd081a5da5b6c365bfde1162e72b08d75ac75289920b1f` | 77,704,715 | false |
| tiny | ggml-tiny.bin | `be07e048e1e599ad46341c8d2a135645097a538221678b7acdd1b1919c6e1b21` | 77,691,713 | true |
| base.en | ggml-base.en.bin | `a03779c86df3323075f5e796cb2ce5029f00ec8869eee3fdfb897afe36c6d002` | 147,964,211 | false |
| base | ggml-base.bin | `60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe` | 147,951,465 | true |
| small | ggml-small.bin | `1be3a9b2063867b937e64e2ec7483364a79917e157fa98c5d94b5c1fffea987b` | 487,601,967 | true |
| medium | ggml-medium.bin | `6c14d5adee5f86394037b4e4e8b59f1673b6cee10e3cf0b11bbdbee79c156208` | 1,533,763,059 | true |
| large | ggml-large-v3.bin | `64d182b440b98d5203c4f9bd541544d84c605196c4f7b845dfa11fb23594d1e2` | 3,095,033,483 | true |

`worker/include/vw_model_catalog.h` is a **committed** header (static const array; no build-time JSON parsing — boring, reviewable). `models/manifest.json` mirrors it for docs/installer/scripts; both files cross-reference each other in comments.

### Storage & integrity
- Per-user model dir: Windows `%LOCALAPPDATA%\vlc-whisper\models`; Linux `${XDG_DATA_HOME:-$HOME/.local/share}/vlc-whisper/models`. Worker CLI `--model-dir <path>` overrides. Created on demand; stale `*.part` deleted at worker start.
- Download: `<dest>/<filename>.part` → stream SHA-256 while writing → compare catalog hash → atomic rename (`rename()` POSIX; `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` Windows). Hash mismatch → delete `.part`, retry once, then `failed`.
- Engine: dedicated thread, single-flight (second DOWNLOAD while active → immediate `failed` progress with `stage=DOWNLOADING` semantics replaced by error status; GUI prevents this anyway). Progress snapshot under mutex; poller reads it. Abort: flag + join; Windows `WinHttpCloseHandle`, Linux `kill(curl child)`.

### Ownership/threading model
- Worker: IPC main loop owns all socket writes. Download thread never writes IPC; it updates a mutex-guarded progress snapshot. Main loop polls at its existing cadence and emits `MODEL_PROGRESS` at ≥1 Hz while a download is active (stage transitions always emitted immediately).
- Plugin: 2 s snapshot loop gains a 6th watched key `whisper-model-download` (string: catalog id, or `abort`). Diff → send `MODEL_CTRL`. Receiver drains `MODEL_PROGRESS` → `config_PutInt("whisper-model-progress")` + `config_PutPsz("whisper-model-status", "<stage>:<model_id>")` (config lock already proven for cross-thread writes). On `stage=DONE`: if catalog filename == resolved selected model filename, trigger one config-style respawn (no budget) so captions resume on the new model.
- Lua: menu entry writes `whisper-model-download`, updates the label once, and returns. The plugin sender and worker
  own progress transport and rendering; no `os.clock`, `dlg:update`, sleep, polling loop, or network call exists in
  Lua. `menu()` returns three entries from the single dialog extension.

### Bounds, time units, and failure behavior
- Download: single-flight; `.part` size capped by catalog `bytes_total` (abort if exceeded); progress `pct = bytes_done * 100 / bytes_total` saturating. Verify stage has no time bound (sha256 of 3 GB ≈ seconds).
- Failure: `stage=FAILED` mirrored to `whisper-model-status`; captions stay off (`E_MODEL_MISSING` semantics on next respawn); playback unaffected. Abort: `stage=ABORTING` → thread joined → `IDLE`.
- Offline/restricted network: `FAILED` after WinHTTP/curl error; bundled `tiny` keeps working.

### Privacy/security implications
- ADR-023: network egress ONLY inside worker, ONLY on explicit user action relayed via authenticated local IPC, ONLY to pinned catalog URLs, ONLY with sha256 verification; never automatic, never at playback start; no telemetry; plugin stays network-free; transcripts/PCM never persisted (unchanged).

### Protocol change: compatible minor (v1.3 → v1.4)

## Acceptance criteria
- [ ] `MODEL_CTRL`/`MODEL_PROGRESS` round-trip: golden bytes fixed in tests; wrong-size payloads rejected; v1.3 peers reject unknown type 14/15 gracefully (validation error, not crash).
- [ ] Selecting `small` + menu `Download selected model` downloads to the per-user dir, status label shows live %, hash verified, file renamed atomically; `whisper-model-status` ends `done:small`; captions respawn on the new model without touching the transport budget (`respawn_count` unchanged).
- [ ] `Abort model download` stops the transfer, removes `.part`, status returns `idle`.
- [ ] Hash mismatch (simulated in unit test) → `.part` deleted, retry once, then `failed`.
- [ ] Bundled default is `ggml-tiny.bin`: installer ships it, plugin resolves it when `model-path` empty, Lua preselects it, worker accepts `--language auto` (whisper auto-detect).
- [ ] Failure at any point preserves VLC playback (passthrough); no allocation/IPC in the audio callback (untouched code path).
- [ ] Docs: ADR-023, api-contracts v1.4 (both frames + per-user dir + carve-out pointer), roadmap 19c shipped + 21a renumbered to ADR-024, source-layout rows, README usage.

## Test plan
- Automated (worktree root):
  ```bash
  cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug -j4
  ctest --preset linux-x64-debug --output-on-failure
  ctest --test-dir build/linux-x64-debug -T memcheck
  cmake --preset windows-x64-debug-cpu && cmake --build --preset windows-x64-debug-cpu -j4
  clang-format --dry-run --Werror $(git diff --name-only origin/gemini/milestone-4-step-19b | grep -E '\.(c|h)$')
  luac -p lua/extensions/vlc_whisper_settings.lua
  ```
- New suites: `test_protocol_codec` (49/66 B goldens, roundtrip, size-rejection), `test_model_download` (SHA-256 NIST vectors: `""`→e3b0c442…b855, `"abc"`→ba7816bf…15ad; catalog lookup; pct math saturation; atomic-rename tmp cleanup; hash-mismatch retry-then-fail), `test_worker_config` (`--model-dir`, `--language auto` accepted, `--model-dir` length reject).
- Manual (Windows, pinned VLC 3.0.23): install → select `small` → `Tools ▸ Extensions ▸ VLC-Whisper Settings` → menu `Download selected model` → watch status % → captions respawn on `small`; `Abort` mid-download; airplane-mode → `failed` with bundled `tiny` still captioning.

## Definition of done
- [x] C17 code; no project-authored C++ introduced
- [x] No blocking work in VLC audio callback
- [x] Network access confined to worker, user-initiated, pinned URLs + sha256 (ADR-023); no telemetry, no transcript/PCM persistence
- [x] Memory, download, queue, frame, and retry limits are bounded
- [x] Error path is safe: captions may stop, playback does not
- [x] Unit/contract/integration tests pass as applicable
- [x] Formatting, warnings-as-errors, and static checks pass
- [x] Protocol contract and compatibility version updated (v1.4)
- [x] `docs/decisions.md`, roadmap, and AI context updated when assumptions change
- [x] Reviewer can reproduce the result from a clean checkout

## Evidence
- Build/test outputs: filled at gate (linux + windows presets, memcheck, luac).
- Known limitations/follow-ups: no resume/Range (large ≈ 3.1 GB re-downloads on failure); installer-time model checkboxes deferred; GUI auto-language entry deferred to step 20; MS Store packaging untouched.
