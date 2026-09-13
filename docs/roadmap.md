# Implementation Roadmap

This document outlines the milestone history and future roadmap for VLC-Whisper.

## Milestone Status

| Milestone | Scope | Status | Summary |
| --- | --- | :---: | --- |
| **0 — Core scaffold** | C17 build, binary authenticated IPC, protocol validation | Complete | Cross-platform build presets, named pipes / Unix domain sockets, little-endian framing. |
| **1 — Worker transcription** | Local Whisper worker, timed segments, audio processing | Complete | Pinned `whisper.cpp`, 16 kHz mono float32 pipeline, monotonic timestamps. |
| **2 — VLC feasibility** | Native VLC module, decoded PCM capture, SPU presentation | Complete | Audio filter plugin, non-blocking callback, subpicture channel integration. |
| **3 — Local/live MVP** | Real-time streaming, play/pause, seek epochs, VAD | Complete | Progressive live windows, SPU frame scheduling, pause/seek state machine, Silero VAD. |
| **4 — Settings & translation** | Lua settings dialog, model downloads, text translation | Complete | In-VLC Lua UI, SHA-256 model verification, opt-in async keyless translation engine. |
| **5 — Quality & reliability** | Quality baseline, regression corpus, defect hardening | Complete | Headless FLEURS benchmark harness, 120s hung-worker watchdog, SPU channel cleanup. |
| **6 — Native Settings Hub** | Standalone Qt settings & subtitle control plane | Planned | Replace Lua with native Qt panel, automatic subtitle policies, SRT/VTT export. |
| **7 — BYOK inference** | Optional remote ASR providers (cloud BYOK) | Optional | Provider-neutral ASR interface, protected credential storage, strict privacy UX. |
| **8 — Upstream & VLC 4** | VLC ecosystem coexistence & compatibility | Future | VLC 4 API compatibility validation, native STT coexistence, upstream contributions. |

## Upcoming Focus: Milestone 6 (Native Settings & Subtitle Hub)

1. **Native Qt Control Plane:** Dedicated settings UI preserving plugin/worker ownership and safe apply/restart semantics.
2. **Subtitle Import / Export:** Format-independent timed cues for generated/translated subtitles with explicit SRT and WebVTT export.
3. **Full-Media Batch Transcription:** Standalone seekable-media transcription to subtitle file isolated from playback scheduling.
4. **Existing Subtitle Translation:** Discover existing text subtitle tracks and translate them without running speech recognition.
