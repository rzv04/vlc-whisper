# Step 19a Research Dossier — Settings GUI Process Architecture & Control Channel

> **Scope**: pre-spike research for roadmap item **19a** (ADR-022). Translation is out of scope (roadmap 19b).
> **Method**: three parallel read-only scouts over vendored VLC 3.0.23 headers, vendored whisper.cpp source, and our plugin/worker/protocol sources, grounded with current upstream whisper.cpp documentation (context7 `/ggml-org/whisper.cpp`). Every claim below cites file:symbol or line.

---

## 1. GUI integration surface (VLC 3.0.23)

| Route | Feasible | Isolation | Packaging cost | Notes |
|---|---|---|---|---|
| Menu entry from `audio_filter` capability | ❌ | — | — | `audio_filter` modules get open/close callbacks and auto-surfaced Preferences controls only; no Tools/menu injection API (`vlc_plugin.h`: `set_capability`/`set_callbacks`; `vlc_actions.h` ACTIONIDs are not a third-party menu API). |
| Native C `interface` module (`set_capability("interface", N)`) | ✅ module runs | Own thread; **no Qt** — cannot create Qt dialogs from a native C interface module | Low (same `.dll`, add_submodule) | Dialog-provider hook exists (`vlc_interface.h`: `intf_thread_t.pf_show_dialog`, `INTF_DIALOG_*`) but rendering belongs to VLC's Qt interface, not to a bare C module. Practical use = launch external process. |
| Lua/C `extension` submodule (**supported menu path**) | ✅ best in-VLC hook | Runs inside VLC process (Lua VM), but UI is Qt-rendered by core (`vlc_extensions.h`: `EXTENSION_HAS_MENU`, `EXTENSION_TRIGGER_MENU`, `extension_dialog_t`, `EXTENSION_WIDGET_*`) | Low-medium (add_submodule to existing lib) | Gets an Extensions-menu entry and widget toolkit for free. A C extension can call `vw_platform_spawn_process` directly; a Lua extension reaches our DLL only through the shared config namespace (`config_GetPsz/PutPsz`, `var_*` — `vlc_configuration.h`, `vlc_variables.h`). |
| Standalone `vlc-whisper-settings.exe` (ADR-011 baseline) | ✅ | Best — full crash/thread isolation outside VLC | Medium (new exe + installer entry) | Launchable from Start Menu today; in-VLC launch via extension hook uses the already-proven spawn API (`vw_platform_spawn_process`, used by `vw_worker_client.c:95`). |

**Key correction to ADR-011**: its "`Tools → VLC-Whisper Settings…`" claim is not achievable from our capabilities without forking VLC (ADR-012 forbids). Realistic in-VLC launch point = **Extensions menu via a small C extension submodule**, plus Start Menu shortcut.

**Recommended direction for ADR-022**: keep the standalone `.exe` (isolation), add a minimal C `extension` submodule to the *same* plugin library whose sole job is menu presence + spawning the exe. The exe then talks to the plugin through the control channel (§2).

---

## 2. Control channel between settings GUI and the running plugin DLL

**Hard constraint discovered**: the GUI **cannot** ride the existing worker pipe. The worker owns the listener and accepts exactly one connection — Linux `listen(fd, 1)` then closes the server fd (`protocol/src/vw_ipc_socket_linux.c`, `vw_ipc_listen`); Windows `CreateNamedPipeA(..., nMaxInstances=1, ...)` (`vw_ipc_pipe_win32.c`). The plugin is always the client, never a listener.

Reusable machinery regardless of channel choice: framed header codec (`vw_protocol_types.h`: 20-byte `vw_frame_header_t`), constant-time token auth (32-byte HELLO), 16-byte `session_id` epochs with stale-epoch SEGMENT drop (`vw_whisper_module.c` ~590), POSITION message pattern, and the respawn path (`vw_plugin_respawn_worker`, `vw_whisper_module.c:292`).

| Channel candidate | Live-apply | New attack surface | Code reuse | Complexity | Verdict sketch |
|---|---|---|---|---|---|
| (a) VLC config system (`config_PutPsz` on plugin vars) + filter re-open / respawn trigger | Restart-grade (re-open needed) | None new (config already global namespace) | High | Lowest | Works even with a pure-Lua GUI; no live control |
| (b) Plugin-owned second listener (framed protocol + token auth reused) | True live commands | Second local listener (mitigate: random name + per-session token like worker) | High (codec/auth/epoch all reusable) | Medium | Enables live-apply for per-call engine params |
| (c) Config file + restart VLC semantics | Worst UX | None | Medium | Low | Fallback only |

Sketch verdict for ADR-022: **(a) as the write path for persisted settings + (b) as the optional live-control socket**, both guarded by the existing auth pattern; (c) implicit when plugin not loaded.

---

## 3. Whisper parameter surface — what can change live (vendored-source confirmed)

Two-layer model (authoritative: `whisper.h:490/533-534` vs `~70-82/106`; behavior `whisper.cpp:6977-6980`, `1604-1675`, `6832-6847`):

| Setting | Layer | Requires reinit | Current wrapper support | Plumbing gap to expose it |
|---|---|---|---|---|
| `n_threads` | `whisper_full_params` (per `whisper_full` call, by value) | **No** — O(1) next-call effect | ❌ hardcoded `wparams.n_threads=4` (`vw_whisper_engine.c`), warmup uses 2 | Engine setter / field read from config at transcribe time |
| `language` (+ auto-detect via `detect_language`) | `whisper_full_params` | **No** — language only selects the SOT token at decode; no tokenizer cache invalidation. Auto-detect runs a one-shot encoder pass and short-circuits that call | ❌ hardcoded `"en"`; `vw_worker_config_t.language` exists but is **dead** (no `--language` flag, never passed to engine) | Wire `config->language` → wparams; add `--language` flag; GUI dropdown sourced from `whisper_lang_max_id` + `whisper_lang_str/_full` (`whisper.h:358-370`) |
| Model file | `whisper_context_params` @ init | **Yes** — full teardown + `whisper_init_from_file_with_params` re-read (t_load_us self-timed but currently unlogged; `whisper_print_timings` never called) | Init-only (`engine_init(model_path,...)`); **no reload path exists** | New worker-side reload orchestration + load-time logging |
| Backend / GPU device (`use_gpu`, `gpu_device`) | Context params @ init | **Yes** — same rebuild class | CLI/config exist (`--backend`, `--gpu-device`) but **plugin never forwards them**: spawn argv is built with only `--pipe/--token/[--model]` (`vw_worker_client.c` ~60-85); plugin registers only `worker-path`/`model-path` config vars (`vw_whisper_module.c:804,839`) | Forward flags on spawn + respawn orchestration (reuse `vw_plugin_respawn_worker`) |

**Blocking-gap summary for 21c**: (1) engine setters for per-call params (drop-in, no reinit); (2) worker engine-reload path incl. load-time observability; (3) spawn-argv forwarding of backend/gpu/language/threads/vad from plugin config; (4) plugin-owned control listener if live-apply (channel b) is chosen.

---

## 4. Consequences carried into the 21a spike / ADR-022 drafting

1. ADR-011 must be **amended**: Tools-menu claim → Extensions-menu entry (C extension submodule) + Start Menu; standalone exe retained.
2. Control channel decision space narrows to (a)+(b) hybrid; (b)'s security posture must mirror the worker's (random name + token, single accepted peer).
3. Live-apply matrix is now evidence-backed: language/threads = live; model/backend/gpu = respawn (worker restart reusing epoch machinery; plugin respawn path already exists).
4. Language dropdown data source resolved (`whisper_lang_*` runtime APIs) — no hardcoded table needed.
5. Observability debt noted: model-load duration unlogged; add before promising "reload" UX.

## Open questions for the spike itself (next phase)
- Does the C `extension` submodule coexisting in the same plugin library complicate `audio_filter` open/close lifecycle or plugin-cache generation? (probe required)
- Minimum viable control-message set for live-apply (language/threads only?) vs deferring model/backend to respawn-only flow.
- Windows named-pipe second-listener naming/collision rules under Session-0-less user sessions.
