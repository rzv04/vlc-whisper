# VLC-Whisper Settings GUI — Lua Extension (Step 19b)

Wired Lua extension dialog for VLC 3.0.23 that reads/writes the plugin config namespace
and triggers a worker respawn via the plugin’s ~2 s poll loop. No network, no translation.

## Files

- `lua/extensions/vlc_whisper_settings.lua` — the extension (descriptor `VLC-Whisper Settings`, dialog with Engine/Model/Language/Threads + Detected-backend status label).

## Config keys (plugin registers; Lua writes via `vlc.config.set`)

- `whisper-backend` — `"auto"` (default) | `"gpu"` | `"cpu"`
- `model-path` — relative path under `models/`, e.g. `models/ggml-tiny.en.bin` (dropdown maps labels → paths; selection allowed even if file absent — `E_MODEL_MISSING` disables captions until 19c)
- `whisper-language` — `"en"` (default) | `ro` | `tr` | `de` | `fr` | `es` — **no `auto`** entry (tiny.en is English-only; auto-detect on English-only models is meaningless and deliberately omitted)
- `whisper-threads` — integer `1..16`, default `4` (clamped on Apply)
- `whisper-backend-active` — read-only mirror written by plugin when `STATUS` v1.3 `resolved_backend` drains (`gpu`/`cpu`); Lua reads it for the status label

Language/dropdown wiring: `Apply` does `tonumber` + clamp `1..16` for threads, then `vlc.config.set` ×4 (model-path set to `models/<chosen>.bin` relative path) and logs `[VLC-Whisper] applied …` lines. Plugin sender-loop polls every ~2 s; any diff vs last-applied snapshot → `vw_plugin_respawn_worker()` (brief caption gap, then resumes on new epoch). All four currently apply via respawn (live per-call for language/threads is a future optimization).

## Windows manual test (verbatim)

1. Install plugin + worker either by running the installer **or** by manual copy:

   ```cmd
   REM manual copy (developer workflow)
   copy build\windows-x64-release\plugin\libvlc_whisper_plugin.dll "C:\Program Files\VideoLAN\VLC\plugins\audio_filter\libvlc_whisper_plugin.dll"
   copy build\windows-x64-release\worker\vlc-whisper-worker.exe "C:\Program Files\VideoLAN\VLC\vlc-whisper-worker.exe"
   copy lua\extensions\vlc_whisper_settings.lua "C:\Program Files\VideoLAN\VLC\lua\extensions\vlc_whisper_settings.lua"
   REM with installer the lua file is already bundled
   "C:\Program Files\VideoLAN\VLC\vlc-cache-gen.exe" "C:\Program Files\VideoLAN\VLC\plugins"
   ```

   Verify:

   ```cmd
   dir "C:\Program Files\VideoLAN\VLC\lua\extensions\vlc_whisper_settings.lua"
   ```

2. Open `Tools > Messages`, set **Verbosity** to `2` (Debug) — or launch with `vlc.exe -vvv --audio-filter=vlc_whisper`.

3. Play any media. Open `View > VLC-Whisper Settings` (on some skins `Tools > Extensions > VLC-Whisper Settings`). The dialog preselects current values via `vlc.config.get`; the **Detected backend** label initially shows `(pending — start playback)` and switches to `gpu` or `cpu` after the first session `STARTED` / `STATUS` `resolved_backend`.

4. Change **Language** `en` → `ro`, click **Apply**. Expected within ~2 s (next sender-loop poll):

   - Messages shows `[VLC-Whisper] applied whisper-language=ro` (plus the other three `applied …` lines).
   - Plugin log `PLUGIN_RESPAWN` and worker restart (`STARTED` with new `session_id`).
   - Captions continue after the brief gap.

   With the bundled `tiny.en` English-only model, selecting `ro` does **NOT** translate output — worker clamps/falls back to `en` with `WARN` log. Expected.

5. Change **Engine** to `gpu` on a machine without Vulkan, click **Apply**. After restart the **Detected backend** label reads `cpu` (worker resolved `VW_HAVE_VULKAN` → `cpu`; `STATUS` `resolved_backend` mirrored into `whisper-backend-active`).

6. Close and re-open the dialog — it re-reads `whisper-backend`, `model-path`, `whisper-language`, `whisper-threads` and shows the last applied values.

## Linux manual test

```sh
mkdir -p ~/.local/share/vlc/lua/extensions
cp lua/extensions/vlc_whisper_settings.lua ~/.local/share/vlc/lua/extensions/vlc_whisper_settings.lua
vlc -vvv --audio-filter=vlc_whisper
# View > VLC-Whisper Settings, change Language en→ro, Apply — expect PLUGIN_RESPAWN + worker restart within ~2 s
```

## Model dropdown label → path

| Label | Relative path |
|-------|---------------|
| tiny.en (default) | `models/ggml-tiny.en.bin` |
| tiny (multilingual) | `models/ggml-tiny.bin` |
| base.en | `models/ggml-base.en.bin` |
| base (multilingual) | `models/ggml-base.bin` |
| small | `models/ggml-small.bin` |
| medium | `models/ggml-medium.bin` |
| large | `models/ggml-large.bin` |

Selection allowed even if file absent; `E_MODEL_MISSING` disables captions until provisioned (19c adds downloader).

## What NOT to expect

- No `auto` language option (deliberately omitted — English-only default model).
- No in-app model downloader (19c).
- No translation (21b).
- Settings do NOT persist across VLC restart unless VLC exits cleanly (`vlcrc` save happens on clean exit).

## Syntax check

```sh
luac -p lua/extensions/vlc_whisper_settings.lua && echo "syntax OK"
```

## Packaging

- NSIS installer (`cmake/vw_installer.nsi.in`) installs to `$INSTDIR\lua\extensions\vlc_whisper_settings.lua`; uninstall deletes it + `RMDir` (non-fatal).
- CPack ZIP (`cmake/vw_packaging.cmake`) bundles `lua/` via `install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lua" DESTINATION .)`.

## Protocol

STATUS v1.3 adds `resolved_backend[16]` (`"gpu"`|`"cpu"`, NUL-padded); 60 B encoded. Decoder handles legacy 44 B (zero-fills). `whisper-backend-active` is the Lua-visible mirror.
