# VLC-Whisper Settings Launcher

`lua/extensions/vlc_whisper_settings.lua` is intentionally tiny. VLC still exposes **VLC-Whisper Settings** in its extension menu, but the Lua code no longer owns a settings form or model controls. It resolves the installed standalone settings executable and invokes its short `--launch-detached` bootstrap mode.

## Launcher invariant

The bootstrap is process-creation acknowledgement only. `vlc-whisper-settings --launch-detached` uses Qt `QProcess::startDetached()` to create the real settings instance and immediately exits with success/failure. Lua reports a one-shot reinstall error when executable resolution or detached process creation fails.

The extension does **not** poll, sleep, wait for window creation, monitor the detached child's lifetime, perform HTTP, hash model files, or synchronize settings. It does not invoke `start`, `cmd.exe`, PowerShell, or a console helper command. On Windows the settings target is a GUI executable, so no blank console window is part of the intended path. A later crash of the detached settings app is not treated as a launcher failure. See ADR-025 in `docs/decisions.md`.

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
