# VLC-Whisper Settings Launcher

`lua/extensions/vlc_whisper_settings.lua` is intentionally tiny. VLC still exposes **VLC-Whisper Settings** in its extension menu, but Lua no longer owns a settings form or model controls. It opens the local `vlc-whisper-settings://launch` URI and reads a one-byte launch result.

## Launcher invariant

The URI is handled by a small access submodule embedded in the existing VLC-Whisper plugin. It invokes the production GUI in `--launch-detached` bootstrap mode without a command shell: `CreateProcessW(..., CREATE_NO_WINDOW, ...)` on Windows and `posix_spawn()` on Linux. The bootstrap then uses Qt `QProcess::startDetached()` to create the real settings instance and exits with success/failure.

Lua therefore does **not** use `os.execute`, `io.popen`, `start`, `cmd.exe`, PowerShell, polling, sleeps, window-ready probes, child-lifetime monitoring, HTTP, or model hashing. A missing/failed bootstrap produces one small VLC error dialog telling the user to reinstall VLC-Whisper. A later crash of the detached settings process is outside the launcher's responsibility. On Windows the bootstrap and real settings process are GUI executables, so this path does not intentionally create a blank console window.

## Executable locations

- Windows: `<VLC>\vlc-whisper-settings\vlc-whisper-settings.exe`
- Ubuntu/Debian: `/usr/bin/vlc-whisper-settings`

The executable is also directly launchable outside VLC and uses a per-user local single-instance channel so repeated launches focus the existing settings window instead of creating copies.

## Persistent settings

The Qt process atomically writes normal per-user files, so Apply never needs administrator/root access:

- Windows: `%LOCALAPPDATA%\vlc-whisper\settings.json`
- Linux: `$XDG_CONFIG_HOME/vlc-whisper/settings.json`, or `~/.config/vlc-whisper/settings.json`

The JSON uses the existing plugin key names (`whisper-backend`, `model-path`, `whisper-language`, `whisper-threads`, logging, paused subtitles, and translation settings). The plugin reads those values only on its existing non-realtime configuration path; VLC's audio callback never touches the file.

Reinstalling VLC-Whisper intentionally deletes `settings.json` and writes a small `reset-settings` marker. The plugin treats that marker as current defaults even if it starts before the Qt process recreates JSON.

## Model downloads

The Qt UI writes only a one-shot `model-command` control file. The plugin consumes that command on its existing sender/config reconciliation path and forwards it to the worker. HTTP, catalog validation, `.part` handling, SHA-256 verification, atomic rename, and abort cleanup remain worker-owned under ADR-023.

The settings application performs local model existence checks for display only; it neither downloads nor hashes model payloads.

## Translation testing

The **How to test** button is guidance only. It does not contact a translation provider. Actual translation occurs only in the worker while media is playing and translation is explicitly enabled.
