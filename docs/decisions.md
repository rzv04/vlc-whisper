# Architecture Decisions

This document indexes and summarizes the architectural decision records (ADRs) for VLC-Whisper. System contracts and operational rules are codified in [`invariants.md`](invariants.md) and [`architecture.md`](architecture.md).

## ADR Summary Index

| ADR | Title | Status | Core Decision & Rationale |
| --- | --- | --- | --- |
| **001** | External local worker | Accepted | Run inference in a standalone out-of-process worker to isolate crashes/hangs from VLC's playback path. |
| **002** | C17 authored code | Accepted | All project-authored code uses C17, matching VLC conventions and avoiding C++ runtime dependencies in plugin. |
| **003** | Pin VLC build | Accepted | Target exact pinned VLC 3.x releases (e.g. 3.0.23+ 64-bit); ABI assumptions verified at build time. |
| **004** | Authenticated local IPC | Accepted | Current-user named pipes (Win32) or `SOCK_SEQPACKET` (Linux) with 32-byte constant-time token verification; no TCP. |
| **005** | Final-only captions first | Accepted | Render immutable final segments to eliminate subtitle flicker and avoid premature replacement complexity. |
| **006** | Seeking & pause lifecycle | Accepted | Explicit `PAUSE`/`RESUME` controls; seeks flush SPSC queue & VAD, sending `STOP(SEEK_DISCONTINUITY)` and starting fresh epoch. |
| **007** | Model policy | Accepted | Ship local `tiny.en` CPU by default; manifest tracks SHA-256 and RAM limits; corrupt models fail gracefully. |
| **008** | Bounded loss over playback | Accepted | When inference falls behind, drop audio with explicit accounting; never stall VLC audio callbacks or playback. |
| **009** | No database | Accepted | Zero persistent audio, transcript, or playback history storage at runtime. |
| **010** | Build strategy | Accepted | CMake presets with static linking for MinGW dependencies; self-contained standalone binaries. |
| **011** | Standalone settings GUI | Superseded | Originally planned as a standalone exe; amended by ADR-022 to use VLC's native Lua extension menu. |
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
| **022** | Lua settings extension | Accepted | Integrated in-VLC settings dialog via Lua extension (`View -> VLC-Whisper Settings`); zero network I/O in UI thread. |
| **023** | Worker model downloads | Accepted | User-initiated, catalog-limited, SHA-256 streaming verification in worker process with atomic rename to per-user dir. |
| **024** | Keyless async translation | Accepted | Opt-in 3-tier fallback translation (Web RPC -> GTX -> Mobile Scrape) with 800ms budget; PCM never leaves local machine. |

## Key Architectural Principles

1. **Strict Realtime Separation:** The VLC audio filter callback does bounded, non-blocking capture and enqueuing only. No inference, heap allocation, blocking locks, or IPC I/O occurs on VLC's realtime audio thread.
2. **Immutable Final Captions (ADR-018):** Subtitles are emitted once as final display cues. Deduplication suppresses exact duplicates, fragments, and expanded superstrings from overlapping windows without complex token-splitting heuristics.
3. **Pacing & Presentation Floor (ADR-021):** Cues shorter than 1.0s wall-clock duration are clamped to a minimum display floor to guarantee readability, with interval clipping against adjacent successor cues to prevent SPU collisions.
4. **Local-First Privacy Boundary (ADR-004, ADR-023, ADR-024):** Audio PCM never leaves the local machine. Model downloads are explicitly initiated by the user and verified by SHA-256. Opt-in translation operates asynchronously strictly on finalized subtitle text fragments.
