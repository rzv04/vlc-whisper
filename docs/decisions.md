# Architecture Decisions

## ADR-001: External local worker

**Status:** Accepted.

Use a separate worker executable for inference. This contains whisper.cpp and model failures outside VLC, permits independent worker tests, and makes GPU backends later packaging variants rather than plugin dependencies. whisper.cpp supports a C-style API, Windows and Linux, CPU-only inference, VAD, and several optional accelerators, so the worker can remain a C-authored host over a pinned dependency. [page:0]

Consequence: define and test IPC now. The plugin must tolerate worker absence/crash without affecting media playback.

## ADR-002: C17 authored code

**Status:** Accepted.

All VLC integration, IPC, worker host, tests, and tooling authored here use C17. whisper.cpp remains third-party C/C++; link the worker with the appropriate C++ linker/runtime while calling only its C header surface. VLC itself is mainly developed in C, which aligns with this constraint. [page:1]

## ADR-003: Pin VLC build

**Status:** Accepted.

Support one exact Windows VLC 3.x distribution/build at a time. Native modules must be built/tested with matching headers, libraries, compiler/runtime conventions, and module conventions. VLC's developer site explicitly says the project evolves quickly and directs developers to source and current wiki material. [page:2]

Consequence: release manifests and CI artifacts include VLC version/commit and ABI assumptions. Compatibility with VLC 4 is a separate port, not an upgrade checkbox.

## ADR-004: Offline-only local IPC

**Status:** Accepted.

Use authenticated, current-user-only named pipes on Windows and Unix-domain `SOCK_SEQPACKET` on Linux. No localhost TCP, WebSocket, HTTP server, cloud fallback, telemetry, or auto-download. This meets the privacy claim even when a firewall or another local process is misconfigured.

## ADR-005: Final-only captions first

**Status:** Accepted.

MVP renders final segments only. This reduces flicker and avoids requiring subtitle replacement semantics before the VLC presentation spike is proven. Keep segment IDs and reserved `replace` capability so later partial hypotheses can revise a rolling caption area.

## ADR-006: Deliberate no-seek MVP

**Status:** Accepted, temporary.

Seeking must be detected and handled as a session-ending discontinuity: clear captions, stop worker input, present a single local status, retain VLC playback. Do not try to block VLC's seek control and do not crash. The first post-MVP feature is `RESET(timeline_epoch)` plus queue/segment invalidation.

## ADR-007: Model policy

**Status:** Accepted.

Ship/support only local `tiny.en` CPU for MVP. Expose a model manifest abstraction (`models/manifest.json`): ID, file name, language scope, model SHA-256 hash, disk size, and RAM estimate. Do not expose model dropdowns until install, validation, benchmarking, and failure UX exist.

### Manifest Verification Sequence & Runtime Policy

1. **Manifest Parsing**: Upon worker launch or receiving a `START` frame, `vlc-whisper-worker` reads `models/manifest.json` and matches the requested `model_id` (`tiny.en`).
2. **SHA-256 Verification**: Worker computes the SHA-256 hash of `models/ggml-tiny.en.bin` prior to passing the path to `whisper_init_from_file_with_params()`.
3. **Pre-allocation Memory Check**: Compare `ram_bytes_estimate` against available system RAM to prevent OOM panics in the process tree.
4. **Error Behavior**: If the manifest or binary model is missing or corrupt (SHA-256 mismatch), the worker emits `E_MODEL_MISSING` or `E_MODEL_INVALID` to the plugin. The caption session disables gracefully while VLC media playback continues uninterrupted.

## ADR-008: Bounded loss over playback impact

**Status:** Accepted.

When inference cannot keep up, drop new unprocessed audio and make this measurable. Never block VLC's audio path or slow playback. This produces caption gaps under load but preserves the media player's core responsibility.

## ADR-009: No database

**Status:** Accepted.

MVP persists no audio, transcript, playback history, or database. A future GUI may store settings in an OS-appropriate per-user configuration file with schema/version migration; it must not create a hidden transcript archive by default.

## ADR-010: Build strategy

**Status:** Accepted.

Use CMake presets/toolchain files and cross-compile Windows x64 worker artifacts from Ubuntu. All MinGW runtime dependencies (`libgcc`, `libstdc++`, `libgomp`, `libwinpthread`) are statically linked into target binaries (`vlc-whisper-worker.exe`, sample binaries) to ensure output executables are fully self-contained and run on Windows without missing DLL errors. The VLC native-module build is a risk-managed exception: first prove whether exact SDK/out-of-tree compilation is sufficient; otherwise maintain a small pinned VLC source patch/in-tree module build. A clean out-of-tree experience is desirable, but not allowed to overrule reliability.

## ADR-011: Standalone settings GUI binary

**Status:** Accepted.

The post-MVP settings and model-management GUI (`vlc-whisper-settings.exe`) will be authored and packaged as a standalone executable rather than embedded inside the VLC plugin DLL (`libvlc_whisper_plugin.dll`).

Consequences:

1. **Crash & Thread Isolation**: Keeps UI event loop execution out of VLC's process space, preventing UI freezes or thread deadlocks with VLC's main Qt window thread.
2. **Independent Execution**: Allows end-users to launch settings and pre-download models directly from the Start Menu without opening VLC or playing media first.
3. **VLC Menu Invocation**: The VLC plugin DLL registers a lightweight menu item under `Tools -> VLC-Whisper Settings...` which invokes `vlc-whisper-settings.exe` out-of-process (`CreateProcess()` / `exec()`).
