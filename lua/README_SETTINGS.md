# VLC-Whisper Settings Launcher

`lua/extensions/vlc_whisper_settings.lua` is intentionally tiny. VLC still exposes **VLC-Whisper Settings** in its extension menu, but the Lua code no longer owns a settings form or model controls. It resolves the installed standalone settings executable, launches it detached, and immediately deactivates.

## Launcher invariant

The Lua extension does **not** poll, sleep, wait/join a child process, perform HTTP, hash model files, or synchronize settings. A missing executable or an immediate spawn failure produces one small VLC error dialog telling the user to reinstall VLC-Whisper.

A requested approximately one-second post-launch verification is deliberately omitted. VLC 3 Lua has no reliable nonblocking child/window-ready primitive; implementing that delay with the available process/sleep/check mechanisms would block or poll the cooperative VLC UI path. See ADR-025 in `docs/decisions.md`.

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
