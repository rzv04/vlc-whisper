# Step 19a — Lua Extension Route Feasibility Record (GUI Host Selection Input)

**Date:** 2026-08-24 · **Branch:** `gemini/milestone-4-step-19a` · **Inputs:** spike B (`gemini/milestone-4-step-19a-lua-extension` @ `fdd121f`), spike A (`…19a-c-interface` @ `1fe2af0`), `step19a_research_dossier.md`, `product.md:25`, `source-layout.md:147`.

## Verdict

The **Lua extension route is feasible as the primary GUI host** and is the recommended route for ADR-022, given the product requirement of *seamless in-VLC menu integration without distributing a recompiled VLC build*. The three gating questions below are answered **yes / yes-with-phasing / yes**, with the honest cost list against the native C interface route at the end.

---

## Q1 — Does the VLC UI stay unblocked, and do extension/VLC threads avoid resource clashes?

**Yes, by construction — with one discipline rule.**

- Lua extension callbacks (`activate`, `trigger_menu`, dialog button handlers, `close`) execute **cooperatively on VLC's interface/UI thread**. They are not parallel workers, so there are *no data races by construction*: no locks are shared with the audio-capture callback, the plugin sender/receiver threads, or the worker process. "Clash" can only mean *duration* — anything slow inside a callback stalls the whole UI.
- Discipline rule (binding for 19c): handlers must stay O(small). The spike's Apply does exactly this: read four widget values → `vlc.config.set` (in-memory write; core persists config on clean exit under its own serialization) → `vlc.msg.info`. Measured class: microseconds-to-milliseconds. No loops, no waits, no I/O beyond the config store.
- Resource footprint between events is zero: the dialog exists only while open; no timers, no polling threads, no sockets. Memory = widget table + state table (~KBs).
- The realtime path is untouched by construction: the extension never enters `vw_audio_capture_process_block` / the SPSC queue / the sender loop. Rule 4 holds trivially because there is nothing to synchronize.
- Future translation (roadmap 21) must NOT do HTTP inside a Lua callback — that would block the UI for the request duration. It belongs behind the plugin/worker boundary (worker-side fetch or plugin sender thread), which the ADR will scope.

## Q2 — Can it be wired to plugin/worker for ALL settings, before play and mid-session?

**Yes for all four settings, with per-setting apply costs.** Bridge mechanism: `vlc.config.set(name, value)` → core `config_PutPsz/PutInt` on proposed plugin vars (`whisper-backend`, `model-path`, `whisper-language`, `whisper-threads`) — the same namespace `vw_plugin_open` already reads via `config_GetPsz` (`spike_lua_extension.md` §3.2).

| Setting | Before playing media | Mid-video session | Mechanism |
|---|---|---|---|
| Language (`auto`/`en`/`ro`/`tr`/…) | ✅ applies at filter open | ✅ near-instant once roadmap 21c wires engine setters (language is a per-`whisper_full` param; dossier §3) + control channel | Config var → control message → engine setter |
| CPU threads | ✅ same | ✅ same (per-call param) | Same as above |
| Model (`tiny`/`base`/`large`) | ✅ at filter open (spawn argv forwarding — 21c gap G3) | ✅ with brief caption blackout: worker respawn reuses `START` epoch machinery (`vw_plugin_respawn_worker`); model load is seconds and currently unlogged (add timing) | Respawn orchestration |
| Engine backend (Vulkan/CPU) | ✅ spawn argv | ✅ same respawn path | Same as model |

So: **before-playing works natively from the config namespace today-plumbing** (values persist; filter reads them when the media opens). **Mid-session needs roadmap 21c's control-channel work regardless of GUI route** — that work item already exists and its design (dossier §2 channels a+b) was validated by both spikes; the Lua route neither forecloses it nor adds constraints.

Persistence note: values set via `vlc.config.*` are saved by VLC's own config-exit machinery; if immediate persistence across crashes is wanted, the C side can call `config_SaveConfigFile` when applying — implementation detail for 21c, not a blocker here.

## Q3 — Installer/uninstaller integration?

**Yes — trivial additive NSIS.** Lua scripts are not binary modules: they need **no** `plugins.dat` cache regeneration and no ABI coupling to the VLC build.

- Install section addition:
  ```nsis
  SetOutPath "$INSTDIR\lua\extensions"
  File "@CMAKE_SOURCE_DIR@\lua\extensions\vlc_whisper_settings.lua"
  ```
- Uninstall section addition:
  ```nsis
  Delete "$INSTDIR\lua\extensions\vlc_whisper_settings.lua"
  RMDir "$INSTDIR\lua\extensions"
  ```
- Scope notes: `$INSTDIR` targets the machine-wide VLC install this installer already manages. Per-user installs can use `%APPDATA%\vlc\lua\extensions\` instead — supported by manual copy (documented in `lua/README_SPIKE.md`); wiring that variant into NSIS would require a per-user install mode and is deferred until requested.

---

## Disadvantages vs the native C interface route (spike A)

Stated plainly — these are real, and they are accepted:

1. **UI-thread execution.** Every handler runs synchronously on VLC's UI thread. Fine for settings forms; becomes a structural ceiling if the GUI ever needs live status updates, progress, or translation-in-flight indicators (those would jank the UI from Lua and would have to move behind the plugin anyway). The C interface module owns a dedicated thread and blocks nothing.
2. **Sandboxed integration surface.** Lua cannot call our C functions or touch plugin internals directly — only the global config namespace and process spawning. Any live-control channel (roadmap 21c socket/message design) must be consumed indirectly (config writes + var notifications, or the plugin owning the listener while Lua merely flips config). The C module sits in-process and could integrate more tightly.
3. **Widget/toolkit ceiling.** `vlc.dialog` offers a fixed basic widget set — no spinner (threads uses a text input), limited layout, no styling. A rich panel still means spawning the standalone exe — which, notably, is the same endgame the C route recommends, so the practical loss is small.
4. **Menu activation nuance.** The entry lives under **View → [extension title]** after the extension manager loads it; first-use UX should be verified manually on the target build (expected: clicking the View-menu item activates then triggers — one click, comparable to a Tools entry, but this is behavior to confirm on Windows, not documented API guarantee).
5. **Error surface.** Runtime errors appear as log noise/dialog instead of compile-time guarantees; mitigated by `luac -p` in the gate and defensive nil-guards already present.

What Lua buys for those costs — and why it wins here: **zero compiled code added** (C17/no-C++ rules untouched by the GUI layer), single text file distribution, trivial install/uninstall, no ABI coupling to the pinned VLC build, no fork requirement, and menu presence without `--extraintf`-style launch flags — i.e., the seamless integration the C route cannot deliver without an extension submodule *anyway* (at which point the C code exists only to spawn an exe, while Lua *is* the panel).

---

## Decision input for ADR-022

Adopt **Lua extension as the primary GUI host** (roadmap 19c proceeds on this basis); retain the standalone `vlc-whisper-settings.exe` (ADR-011) as the optional rich-panel tier spawned from the Lua dialog if/when needed; park the native C interface route (spike A remains the reference implementation and its findings remain valid). ADR-022 formalizes this with the rejected-alternatives table drawn from §7.x spikes + this record.
