# VLC-Whisper Lua Extension Spike — Manual Test Instructions

This is a **feasibility spike**. The extension proves that a VLC Lua extension can host
a settings GUI; it does NOT write config or contact the whisper plugin/worker.

## Files

- `lua/extensions/vlc_whisper_settings.lua` — the extension (descriptor + dialog).

## Windows manual test (verbatim)

1. Ensure VLC 3.0.23 (32/64 matched to your install) is installed. Close all VLC instances.

2. Copy the spike extension into VLC's Lua extensions folder:

   ```
   copy lua\extensions\vlc_whisper_settings.lua "<VLC>\lua\extensions\vlc_whisper_settings.lua"
   ```

   Default `<VLC>` location:
   - 64-bit: `C:\Program Files\VideoLAN\VLC`
   - 32-bit: `C:\Program Files (x86)\VideoLAN\VLC`

   Concrete example (64-bit default):

   ```
   copy lua\extensions\vlc_whisper_settings.lua "C:\Program Files\VideoLAN\VLC\lua\extensions\vlc_whisper_settings.lua"
   ```

   Verify the file exists:

   ```
   dir "C:\Program Files\VideoLAN\VLC\lua\extensions\vlc_whisper_settings.lua"
   ```

3. Start VLC. Open the log viewer:

   - Menu: `Tools` > `Messages` (or `Tools` > `Messages` > `Verbosity` → `2 (Debug)`).
   - In the Messages window, set **Verbosity** to `2` (or run VLC with `vlc.exe -vvv` to force debug).

4. Activate the extension:

   - Menu: `View` > `VLC-Whisper Settings (Spike)` 
     — on some skins: `Tools` > `Extensions` > `VLC-Whisper Settings (Spike)`.
   - Alternatively `View` > `Extensions` and tick `VLC-Whisper Settings (Spike)`.

   Expected log lines (filter Messages for `SPIKE`):

   ```
   [VLC-Whisper][SPIKE] extension activate — building dialog
   [VLC-Whisper][SPIKE] dialog shown (Engine/Model/Language dropdowns + Threads default=4)
   ```

5. Verify the dialog content:

   - Title bar: `VLC-Whisper Settings (Spike)`.
   - Row 1: `Engine:` label + dropdown with `auto (default)`, `GPU (Vulkan)`, `CPU only`.
   - Row 2: `Model:` label + dropdown with `tiny.en (default)`, `tiny (multilingual)`, `base.en`, `base (multilingual)`, `small`, `medium`, `large`.
   - Row 3: `Language:` label + dropdown with `auto (detect)`, `English (en)`, `Romanian (ro)`, `Turkish (tr)`, `German (de)`, `French (fr)`, `Spanish (es)`.
   - Row 4: `Threads:` label + text input containing `4`.
   - Row 5: `Apply` button.
   - Row 6: hint label `Spike: Apply logs with [SPIKE] prefix; no config written.`

   **Spinner gap**: `Threads` is a plain text input, not a spinbox — VLC Lua's dialog toolkit has no spinner widget (`EXTENSION_WIDGET_SPIN_ICON` is a static loading icon, not an input). Documented in `docs/plans/spike_lua_extension.md`.

6. Exercise Apply:

   - Leave defaults or change:
     - Engine → `GPU (Vulkan)`
     - Model → `base.en`
     - Language → `ro`
     - Threads → `8` (type `8` replacing `4`)
   - Click `Apply`.

   Expected log lines (exact prefix `vlc.msg.info` with `[VLC-Whisper][SPIKE]`):

   ```
   [VLC-Whisper][SPIKE] Apply engine=gpu model=base.en language=ro threads=8
   [VLC-Whisper][SPIKE] state stored local table (no config write in spike)
   ```

   With defaults untouched:

   ```
   [VLC-Whisper][SPIKE] Apply engine=auto model=tiny.en language=en threads=4
   ```

7. Close the dialog via the `X` or `Close` action:

   ```
   [VLC-Whisper][SPIKE] extension close (user closed dialog)
   [VLC-Whisper][SPIKE] extension deactivate
   ```

   Re-open via `View` > `VLC-Whisper Settings (Spike)` should re-emit the activate lines.

8. Removal (restore clean state):

   ```
   del "C:\Program Files\VideoLAN\VLC\lua\extensions\vlc_whisper_settings.lua"
   ```

   Restart VLC. The menu entry must disappear and no `[SPIKE]` lines must appear in Messages.

## Linux manual test (verbatim)

1. Close VLC.

2. Copy the spike extension (pick one destination; user-local is preferred for testing):

   ```sh
   # User-local (no sudo):
   mkdir -p ~/.local/share/vlc/lua/extensions
   cp lua/extensions/vlc_whisper_settings.lua ~/.local/share/vlc/lua/extensions/vlc_whisper_settings.lua

   # System-wide:
   sudo cp lua/extensions/vlc_whisper_settings.lua /usr/share/vlc/lua/extensions/vlc_whisper_settings.lua
   # or /usr/lib/vlc/lua/extensions/ depending on distro
   ```

   Verify:

   ```sh
   ls -l ~/.local/share/vlc/lua/extensions/vlc_whisper_settings.lua
   # or
   ls -l /usr/share/vlc/lua/extensions/vlc_whisper_settings.lua
   ```

3. Start VLC with debug verbosity:

   ```sh
   vlc -vvv
   # or open Tools > Messages and set Verbosity to 2
   ```

4. Activate the extension:

   - Menu: `View` > `VLC-Whisper Settings (Spike)` (or `Tools` > `Extensions`).
   - Check terminal / Messages for:

   ```
   [VLC-Whisper][SPIKE] extension activate — building dialog
   [VLC-Whisper][SPIKE] dialog shown (Engine/Model/Language dropdowns + Threads default=4)
   ```

5. Verify the same four controls as on Windows (Engine/Model/Language dropdowns, Threads text_input default `4`, Apply button).

6. Click `Apply` (change or keep defaults):

   ```
   [VLC-Whisper][SPIKE] Apply engine=auto model=tiny.en language=en threads=4
   [VLC-Whisper][SPIKE] state stored local table (no config write in spike)
   ```

7. Syntax pre-check (Lua 5.1-era, no compile):

   ```sh
   luac -p lua/extensions/vlc_whisper_settings.lua && echo "syntax OK"
   ```

8. Removal:

   ```sh
   rm ~/.local/share/vlc/lua/extensions/vlc_whisper_settings.lua
   # or
   sudo rm /usr/share/vlc/lua/extensions/vlc_whisper_settings.lua
   ```

   Restart VLC and confirm the menu entry and `[SPIKE]` logs are gone.

## Troubleshooting

- **Menu entry missing**: confirm the file is named exactly `vlc_whisper_settings.lua` under `lua/extensions/` and VLC was restarted; check Messages with verbosity 2 for Lua errors.
- **Dialog empty**: ensure VLC is 3.0.x (API: `vlc.dialog`, `add_dropdown`/`add_text_input`/`add_button`/`vlc.msg.info`); 4.0+ is untested for this spike.
- **No `[SPIKE]` logs**: set `Tools > Messages > Verbosity → 2` or launch with `-vvv`.
- **`luac -p` fails**: the file must remain Lua 5.1-compatible (no `goto`, no `//`, no 5.3-only syntax).

## What this spike does NOT do

- No `config_PutPsz`/`config_PutInt` writes (real GUI would write `model-path`, `whisper-language`, `whisper-threads`, `whisper-backend` — see `docs/plans/spike_lua_extension.md`).
- No worker or plugin IPC.
- No network I/O.
- No audio-callback interaction.
