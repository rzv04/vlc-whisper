# Superseded — see README_SETTINGS.md

The spike extension (`VLC-Whisper Settings (Spike)`) has been rewired into the
shipped Lua settings GUI (Step 19b). See `lua/README_SETTINGS.md` for the
wired dialog, config keys, manual test, and packaging.

- Current extension: `lua/extensions/vlc_whisper_settings.lua` — descriptor `VLC-Whisper Settings`, reads/writes `whisper-backend`/`model-path`/`whisper-language`/`whisper-threads` via `vlc.config`, status label from `whisper-backend-active` (STATUS v1.3 `resolved_backend`).
- Spike file is retained for history; the authoritative docs are in `README_SETTINGS.md`.
