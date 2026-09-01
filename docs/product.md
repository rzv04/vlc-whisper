# VLC-whisper Product Brief

## Purpose

**VLC-whisper** is a local-captioning extension ensemble for desktop VLC. While VLC plays media, it copies decoded playback audio to a local worker, which produces timed speech captions and returns them to VLC for on-video display. Audio remains on the user's computer; transcript text leaves the computer only if the user explicitly opts into the keyless subtitle translation feature (ADR-024). Transcription itself makes zero network requests; model provisioning and opt-in translation are isolated, explicit network paths.

The first supported target is **Windows 10/11 x64 with one pinned VLC 3.x build and English local media**. Linux x64 is a supported build target.

## User and problem

The initial user watches local video in VLC and needs captions when no usable subtitle track exists. They want captions to appear automatically, in the normal subtitle area, without uploading private audio or installing a separate transcription application. When watching foreign-language content, they can optionally enable keyless real-time subtitle translation into their preferred language.

The problem is not “perfect subtitles”. Speech recognition is delayed, can revise recent text, misses difficult audio, and has hardware-dependent speed. The product must prefer uninterrupted playback and truthful failure over pretending it can caption every source or machine.

## Optimal flow

1. The user installs the VLC-whisper package into a compatible VLC installation; the bundled multilingual `tiny` model is the default.
2. They open an ordinary local video, press Play, and VLC activates VLC-whisper.
3. The capture module duplicates decoded PCM with media timestamps; the worker transcribes bounded rolling audio windows.
4. VLC renders final timed segments in the subtitle/OSD region. If translation is enabled, translated or dual-line subtitles are rendered.
5. Pause pauses capture and caption clock progression; resume continues the same session. Stop/end clears captions and terminates or resets the worker session.

## Constraints

- **Network and privacy boundary:** model downloads are permitted only after an explicit settings action, run by the worker's dedicated download thread while media plays, and limited to sha256-pinned catalog URLs. Real-time subtitle translation (ADR-024) is an explicit user opt-in (disabled by default) that egresses only finalized text snippets over HTTPS to keyless Google endpoints with a strict 800ms timeout budget; audio/PCM never leaves the machine. There is no cloud transcription, telemetry, automatic download, remote logging, or network listener; Lua and the VLC audio filter module callback path remain completely network-free.
- **Language/code:** project-authored plugin and worker code is C17; whisper.cpp remains an unchanged, pinned third-party C/C++ dependency used through its C-style public API.
- **Platform:** build Windows x64 artifacts on Ubuntu with CMake and a pinned cross-toolchain; test them in real Windows VLC.
- **VLC compatibility:** native VLC modules are coupled to VLC internals. Pin and test against a precise VLC release/build; do not claim a durable generic third-party plugin ABI.
- **Playback safety:** no IPC, inference, disk operation, allocation, or blocking lock may run on VLC's realtime audio callback path.
- **MVP source:** local file URLs only. “Local” excludes optical media, capture devices, network shares, and remote URLs until explicitly added.
- **MVP model:** bundled multilingual `tiny`, CPU backend, concrete-language transcription, single active playback session.

## Non-goals (MVP Base Scope)

- Speaker labels, subtitle-file export, editing, search, updates, accounts, analytics, DRM bypass, and accessibility certification.
- Multiple simultaneous VLC instances, network streams, IPTV, VOD, livestreams, or accessibility certification.
- Replacing or altering embedded/external subtitle tracks. VLC-whisper is an additional generated-caption path.

## Future product order

1. Robust local playback with explicit unsupported-seek behavior.
2. Seek and discontinuity support for local files.
3. Stream classification and live/VOD behavior, with each source tested separately.
4. Settings UI, model provisioning, language selection, then optional packaged hardware backends.

A GUI comes after stable core semantics: it changes configuration, not the capture/worker/caption ownership model.
