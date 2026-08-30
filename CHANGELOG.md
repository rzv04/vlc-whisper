# Changelog

All notable changes to VLC-Whisper will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - YYYY-MM-DD

### Added

- Initial public MVP release of VLC-Whisper.
- Real-time Whisper speech transcription directly inside VLC Media Player.
- Support for local media, network video-on-demand, and live/non-seekable streams.
- Seek-aware subtitle synchronization for local and seekable media.
- Local speech recognition powered by whisper.cpp with Silero VAD.
- Vulkan GPU acceleration with multi-core CPU fallback.
- Optional real-time subtitle translation with single-line and dual-line display modes.
- In-VLC settings for transcription language, model, compute backend, CPU threads, and translation.
- On-demand Whisper model downloading and validation.
- Windows 10/11 64-bit installer and portable distribution.
- Experimental Linux source support.

[Unreleased]: https://github.com/rzv04/vlc-whisper/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/rzv04/vlc-whisper/releases/tag/v0.1.0
