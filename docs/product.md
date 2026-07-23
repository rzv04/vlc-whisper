# VLC-whisper Product Brief

## Purpose

**VLC-whisper** is a local-captioning extension ensemble for desktop VLC. While VLC plays media, it copies decoded playback audio to a local worker, which produces timed speech captions and returns them to VLC for on-video display. Audio and transcript text remain on the user's computer; the running product makes no network request.

The first supported target is **Windows 10/11 x64 with one pinned VLC 3.x build and English local media**. Linux x64 is a planned port, not an MVP support promise.

## User and problem

The initial user watches English-language local video in VLC and needs captions when no usable subtitle track exists. They want captions to appear automatically, in the normal subtitle area, without uploading private audio or installing a separate transcription application.

The problem is not “perfect subtitles”. Speech recognition is delayed, can revise recent text, misses difficult audio, and has hardware-dependent speed. The product must prefer uninterrupted playback and truthful failure over pretending it can caption every source or machine.

## Happy path

1. The user installs the VLC-whisper package into a compatible VLC installation and stores the supplied `tiny.en` model locally.
2. They open an ordinary local English video, press Play, and VLC activates VLC-whisper.
3. The capture module duplicates decoded PCM with media timestamps; the worker transcribes bounded rolling audio windows.
4. VLC renders final timed segments in the subtitle/OSD region. A small rolling partial may appear only if the chosen VLC rendering integration supports safe replacement.
5. Pause pauses capture and caption clock progression; resume continues the same session. Stop/end clears captions and terminates or resets the worker session.

## Constraints

- **Privacy boundary:** no cloud inference, telemetry, automatic model download, remote logging, or network listener. Installing a model is a separate user-controlled offline/package step.
- **Language/code:** project-authored plugin and worker code is C17; whisper.cpp remains an unchanged, pinned third-party C/C++ dependency used through its C-style public API.
- **Platform:** build Windows x64 artifacts on Ubuntu with CMake and a pinned cross-toolchain; test them in real Windows VLC.
- **VLC compatibility:** native VLC modules are coupled to VLC internals. Pin and test against a precise VLC release/build; do not claim a durable generic third-party plugin ABI.
- **Playback safety:** no IPC, inference, disk operation, allocation, or blocking lock may run on VLC's realtime audio callback path.
- **MVP source:** local file URLs only. “Local” excludes optical media, capture devices, network shares, and remote URLs until explicitly added.
- **MVP model:** `tiny.en`, CPU backend, English transcription, single active playback session.

## Non-goals

- Translation, bilingual captions, speaker labels, subtitle-file export, editing, search, model downloading, updates, accounts, analytics, DRM bypass, and accessibility certification.
- Seeking, playback-rate changes, A/B loops, title/chapter transitions, multiple simultaneous VLC instances, network streams, IPTV, VOD, livestreams, GPU acceleration, or a configuration GUI.
- Replacing or altering embedded/external subtitle tracks. VLC-whisper is an additional generated-caption path.

## Future product order

1. Robust local playback with explicit unsupported-seek behavior.
2. Seek and discontinuity support for local files.
3. Stream classification and live/VOD behavior, with each source tested separately.
4. Settings UI, language and model selection, then optional packaged hardware backends.

A GUI comes after stable core semantics: it changes configuration, not the capture/worker/caption ownership model.
