# Research: Model Provisioning & Download UX (Step 19c precursor)

**Date:** 2026-08-24 · **Branch:** `gemini/milestone-4-step-19b` (@ `36616bc`) · **Status:** research only, no code
**Inputs:** user decision — bundle one model now, lazy-download the rest; Lua extension supports **one dialog at a time**; PotPlayer shows a dedicated progress dialog, which we cannot replicate; candidate feedback surfaces: extension GUI status, OSD, SPU.

**Implementation amendment (2026-08-26):** This research predates the no-wait requirement and its Lua polling-loop
recommendation is superseded. The shipped route keeps the single Lua dialog as a command surface only: callbacks
write config and return. The worker thread downloads; the plugin sender renders progress on a dedicated C-managed,
wall-clock SPU channel that is separate from captions and survives pause/seek blanking. See
[`docs/plans/model_download_no_wait_plan.md`](model_download_no_wait_plan.md) and ADR-023.

---

## 1. Scope and questions

1. Which model to bundle as-is: English-only `ggml-tiny.en.bin` (current) vs universal `ggml-tiny.bin`?
2. Who performs lazy downloads of the other six models, and when does the user opt in?
3. How is download progress surfaced, given: single-dialog constraint, no progress-bar widget, and the offline/privacy invariant (`AGENTS.md` Rule 5)?
4. Can the extension GUI status update in real time? (verified against VLC 3.0.23 sources — §7)

---

## 2. Bundled model: `tiny.en` vs universal `tiny`

| | `ggml-tiny.en.bin` (current bundle) | `ggml-tiny.bin` (universal) |
|---|---|---|
| Disk size | 77,704,715 B (pinned in `models/manifest.json`) | ≈ 75 MiB (pin exact sha256 at adoption) |
| Languages | English only (`multilingual: false`) | 99 languages incl. en/ro/tr/de/fr/es |
| English accuracy | Slightly better (English-only head, no language-token overhead) | Marginally worse at tiny tier |
| Auto language-detect | Meaningless (19b deliberately omitted the entry) | Meaningful — reopens the `auto` dropdown entry |
| Extension dropdown coverage | ro/tr/de/fr/es entries select models that don't exist yet | All six dropdown languages work out of the box |
| Worker CLI | accepts `--language <code>` | 19b worker **rejects literal `auto`** (`vw_worker_config.c`) — needs a change to accept `auto` when the loaded model is multilingual |

**Recommendation: bundle universal `ggml-tiny.bin`** and keep `tiny.en` lazy-downloadable for users who want maximum English accuracy. Rationale: identical size class, one binary covers the entire shipped language dropdown plus future auto-detect, and the multilingual gap is the single most common "why are there no captions" trap for non-English users. The accuracy delta at tiny tier is negligible; quality-motivated users will lazy-download `base`/`small` anyway.

**Adoption checklist (when implemented, not in this research):** `models/manifest.json` entry + `default_model_id`; `cmake/vw_provision_model.cmake` `MODEL_URL`/`MODEL_SHA256` defaults; installer `File` line (`cmake/vw_installer.nsi.in:127`) + uninstall `Delete` line (:191); plugin probe list `vw_plugin_resolve_model_path` (`plugin/src/vw_whisper_module.c:216` — hardcodes `{"ggml-tiny.en.bin", "models/ggml-tiny.en.bin"}`); extension default model id (currently `1 = tiny.en`); worker `--language auto` acceptance gated on `model.is_multilingual`.

---

## 3. Prerequisite findings (verified against current plumbing)

Code-level facts checked before designing the download flow:

1. **`model-path` / `worker-path` registration is intact — no fix needed.** Both are registered via `add_loadfile` (`plugin/src/vw_whisper_module.c:1076` worker-path, `:1080` model-path); in VLC 3.0 `add_loadfile` is a string-backed config item, so Lua `vlc.config.set`, `config_GetPsz` in the 2 s snapshot loop (:433-434), the snapshot diff, and the respawn path all work for model changes. New progress-mirror vars (`whisper-model-progress`, `whisper-model-status`, §8) follow the same pattern as the existing read-only `whisper-backend-active` mirror (:1087).
2. **Worker has no HTTP client.** `grep -rl "download|curl|winhttp|URLDownload" worker/src plugin/src` → no matches. The only network code in the repo is CMake build-time (`cmake/vw_provision_model.cmake` `file(DOWNLOAD ...)`, sha256-pinned) and the standalone helper scripts `models/vw_download_vad_model.{cmd,sh}`.
3. **Model path resolution probes only bundled names** (`vw_plugin_resolve_model_path`, :214-240 — `{"ggml-tiny.en.bin", "models/ggml-tiny.en.bin"}`). Switching the bundle to universal tiny requires a `ggml-tiny.bin` entry here, and a lazily downloaded model in a per-user directory (§6) will not be found until the probe list/order is extended (config `model-path` → install `models/` → per-user dir).
4. **Worker rejects `--language auto`** (19b contract). Bundling universal tiny makes `auto` meaningful again; worker must accept it when `whisper_full` model `is_multilingual()` (whisper.cpp auto-detects when language unset).

---

## 4. Who downloads — options

| Option | Mechanism | Pros | Cons |
|---|---|---|---|
| **A. Install-time** | NSIS/CMake fetches selected models during installation (current pattern, extended with per-model checkboxes on the components page; `/nonfatal` for optional models) | Zero runtime network code; admin rights available; invariant untouched | Requires network at install; user must anticipate needs; offline installer must skip gracefully |
| **B. Worker runtime download** | Lua writes a control var → plugin forwards a protocol frame → worker downloads (own thread), verifies sha256, mirrors progress back | PotPlayer-parity UX; user picks any model any time; single code path reuses `models/manifest.json` URLs+hashes | Needs HTTP client in worker; needs an explicit privacy carve-out (§5); write-location ACL work (§6) |
| **C. Helper scripts** (exists for VAD) | User runs `vw_download_vad_model.{cmd,sh}`-style script manually | Zero network in shipped binaries | Poor UX; breaks parity goal; discoverability ~zero |

**Recommendation: B, with A retained as the offline fallback.** The installer keeps shipping the bundled default (works with zero network forever); runtime download is strictly **user-initiated, one model at a time, sha256-pinned, from the pinned Hugging Face URL in `models/manifest.json`**. Helper scripts stay for air-gapped users.

**Privacy invariant handling:** `AGENTS.md` Rule 5 says "zero network requests". A user-initiated model fetch is not telemetry/cloud transcription, but the rule as written forbids it. This requires a new ADR (decisions.md) carving out: *network egress occurs only inside the worker process, only on explicit user action in the settings GUI, only to the pinned model URL, only with sha256 verification; never automatic, never at playback start, never for any other host*. The plugin itself remains network-free (it only relays control/progress frames over the existing authenticated local IPC).

**Worker HTTP client:** Windows → WinHTTP (system library, no new link dependency, supports progress callbacks + cancellation). Linux → fork `curl -fL -o <tmp>` as a subprocess (zero new link dependency; curl is near-universal) or link libcurl if a dependency is acceptable. Either way the download must run on a **dedicated worker thread**, never the IPC loop (single-listener constraint; the connection must keep answering STATUS/KEEPALIVE during a multi-minute download).

---

## 5. Where downloaded models live

- `$INSTDIR\models\` (Program Files) requires admin to write → **not writable at runtime** for standard users, and **read-only entirely** for MS Store VLC installs (`WindowsApps`).
- **Recommendation: per-user model directory**, e.g. `%LOCALAPPDATA%\vlc-whisper\models\` (Windows) / `~/.local/share/vlc-whisper/models/` (Linux), created on demand.
- Resolution order becomes: configured `model-path` (if set) → install-dir `models/` (bundled default) → per-user dir (downloaded models). The plugin probe (`vw_plugin_resolve_model_path`) and the worker's own fallback must both learn the third location; the worker should also accept an explicit absolute `--model` from the plugin argv (already forwarded today).
- Atomicity: download to `<name>.part`, verify sha256 against manifest, `rename()` into place. On worker start, delete stale `*.part` (covers VLC killed mid-download — the plugin tears down the worker on exit, which aborts the transfer).

---

## 6. When and how the user chooses to download

Three trigger points, all opt-in; none automatic:

- **T1 — Install time (Option A):** NSIS components page offers optional model checkboxes. Best for predictable offline installs; no runtime code involved.
- **T2 — Settings GUI (primary, Option B):**
  - Model dropdown labels annotate availability: `small (not installed — 466 MiB)` vs `tiny (bundled)`. Availability is known locally: the extension already resolves the current `model-path`; a per-user-dir existence check can be surfaced by the plugin through a config mirror (e.g. `whisper-model-status`) rather than the extension probing the filesystem (Lua `vlc.io` could stat, but keeping FS knowledge plugin-side avoids duplicating the probe order).
  - Selecting a missing model + `Apply` → status row shows `Model missing — open menu → Download model`.
  - `menu()` already returns a list; add a second entry `Download selected model` (and `Abort download` while active). This works with the dialog **closed**: `trigger_menu` opens the single dialog (constraint §7.1) and the download progress renders inside it.
- **T3 — Playback failure (passive):** worker emits the existing `E_MODEL_MISSING`; plugin mirrors it into `whisper-model-status`; one-shot OSD message `VLC-Whisper: model <name> not found — Tools ▸ Extensions ▸ VLC-Whisper Settings` while playback continues unaffected (existing UX contract). OSD requires an active vout — true whenever the user is watching; no captions are lost that weren't already lost.
- **First run:** nothing to choose — bundled universal tiny works offline immediately.

---

## 7. Real-time status: what VLC 3.0.23 actually supports (verified)

Sources: `modules/lua/extension.c`, `modules/lua/libs/dialog.c`, `modules/lua/libs/net.c`, `share/lua/README.txt` @ vlc-3.0 branch; context7 `/videolan/vlc`.

### 7.1 Single dialog (hard constraint)
`vlclua_dialog_create` (dialog.c): *"Only one dialog allowed per extension!"* — a second `vlc.dialog()` call errors. All progress UI must live inside the existing settings dialog; there is no second-window escape hatch. The `menu()` path can trigger actions without the dialog, but any visual progress needs the one dialog open.

### 7.2 Widget set (no progress bar)
`dialog.c` registers: `add_button, add_label, add_html, add_text_input, add_password, add_check_box, add_dropdown, add_list, add_image, add_spin_icon`. **There is no progress-bar widget.** Progress is rendered as label text (`w:set_text("47% — 35/75 MiB")`) plus optionally `add_spin_icon` (`w:animate()`/`w:stop()`) as a busy indicator.

### 7.3 Real-time updates: Lua polling rejected by the no-wait requirement
Although VLC exposes `d:update()` and `w:set_text`, the implementation must not hold a Lua callback open or emulate
a timer with `os.clock()`. The callback writes the command and returns; progress is pushed through the existing
worker/plugin IPC path and rendered by the C presenter. This keeps the one-dialog constraint without any Lua busy
wait or UI-thread polling.

### 7.4 OSD: YES (with a vout caveat)
Extensions get `luaopen_osd` (extension.c:845): `osd.channel_register()`, `osd.message(string, [id], [position], [duration µs])`, `osd.slider(position 0–100, "horizontal"|"vertical", [id])`. An OSD slider updated from the same loop gives PotPlayer-like progress **over the video**, independent of the dialog. Caveat: OSD renders only when a video output exists (audio-only playback shows nothing) — fine, since downloads are user-initiated from the GUI anyway.

### 7.5 Dedicated SPU progress: accepted; caption channel remains separate
Rendering progress through the existing caption channel would collide with real captions and pollute the transcript
surface. The implementation therefore registers a separate wall-clock SPU channel from `vw_caption_presenter.c`.
`vw_caption_presenter_blank()` flushes only caption/OSD channels, preserving progress while video is paused or sought;
explicit abort, worker disconnect, and teardown flush the dedicated channel.

### 7.6 Runtime Lua environment (what the extension may call)
At **activation** the extension state gets `luaL_openlibs` + `config, dialog, input, object, osd, playlist, stream, strings, variables, video, vlm, volume, xml, io, errno` and — via `vlclua_fd_init` — `vlc.net` (`connect_tcp/send/recv/poll`; `net.read/write` are POSIX-only) (extension.c:810-858; net.c:486-513). So the extension *could* do HTTP itself — **but must not**: an in-Lua fetch blocks the extension thread for the whole transfer and duplicates logic the worker must have anyway (hash verify, per-user dir, resume). The 19b plan already ruled this out ("in-Lua HTTP fetch would block VLC's UI thread"); the division of labor stays: **Lua = UI only, worker = network + FS.**
Reminder from the scan-state incident (commits `0993112`/`36616bc`): the *scan* pass runs in a bare `luaL_newstate` with **no standard libraries** — top-level code must stay library-free regardless of what §7.6 allows at runtime.

---

## 8. Control/progress handshake (design sketch, no code)

Reuses the 19b live-apply pattern end to end; protocol change is a compatible minor bump (v1.4):

1. **Config vars** (registered like the existing `whisper-*` items; read-only mirrors follow the `whisper-backend-active` precedent): `whisper-model-download` (string: model id to fetch, or `abort`), `whisper-model-progress` (int 0–100, read-only mirror), `whisper-model-status` (string: `idle|downloading|verifying|done|failed|missing|aborting`, read-only mirror).
2. **Plugin** (2 s snapshot loop already exists): detects `whisper-model-download` diff → sends new `MODEL_CTRL` frame to worker (same authenticated IPC, single listener). Drains `MODEL_PROGRESS` frames → `config_PutInt`/`PutPsz` mirrors (VLC config lock already proven safe for cross-thread writes in 19b's `whisper-backend-active`).
3. **Worker**: dedicated download thread; WinHTTP/subprocess-curl to the manifest URL into `<per-user dir>/<name>.part`; sha256 verify; atomic rename; progress frames at ~1 Hz (pct + bytes); honors `abort`; never blocks the IPC loop; deletes stale `.part` at startup.
4. **Extension**: `Download` / `Abort` menu entries write the control variable, update the current label once, and
   return. No filesystem access, network, timer, polling loop, sleep, or busy wait exists in Lua. The plugin sender
   owns progress rendering and the worker owns transfer cancellation.
5. **Failure surfaces**: offline/restricted network (WDAG) → `failed` + OSD + status row; bundled default unaffected. Disk-full pre-check against manifest `disk_bytes`. Hash mismatch → delete `.part`, retry once, then `failed`. Single-flight: a second request while downloading is rejected in the GUI (dropdown disabled while `downloading`). Resume/Range: deferred (largest model ≈ 2.9 GB argues for it eventually; v1 accepts re-download).

---

## 9. Recommendation summary

| Question | Recommendation |
|---|---|
| Bundle which model | Universal `ggml-tiny.bin`; keep `tiny.en` lazy-downloadable |
| Download executor | Worker process, dedicated thread (WinHTTP / subprocess curl), sha256-pinned from `models/manifest.json` |
| Invariant handling | New ADR: user-initiated-only network carve-out; plugin stays network-free |
| Write location | Per-user model dir; probe order config → install dir → user dir |
| User trigger points | Installer checkboxes (optional) · settings dropdown annotation + menu entry (primary) · `E_MODEL_MISSING` OSD (passive) |
| Progress UI | Dedicated C-managed wall-clock SPU channel over the video; the existing Lua label is command feedback only; separate from the caption SPU channel |
| Prerequisites to fix first | Probe-list entries (`ggml-tiny.bin` bundle + per-user dir) in `vw_plugin_resolve_model_path`; worker `--language auto` for multilingual; worker HTTP client + download thread |

## 10. Open questions

1. Resume support for `large` (2.9 GB) in v1, or accept full re-download? (Range requests are straightforward in WinHTTP; subprocess curl has `-C -`.)
2. Should the installer's components page offer optional models (T1), or is install-time network to be avoided entirely in favor of the runtime flow?
3. Per-user dir naming/location — `%LOCALAPPDATA%\vlc-whisper\models` vs `%APPDATA%\vlc\models` (the latter rides VLC's own data dir but couples our state to VLC's).
4. Does `whisper-model-status` belong in `STATUS` frames (tail-field growth, v1.3→v1.4 compat already planned) or a dedicated frame? Dedicated frame keeps STATUS at 60 B for all consumers.
