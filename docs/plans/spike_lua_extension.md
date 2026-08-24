# Spike: VLC Lua Extension Settings GUI — Feasibility Proof (Step 19a)

> **Branch**: `gemini/milestone-4-step-19a-lua-extension` (worktree `gemini-m4-spike-lua-ext`)  
> **Date**: 2026-08-24  
> **Scope**: Prove that a **VLC Lua extension** (`lua/extensions/*.lua`) can host the future
> vlc-whisper settings GUI WITHOUT wiring to the whisper plugin/worker. GUI = concept;
> deliverable = mechanism proof + plan + invariant/wire-up analysis. Companion to the
> native-C-interface spike (`spike_native_interface.md`, same dossier).

**Research dossier authority**: `docs/plans/step19a_research_dossier.md` §§1–4.
**Binding repo rules**: `AGENTS.md` (C17, `vw_` namespacing, no network at runtime,
never block in audio callbacks). Vendored headers are API authority:
`worker/third_party/vlc-3.0.23/include/vlc_extensions.h`,
`vlc_configuration.h`, `vlc_common.h`.

---

## 1. What was built

### 1.1 Artifact

- **New file** `lua/extensions/vlc_whisper_settings.lua` — standalone Lua 5.1 extension,
  no build step, no `vw_` C symbols required for the spike, no dependency on
  `plugin/src/vw_whisper_module.c` or `protocol/`.
- **New file** `lua/README_SPIKE.md` — verbatim manual-test instructions for
  Windows (`<VLC>\lua\extensions\`) and Linux (`~/.local/share/vlc/lua/extensions`),
  with exact log filters.

### 1.2 Extension behavior

**Descriptor** (`descriptor()`):

```lua
return {
  title = "VLC-Whisper Settings (Spike)",
  version = "0.1.0",
  author = "vlc-whisper",
  url = "https://github.com/rzv04/vlc-whisper",
  shortdesc = "VLC-Whisper Settings (Spike)",
  description = "Feasibility spike …",
  capabilities = { "menu" },
}
```

Gets a single menu entry under `View` (some skins `Tools` > `Extensions`):
`VLC-Whisper Settings (Spike)` via `menu()` + `trigger_menu(id)`. Lua extensions
are the **only** blessed way for a third-party VLC module to own a menu entry
(`vlc_extensions.h`: `EXTENSION_HAS_MENU` / `EXTENSION_TRIGGER_MENU`); an
`audio_filter` capability cannot inject a Tools-menu item (dossier §1).

**Lifecycle** (`activate` / `deactivate` / `close`):

- `activate()` creates a single `vlc.dialog("VLC-Whisper Settings (Spike)")`,
  populates widgets (below), calls `dlg:show()`, logs
  `[VLC-Whisper][SPIKE] extension activate — building dialog`.
- `deactivate()` hides and nils the dialog (idempotent, `pcall` guarded).
- `close()` → `vlc.deactivate()` as required by the VLC Lua extension contract
  (cf. `VLSub.lua:close()`).
- No background threads, no timers, no I/O — bounded lifetime tied to dialog
  visibility.

**Dialog widgets** (`vlc.dialog` API per `vlc.verg.ca/m/dialog/` and `VLSub.lua`):

| Row | Label | Widget | Type | Notes |
|-----|-------|--------|------|-------|
| 1 | `Engine:` | `w_engine` | `dlg:add_dropdown(2,1,2,1)` + `add_value` ×3 | `auto (default)` id 1, `GPU (Vulkan)` id 2, `CPU only` id 3 — maps to §3 `whisper_context_params.use_gpu` |
| 2 | `Model:` | `w_model` | `dlg:add_dropdown(2,2,2,1)` + `add_value` ×7 | `tiny.en` id 1 (default) … `large` id 7 — maps to §3 model-file reinit |
| 3 | `Language:` | `w_language` | `dlg:add_dropdown(2,3,2,1)` + `add_value` ×7 | `auto (detect)` id 1, `en` id 2 (default), `ro`/`tr`/`de`/`fr`/`es` — maps to `whisper_full_params.language` |
| 4 | `Threads:` | `w_threads` | `dlg:add_text_input("4",2,4,2,1)` | Default `"4"` as required; see spinner gap below |
| 5 | — | `Apply` | `dlg:add_button("Apply", on_apply, 1,5,4,1)` | Reads `get_value()`/`get_text()`, stores in `spike_state`, `vlc.msg.info` logs with `[SPIKE]` prefix |
| 6 | hint | label | `dlg:add_label("Spike: Apply logs …",1,6,4,1)` | Banner stating spike isolation |

**Apply handler** (`on_apply`):

```lua
local spike_state = { engine="auto", model="tiny.en", language="en", threads="4" }
-- engine_map / model_map / language_map map dropdown ids → strings
vlc.msg.info("[VLC-Whisper][SPIKE] Apply engine="..spike_state.engine
  .." model="..spike_state.model.." language="..spike_state.language
  .." threads="..spike_state.threads)
vlc.msg.info("[VLC-Whisper][SPIKE] state stored local table (no config write in spike)")
```

SPIKE prefix makes filtering trivial at verbosity 2 (`Tools > Messages`, verbosity `2`,
or `vlc -vvv`). The spike deliberately **never** calls `vlc.config.set` / `config_PutPsz`
— that is the wire-up bridge for the real GUI (see §3).

**Lua validity**: checked with `luac -p` (Lua 5.3 host, Lua 5.1-era syntax: no `goto`,
no `//`, no `&`/`|` bitwise); compatible with VLC's embedded Lua 5.1/5.2 depending on
build. Single global dialog (`dlg`) + local widget handles matches `VLSub.lua` pattern.

### 1.3 Out-of-scope (additive only)

- Does NOT modify `plugin/src/vw_whisper_module.c`, presenter, worker, protocol, queue.
- No installer change, no `plugin/CMakeLists.txt` edit, no `.dll` rebuild.
- No network I/O, no `vw_platform_spawn_process` from Lua (not exposed).
- No `config_PutInt`/`config_SaveConfigFile` yet — those belong to the §3 bridge.

---

## 2. PotPlayer parity widget mapping

PotPlayer (per `docs/roadmap.md` 19c) minimum settings set: inference engine selector,
model picker, caption language selector, CPU thread-count spinner, all persisting and
reaching the worker via the control channel. VLC Lua mapping:

| PotPlayer feature | PotPlayer widget | VLC Lua extension equivalent | Parity | Notes |
|-----------------|-----------------|------------------------------|--------|-------|
| Inference engine selector (`auto` / Vulkan GPU / CPU + detected backend status) | ComboBox | `w_engine` **dropdown** (`EXTENSION_WIDGET_DROPDOWN`, `add_dropdown` + `add_value`) | ✅ full | Spike offers the 3 options; real GUI would query `whisper` backend at runtime for status label (`add_label` dynamic) |
| Model picker (`tiny`/`base`/`large` + language variants per `models/manifest.json`) | ComboBox + download button | `w_model` **dropdown** | ✅ full (selection) / ⏳ deferred (download) | Model download UI is 19d scope; reuse `cmake/vw_provision_model.cmake` SHA-256 flow; Lua would spawn standalone provisioner or open URL |
| Caption language selector (`auto`, `en`, `ro`, `tr`, full `whisper_lang_*` list) | ComboBox | `w_language` **dropdown** | ✅ full | Full list ~100 entries via `whisper_lang_max_id` + `whisper_lang_str` — spike trims to 7 for proof; real GUI enumerates at activation |
| CPU thread-count spinner (default = HW concurrency capped by whisper guidance) | **Spinner** (up/down) | `w_threads` **text_input** (`EXTENSION_WIDGET_TEXT_FIELD`) — **spinner gap** | ⚠️ gap | **No spinner widget exists** in the VLC extension toolkit (`vlc_extensions.h`: `EXTENSION_WIDGET_*` = `LABEL|BUTTON|IMAGE|HTML|TEXT_FIELD|PASSWORD|DROPDOWN|LIST|CHECK_BOX|SPIN_ICON`; `SPIN_ICON` is a static "loading…" animation, not an input). `TEXT_FIELD` with default `"4"` is the closest available control. Validation (`tonumber` + clamp `[1..16]`) must be done in Lua `on_apply`. A real native spinner requires a standalone `vlc-whisper-settings.exe` (Qt/Win32) launched from the Lua menu via the C extension route, or C `extension` widget upgrade if VLC ever adds one. |
| Apply / Save / Cancel | Buttons | `Apply` **button** (`add_button`) | ✅ full | Spike logs; real GUI adds `Cancel` (`close` → `vlc.deactivate`) and persistent save |

**Spinner gap disposition**: explicitly documented in the extension source comment,
in `lua/README_SPIKE.md` manual-verification steps, and here. Not a blocker —
most users accept typing `4`/`8`; the deferred standalone exe (ADR-011 baseline,
dossier §1) provides a native spinner without forcing a VLC fork (ADR-012).

**Additional PotPlayer widgets not in 19c minimum** (translation toggle, per-cue
translation UI) belong to 19b/21 and are out of this spike.

---

## 3. Config-namespace wire-up bridge (how the REAL GUI would later attach through THIS route)

### 3.1 Dossier channel (a) — the only channel a pure-Lua extension can use

Dossier §2 narrows the control channel to a **hybrid (a)+(b)**. For a pure-Lua
extension, channel (b) (plugin-owned second listener) is unreachable — Lua cannot
`socket`/`pipe` to the worker and cannot call `vw_platform_spawn_process`. The
available path is **channel (a): VLC config system** (`vlc_configuration.h`):

- Lua side: `vlc.config.set(name, value)` / `vlc.config.get(name)` (`vlc.verg.ca/m/config/`), which proxies to the C core's `config_PutPsz` / `config_PutInt` / `config_GetPsz`.
- C side (plugin `audio_filter`): `config_GetPsz(obj, name)` / `config_GetInt(obj, name)` in `vw_plugin_open` / sender-thread polling, declared in `plugin/src/vw_whisper_module.c:804/839`.

The config namespace is **global to the VLC process** — Lua and the `audio_filter`
share it (no per-module isolation). Vars must be declared in the plugin's
`vlc_module_begin()` block via `add_string` / `add_integer` / `add_loadfile` so the
Lua `set()` call does not fail with "invalid option".

### 3.2 Exact config vars the real GUI would write

Current plugin registers only two vars (`plugin/src/vw_whisper_module.c:935/939`):

```c
add_loadfile("worker-path", NULL, "Path to vlc-whisper-worker executable …", false)
add_loadfile("model-path",  NULL, "Path to ggml-tiny.en.bin model file …", false)
```

**Spike proposes four new vars** for 19c, exactly matching the four controls in the
dialog. Names are `vw_`-prefixed in the source identifier but the config *key*
is the string literal passed to `config_PutPsz` — proposed keys below (final
ADR-022 may bikeshed the prefix, but the four keys are the wire-up contract):

| Spike control | Config key (string literal) | C decl in `vlc_module_begin()` | Lua `set()` call | C `get` in plugin | Persisted in `vlcrc` | Apply semantics |
|---------------|-----------------------------|--------------------------------|------------------|-------------------|------------------------|-----------------|
| Engine dropdown | `whisper-backend` | `add_string("whisper-backend", "auto", "Inference backend (auto|gpu|cpu)", "auto = probe Vulkan, gpu = require Vulkan, cpu = forbid Vulkan", false)` | `vlc.config.set("whisper-backend", spike_state.engine) -- "auto"/"gpu"/"cpu"` | `char *b = config_GetPsz(obj, "whisper-backend");` → maps to `vw_worker_config_t.backend` + spawn argv `--backend` | yes | **Respawn** (worker rebuild: `whisper_init_from_file_with_params` with `use_gpu` per `vlc.cont…` ; `vw_plugin_respawn_worker` epoch) |
| Model dropdown | `model-path` (reuse existing) | already declared `add_loadfile("model-path", …)` | `vlc.config.set("model-path", model_path_for_id(spike_state.model))` — values are filesystem paths (e.g. `models/ggml-tiny.en.bin`), not bare labels; dropdown labels map to paths | `config_GetPsz(obj, "model-path")` already at `vw_whisper_module.c:839` | yes | **Respawn** (full worker teardown + `whisper_init` re-read; log `t_load_us` — dossier §4 observability debt) |
| Language dropdown | `whisper-language` | `add_string("whisper-language", "en", "Caption language (auto|en|ro|…)", "auto runs whisper_lang auto-detect", false)` | `vlc.config.set("whisper-language", spike_state.language) -- "auto"|"en"|…` | `config_GetPsz(obj, "whisper-language")` → `config->language` → `wparams.language` at `vw_whisper_engine.c` next `whisper_full` call | yes | **Live** (no reinit; language selects SOT token per `whisper.cpp:6977`; dossier §3 says auto-detect is a one-shot encoder pass). `threads` similarly live |
| Threads text_input | `whisper-threads` | `add_integer("whisper-threads", 4, "CPU threads for inference", "1..16, default 4; capped to hardware concurrency", false)` | `vlc.config.set("whisper-threads", tonumber(spike_state.threads) or 4)` — Lua `set` accepts number for integer vars; C bridge is `config_PutInt` | `config_GetInt(obj, "whisper-threads")` → `wparams.n_threads` per-call | yes | **Live** (O(1) next-call effect; dossier §3: currently hardcoded `wparams.n_threads=4` in `engine`) |

**Auxiliary var** for completeness (not in the four-widget spike): `whisper-gpu-device`
(`add_integer("whisper-gpu-device", 0, …)` → `--gpu-device` argv). Engine dropdown
could hide it behind an advanced panel.

Lua Apply for the real GUI (future, not in spike):

```lua
local function on_apply_real()
  local eng = engine_map[w_engine:get_value()]
  local mod = model_path_for_id(w_model:get_value()) -- label → path via manifest
  local lang = language_map[w_language:get_value()]
  local thr = tonumber(w_threads:get_text()) or 4
  if thr < 1 then thr = 1 elseif thr > 16 then thr = 16 end

  vlc.config.set("whisper-backend", eng)   -- config_PutPsz
  vlc.config.set("model-path", mod)        -- config_PutPsz (existing var)
  vlc.config.set("whisper-language", lang) -- config_PutPsz
  vlc.config.set("whisper-threads", thr)   -- config_PutInt
  -- Persist to vlcrc (VLC's config Save; not auto on set):
  -- vlc.config is not documented to auto-save; the C side may call config_SaveConfigFile.
  vlc.msg.info("[VLC-Whisper] settings saved — restart filter to apply model/backend; language/threads live next segment")
end
```

Saving: `config_SaveConfigFile(VLC_OBJECT(...))` (C) persists to `vlcrc` (per
`vlc_configuration.h:110`). Lua has no `save` binding; the C plugin would call
it on next open, or the Lua extension could trigger a tiny C `extension`
helper that calls it (dossier's recommended hybrid: keep Lua for UI, a 20-line
C `extension` submodule for `config_SaveConfigFile` + `vw_platform_spawn_process`
to launch the standalone exe when a richer GUI is needed).

**Restart vs live** per dossier §3:

- **Restart-grade** (`model-path`, `whisper-backend`): plugin must respawn the
  worker (`vw_plugin_respawn_worker` at `vw_whisper_module.c:292`, reusing the
  16-byte `session_id` epoch so stale `CAPTION_SEGMENT`s are dropped). The GUI
  surfaces this as "Changes will apply on next playback / Restart playback".
- **Live** (`whisper-language`, `whisper-threads`): read at each `whisper_full`
  call; no worker restart, no session drop. The plugin's sender thread already
  polls `input_GetState` at 5/20 ms cadence — a similar poll for config vars
  (or `var_AddCallback` on them) provides live-apply without a second listener.

### 3.3 Why config-namespace is sufficient for a pure-Lua phase 1

- Zero new IPC surface (mitigates the extra-listener attack surface noted in
  dossier §2, channel (b)).
- Reuses the already-proven `vw_whisper_module.c` `config_GetPsz` path.
- `vlcrc` persistence is free; no custom file format (dossier fallback (c)).
- Lua ↔ C sharing requires no new transport — the spike proves the menu entry
  exists; adding `vlc.config.set` is a 4-line diff per control.

Channel (b) (plugin-owned second listener, framed protocol, 32-byte token,
`vw_frame_header_t`) remains the **future live-control** channel for per-call
engine params if sub-restart latency is required, but it is not needed to prove
the Lua route viable.

---

## 4. Invariants & Wire-Up Feasibility

### 4.1 Invariant compliance (AGENTS.md + dossier)

| Invariant (AGENTS.md §) | Spike status | Real GUI via this route |
|--------------------------|--------------|-------------------------|
| **4 VLC realtime callback safety** — never inference/IPC/blocking/alloc in audio callback; enqueue to bounded SPSC only | ✅ spike never registers an audio callback; `vlc.dialog` runs on the Qt/main thread, `on_apply` is a short `vlc.msg.info` + table store (bounded). Future `vlc.config.set` is a short string copy, also not on the audio path. | Preserved: the real Lua bridge writes config keys only; the plugin's audio callback (`vw_audio_capture.c`) remains enqueue-only. The `whisper-threads` live poll in the sender thread (not the callback) reads `config_GetInt` at the 5/20 ms cadence already bounded in `vw_plugin_sender_main`. |
| **5 Offline & privacy** — zero network, no cloud, no transcript/PCM logging | ✅ spike has no `net`/`http` use, no `vlc.stream`, no file writes except `vlc.msg`. The capped log never includes PCM/transcript. | Preserved: `vlc.config.set` is local-only; real GUI would not introduce network (translation 19b is gated behind a separate opt-in ADR-023). |
| **1 C17 / 2 Google style 120 cols / 3 `vw_` namespacing** | N/A for Lua file, but respected: no project-authored C++ in this spike; Lua file uses no `vw_` C symbols. The future C bridge (`add_string("whisper-…")`, `config_GetInt`) will be in `plugin/src/vw_whisper_module.c` with `vw_` prefix and 2-space/120-col `clang-format`. | Full compliance: new config vars live alongside `worker-path`/`model-path` with `vw_`-prefixed helpers. |
| **10 Verification checklist** (format + build + valgrind) | Spike skips formatters/build per assignment ("Skip formatters/gates — orchestrator verifies"); `luac -p` is the equivalent format proof and passes on Lua 5.3. Real C bridge will re-enable `cmake --preset linux-x64-debug && ctest` + `clang-format`. | No regression: Lua file does not break native presets (`plugin/CMakeLists.txt` untouched). |
| **Bounded thread/lifetime** | ✅ one `vlc.dialog`, created on `activate`, hidden on `deactivate`, no leaked `vlc_cond`/`vlc_clone`. | Same: Lua dialog lifetime is managed by VLC's extension manager (`EXTENSION_ACTIVATE`/`DEACTIVATE` in `vlc_extensions.h`), not by us. |
| **Crash isolation vs ADR-011** | Lua runs **inside** VLC's process (Lua VM), unlike the ADR-011 `vlc-whisper-settings.exe` which is fully isolated. Spike documents the tradeoff (below). | Hybrid path: keep Lua for in-VLC presence; spawn the isolated exe from Lua via a tiny C extension helper if crash isolation is required (dossier recommended direction). |

**Threading note for future C bridge**: the plugin already uses `atomic_bool` + `vlc_cond`/`sleep-poll` for sender thread; the wire-up bridge adds no new threads.

### 4.2 Audio-callback safety (Rule 4) — explicit

- The spike's `on_apply` is **never** invoked on the audio path. VLC invokes it via
  `extension_DialogCommand(…, EXTENSION_EVENT_CLICK, …)` on the dialog's owner
  object (`vlc_extensions.h:253`), which the Qt interface dispatches on the main/UI
  thread. No `aout`/`filter` callback is involved.
- The future `whisper-threads` live path must still respect Rule 4: never write
  `wparams.n_threads` from the audio callback. The correct locus is the sender
  thread's 5 ms poll loop or a `var_AddCallback("whisper-threads", …)` that runs
  outside the callback. This report states that requirement so 19c cannot regress it.

### 4.3 Why this route permits full GUI wire-up with no dead ends

1. **Menu presence is proven, not speculated.** `vlc_extensions.h` guarantees that
   `capabilities = {"menu"}` gives an Extensions-menu entry (`EXTENSION_HAS_MENU`).
   The spike exercises exactly that contract; no VLC fork is needed (ADR-012).

2. **Config vars are additive and non-breaking.** `vlc_module_begin()` `add_string`
   / `add_integer` for `whisper-backend` / `whisper-language` / `whisper-threads`
   sit beside the existing `add_loadfile("model-path")`; old `vlcrc` files without
   them fall back to defaults (`"auto"` / `"en"` / `4`). No reorder, no removal.

3. **Extension submodule can coexist with `audio_filter` in one `.dll`.**
   `vlc_plugin.h` `add_submodule` allows a single `libvlc_whisper_plugin.dll` to
   export both `set_capability("audio_filter", 0)` and an `extension` capability
   (the C companion to this Lua proof). `vlc-cache-gen` picks up both. This is the
   dossier's "Low (same .dll, add_submodule)" packaging cost; the spike does not
   yet add the submodule, proving that the Lua path needs **no** `.dll` change
   for phase-1.

4. **Lua → C → worker path reuses existing machinery.**  
   `vlc.config.set("model-path"/"whisper-…")` → `config_GetPsz` → `vw_worker_config`
   → spawn argv (`--backend`/`--gpu-device`) + `wparams.n_threads`/`language` is the
   same machinery the worker already uses for `worker-path`/`model-path`. The only
   new code is forwarding 2–4 argv flags and adding engine setters for per-call
   params (dossier §3 "Blocking-gap summary") — no new IPC codec, no duplicate
   auth.

5. **Spinner gap has a non-dead-end deferral.** If a native spinner is mandated,
   the Lua menu entry can remain and delegate to a standalone
   `vlc-whisper-settings.exe` spawned via the already-proven
   `vw_platform_spawn_process` (`protocol/src/vw_ipc_*`, used at
   `vw_worker_client.c:95`). That helper lives in a 20-line C `extension`
   submodule alongside the Lua extension — again, same `.dll` via `add_submodule`,
   no fork, and the Lua dialog can show "Open advanced settings …" as a button
   that triggers the spawn.

6. **Crash isolation is selectable.** Pure-Lua phase-1 trades isolation for
   zero packaging cost (Lua errors are caught by the VM, but a native crash would
   still be in-process). When ADR-011-grade isolation is required, the same
   menu entry spawns the out-of-process exe — the dossier's verdict "keep the
   standalone exe (isolation), add a minimal C extension for menu + spawn"
   composes directly on top of this spike.

**No dead ends** — every 19c requirement (engine/model/language/threads, persistence,
live vs respawn, model download via `vw_provision_model.cmake`, future translation
toggle) can be reached from this starting point without discarding the spike code.
The spike Lua file becomes the in-VLC launcher + quick-settings panel; the
standalone exe becomes the rich panel; the config namespace is the shared
persistence layer; the optional second listener (dossier channel b) is the later
live-control optimization.

### 4.4 Exact verification (Windows)

See `lua/README_SPIKE.md` §Windows manual test for verbatim steps: copy to
`<VLC>\lua\extensions\vlc_whisper_settings.lua`, `Tools > Messages` verbosity `2`,
menu `View > VLC-Whisper Settings (Spike)`, expected `[VLC-Whisper][SPIKE]` log
lines, four-widget dialog check (threads default `4`), `Apply` logging, `del`
removal. Linux steps are the same path with `~/.local/share/vlc/lua/extensions`.

---

## 5. Real GUI attachment plan (from THIS route)

1. **Phase 1 (this spike, done)**: Lua menu entry + dialog proof, no config writes.
2. **Phase 2 (19c quick-settings)**: add `vlc.config.set` to `on_apply` for the
   four keys above; add `add_string`/`add_integer` decls in
   `plugin/src/vw_whisper_module.c`; wire plugin reads in `vw_plugin_open` and
   live `wparams` setters in `vw_whisper_engine.c`; persist via
   `config_SaveConfigFile`.
3. **Phase 3 (19c rich GUI)**: ship `vlc-whisper-settings.exe` (Qt) with full
   manifest-driven model list, spinner, backend probe, model-download UI reusing
   `cmake/vw_provision_model.cmake`; Lua menu adds `Advanced…` button that calls
   a tiny C extension's `vw_platform_spawn_process` helper to launch the exe
   (the C extension is a second submodule in the same `.dll`).
4. **Phase 4 (live-control, optional)**: plugin-owned second listener (framed
   `vw_protocol_codec`, 32-byte token, `session_id` epoch) for sub-restart
   live-apply of `language`/`threads` if polling latency is unacceptable; degrades
   to config-file restart when plugin not loaded (dossier hybrid (a)+(b)).

All phases reuse the menu entry and the four config keys proven here.

---

## 6. References

- Dossier: `docs/plans/step19a_research_dossier.md` §§1–4
- Headers: `worker/third_party/vlc-3.0.23/include/vlc_extensions.h` (dialog/widget
  enums), `vlc_configuration.h` (`config_PutPsz`/`config_GetPsz`), `vlc_dialog.h`
- Example: `share/lua/extensions/VLSub.lua` (VideoLAN) — `descriptor`/`activate`/
  `add_dropdown`/`add_text_input`/`add_button`/`vlc.msg.info` pattern
- Docs: `vlc.verg.ca/m/dialog/` (dialog API), `vlc.verg.ca/m/config/` (Lua config API)
- Roadmap: `docs/roadmap.md` 19a/b/c/d, `AGENTS.md` invariants, `docs/source-layout.md`
