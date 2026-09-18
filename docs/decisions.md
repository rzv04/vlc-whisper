# Architecture Decisions

This document indexes and summarizes the architectural decision records (ADRs) for VLC-Whisper. System contracts and operational rules are codified in [`invariants.md`](invariants.md) and [`architecture.md`](architecture.md).

## ADR Summary Index

| ADR | Title | Status | Core Decision & Rationale |
| --- | --- | --- | --- |
| **001** | External local worker | Accepted | Run inference in a standalone out-of-process worker to isolate crashes/hangs from VLC's playback path. |
| **002** | C17 authored core | Accepted | Plugin, worker, protocol, and realtime code remain C17. The isolated Qt settings application is the sole production C++17 exception. |
| **003** | Pin VLC build | Accepted | Target exact pinned VLC 3.x releases (e.g. 3.0.23+ 64-bit); ABI assumptions verified at build time. |
| **004** | Authenticated local IPC | Accepted | Current-user named pipes (Win32) or `SOCK_SEQPACKET` (Linux) with 32-byte constant-time token verification; no TCP. |
| **005** | Final-only captions first | Accepted | Render immutable final segments to eliminate subtitle flicker and avoid premature replacement complexity. |
| **006** | Seeking & pause lifecycle | Accepted | Explicit `PAUSE`/`RESUME` controls; seeks flush SPSC queue & VAD, sending `STOP(SEEK_DISCONTINUITY)` and starting fresh epoch. |
| **007** | Model policy | Accepted | Ship local `tiny.en` CPU by default; manifest tracks SHA-256 and RAM limits; corrupt models fail gracefully. |
| **008** | Bounded loss over playback | Accepted | When inference falls behind, drop audio with explicit accounting; never stall VLC audio callbacks or playback. |
| **009** | No database | Accepted | Zero persistent audio, transcript, or playback history storage at runtime. |
| **010** | Build strategy | Accepted | CMake presets with static linking for MinGW dependencies; self-contained standalone binaries. |
| **011** | Standalone settings GUI | Reinstated by ADR-025 | The original standalone-process boundary is now used for the production Qt settings UI; ADR-022 remains historical context for the interim Lua dialog. |
| **012** | Out-of-tree packaging | Accepted | Ship external plugin DLL and worker without patching or distributing custom VLC binaries. |
| **013** | Decoupled worker reader | Accepted | Dedicated reader thread with 512-slot buffer (~10.2s absorption) decouples IPC reads from blocking inference. |
| **014** | Process-wide media init | Accepted | Windows Media Foundation (`MFStartup`/`MFShutdown`) initialized once per worker process, not per seek thread. |
| **015** | Model-once worker lifetime | Accepted | Worker loads Whisper model once at startup and reuses it across seek epochs; seeks reset buffers without reloading model. |
| **016** | Native VLC SPU pipeline | Accepted | Delegate subpicture queueing and PTS scheduling to VLC's native SPU engine (`vout_RegisterSubpictureChannel`). |
| **017** | Phrase-by-phrase timing | Accepted | Extract sub-segments with native centisecond offsets (`t0`/`t1` * 10000) for natural conversational pacing. |
| **018** | Final immutable subtitles | Accepted | Each phrase is emitted once with authentic bounds; overlapping re-recognitions dropped wholesale; no suffix revision. |
| **019** | Multi-tier VAD & silence | Accepted | Pinned Silero VAD (Tier 1) + acoustic confidence gating (Tier 2) + formatting filter (Tier 3) suppress hallucinations. |
| **020** | Non-overlapping chunking | Accepted | In lookahead mode, slice audio at natural Silero VAD pauses (6–24s) with zero overlap, reducing compute by 75%. |
| **021** | Reading floor & greedy dec | Accepted | Wall-clock minimum 1.0s reading floor for short cues; deterministic greedy decoding (`temperature=0.0f`) with bounded fallback. |
| **022** | Lua settings dialog | Superseded by ADR-025 | The in-VLC Lua form was useful but constrained by VLC's cooperative/sandboxed extension UI. Lua remains only as the launcher entry point. |
| **023** | Worker model downloads | Accepted | User-initiated, catalog-limited, SHA-256 streaming verification in worker process with atomic rename to per-user dir. |
| **024** | Keyless async translation | Accepted | Opt-in 3-tier fallback translation (Web RPC -> GTX -> Mobile Scrape) with 800ms budget; PCM never leaves local machine. |
| **025** | Standalone Qt settings + fire-and-forget Lua | Accepted | `VLC-Whisper Settings` launches a separate Qt 6 process. Settings persist in a per-user JSON file; Lua performs only bounded executable discovery and detached launch, with a one-shot error dialog for immediate launch failure. Model-download HTTP remains worker-owned. |

### ADR-025 launcher timing clarification

A requested ~1 second post-spawn verification is **not implemented** in the VLC Lua extension. VLC 3's Lua extension environment does not provide a reliable asynchronous child-liveness/window-ready primitive that would satisfy the existing UI-thread invariant; implementing that delay with sleep, process wait/join, or repeated checks would block or poll the cooperative extension path. The launcher therefore reports missing executables and immediate `os.execute` failures only, then deactivates immediately. A future native/nonblocking acknowledgement channel may add delayed startup diagnostics without weakening this invariant.

The standalone process may briefly wait on its own local single-instance socket; that wait is outside VLC and cannot stall playback or VLC's UI thread.

## Key Architectural Principles

1. **Strict Realtime Separation:** The VLC audio filter callback does bounded, non-blocking capture and enqueuing only. No inference, heap allocation, blocking locks, or IPC I/O occurs on VLC's realtime audio thread.
2. **Immutable Final Captions (ADR-018):** Subtitles are emitted once as final display cues. Deduplication suppresses exact duplicates, fragments, and expanded superstrings from overlapping windows without complex token-splitting heuristics.
3. **Pacing & Presentation Floor (ADR-021):** Cues shorter than 1.0s wall-clock duration are clamped to a minimum display floor to guarantee readability, with interval clipping against adjacent successor cues to prevent SPU collisions.
4. **Local-First Privacy Boundary (ADR-004, ADR-023, ADR-024):** Audio PCM never leaves the local machine. Model downloads are explicitly initiated by the user and verified by SHA-256. Opt-in translation operates asynchronously strictly on finalized subtitle text fragments.
5. **Settings Process Isolation (ADR-025):** The Qt GUI owns durable user settings and UI only. The Lua launcher does not poll or wait for it, and the worker retains all model-download network/verification ownership.
