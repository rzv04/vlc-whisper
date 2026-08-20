# Implementation Task: Step 18 — Windows Installer Packaging, Open-Source Licensing & End-to-End Acceptance Plan

## Goal
Deliver a production-grade, plug-and-play Windows installer (`.exe` / `.zip`) and open-source licensing framework (MIT + `THIRD_PARTY_NOTICES.md`) for VLC-Whisper, enabling end users with existing VLC installations to install the plugin, worker, models (`tiny.en` + `silero-vad`), and runtime dependencies with zero manual configuration, and verify end-to-end local video and stream transcription acceptance.

---

## Context
- **Relevant docs/ADR**: `docs/architecture.md`, `docs/product.md`, `docs/source-layout.md`, `docs/api-contracts.md`, `docs/whisper-api.md`, `ADR-010` (Static MinGW runtime linking), `ADR-012` (Out-of-tree plugin packaging), `ADR-015` (Single Model Lifetime), `ADR-020` (Silero VAD Chunking), `ADR-021` (Subtitle Reading Floor & Deterministic Decoding).
- **VLC/worker/protocol version affected**: Packaging toolchain (`cmake/Packaging.cmake`, `cmake/vlc_whisper_installer.nsi.in`), Root `LICENSE`, Root `THIRD_PARTY_NOTICES.md`, Plugin path resolution (`plugin/src/vw_whisper_module.c`), Protocol v1.2 (compatible).
- **Assumptions and explicit non-goals**:
  - Non-goal: Re-distributing the full VLC Media Player installer (VLC-Whisper is an out-of-tree plugin that detects and integrates into pre-installed VLC 3.0.x x64 instances).
  - Non-goal: Modifying or forking VLC source code.
  - Non-goal: Closed-source commercial DRM or telemetry.
  - Assumption: Target user systems run 64-bit Windows 10/11 with 64-bit VLC 3.0.x installed.

---

## Scope
- **In scope**:
  1. **Open-Source Licensing Compliance & Permissive Relicensing**:
     - Author root `LICENSE` (MIT License).
     - Author comprehensive `THIRD_PARTY_NOTICES.md` detailing attributions and licenses for `whisper.cpp` (MIT), `ggml` (MIT), OpenAI Whisper weights (MIT), Silero VAD weights (MIT), VLC plugin API / `libvlccore` (LGPL v2.1+ dynamic linking), FFmpeg (LGPL v2.1+ POSIX), Vulkan SDK (Apache 2.0), and GCC / MinGW-w64 runtime (GPL v3 with GCC Runtime Library Exception v3.1).
  2. **GitHub Releases & Windows Installer Legal Validation**:
     - Formal confirmation of legal rights to distribute a single self-contained Windows installer containing worker, plugin, weights, and runtimes via public GitHub Releases.
  3. **VLC Plugin & Worker Path Auto-Discovery**:
     - Extend `vw_plugin_resolve_worker_path` and `vw_plugin_resolve_model_path` in `plugin/src/vw_whisper_module.c` to probe Windows Registry keys (`HKCU\Software\VLC-Whisper\InstallPath`, `HKLM\Software\VLC-Whisper\InstallPath`) and `%LOCALAPPDATA%\vlc-whisper\`.
  4. **Plug-and-Play Windows Installer & Packaging Script**:
     - NSIS script template (`cmake/vlc_whisper_installer.nsi.in`): 64-bit registry detection of VLC (`HKLM\Software\VideoLAN\VLC`), plugin deployment to `<VLC_DIR>\plugins\audio_filter\`, worker and models deployment, automated `vlc-cache-gen.exe` / `--reset-plugins-cache` cache regeneration, full uninstaller, and Start Menu/Desktop shortcuts with `--audio-filter=vlc_whisper`.
     - CMake CPack integration (`cmake/Packaging.cmake`): Standalone NSIS installer target (`installer`) and portable release ZIP generator (`cpack -G ZIP`).
  5. **End-to-End Local Media & Stream Acceptance Protocol**:
     - Structured test matrices for local video files (`.mp4`, `.mkv`, `.avi`, `.mov`), audio-only files (`.mp3`, `.flac`, `.wav`), live network streams (HTTP/HLS/RTSP), seeking/pause handling, and clean uninstallation.
- **Out of scope**:
  - Standalone GUI settings application (scheduled for Milestone 4 Step 21).
  - Multi-language translation models (scheduled for Milestone 4 Step 22).

---

## Design & Legal Analysis

### 1. Open-Source Licensing Compatibility Matrix

All components linked or bundled in VLC-Whisper are 100% compatible with releasing the root project under the **MIT License**:

| Component | License | Ingestion / Linking | Compatibility with Project MIT License | Distribution Conditions |
|---|---|---|---|---|
| **whisper.cpp & ggml** | MIT | Static compilation into `vlc-whisper-worker` | **100% Compatible** | Include ggml/whisper.cpp copyright notice in `THIRD_PARTY_NOTICES.md`. |
| **OpenAI Whisper Weights** (`ggml-tiny.en.bin`) | MIT | Ingested binary data file | **100% Compatible** | OpenAI explicitly released Whisper weights under MIT. Include OpenAI attribution. |
| **Silero VAD Weights** (`ggml-silero-vad.bin`) | MIT | Ingested binary data file | **100% Compatible** | Silero Team released weights under MIT (`snakers4/silero-vad`). Include Silero attribution. |
| **VLC 3.0 API & `libvlccore`** | LGPL v2.1+ | Dynamic loading/linking (`libvlccore.dll.a`) | **100% Compatible** | VideoLAN relicensed `libvlccore`/`libvlc` to LGPL v2.1 to allow permissive out-of-tree plugins. Satisfies LGPL v2.1 §6. |
| **FFmpeg Libraries** | LGPL v2.1+ | Dynamic linking (POSIX only) | **100% Compatible** | On Windows, VLC-Whisper uses native Windows Media Foundation (`mfplat.dll`), avoiding FFmpeg entirely. On Linux, dynamic linking against LGPL FFmpeg satisfies LGPL v2.1 §6. |
| **Vulkan SDK Loader & `glslc`** | Apache 2.0 / MIT | Dynamic link (`vulkan-1.dll`) | **100% Compatible** | Permissive Apache 2.0 allows bundling and linking with MIT binaries. Include Khronos & Google notices. |
| **MinGW-w64 Runtime** (`libgcc`, `libstdc++`, `libwinpthread`) | GPL v3 with **GCC Runtime Exception v3.1** / MIT | Static runtime linking (`-static -static-libgcc -static-libstdc++`) | **100% Compatible** | GCC Runtime Library Exception v3.1 explicitly permits static linking into independent works without subjecting the application to GPL copyleft. |

---

### 2. Standalone Windows Installer Distribution Legality

Distributing a self-contained installer via GitHub Releases containing:
- `vlc-whisper-worker.exe` (statically linked with MinGW runtime and whisper.cpp)
- `libvlc_whisper_plugin.dll` (dynamically interfaces with VLC)
- `models/ggml-tiny.en.bin` & `models/ggml-silero-vad.bin`
- `LICENSE` & `THIRD_PARTY_NOTICES.md`

is **fully legal and compliant** because:
1. **Zero GPL Copyleft Contagion**: No component in the Windows binary distribution uses pure GPL.
2. **LGPL Compliance**: VLC and FFmpeg (if used) are dynamically linked; source code links to upstream VideoLAN and FFmpeg are provided in `THIRD_PARTY_NOTICES.md`.
3. **Windows OS Exception**: Windows Media Foundation (`mfplat.dll`, `mfreadwrite.dll`) is a standard operating system component.

---

### 3. VLC 3.0 Plugin Discovery & Cache Regeneration on Windows

1. **Plugin Installation Directory**:
   - System-wide location: `<VLC_INSTALL_DIR>\plugins\audio_filter\libvlc_whisper_plugin.dll`
   - *Note on `%APPDATA%\vlc\plugins`*: VLC 3.0 on Windows does **not** scan `%APPDATA%\vlc\plugins` by default for binary C plugins without `VLC_PLUGIN_PATH`. Therefore, the installer targets `<VLC_INSTALL_DIR>\plugins\audio_filter\`.
2. **Plugin Cache Invalidation (`plugins.dat`)**:
   - VLC uses `<VLC_INSTALL_DIR>\plugins\plugins.dat` for fast startup. Dropping a DLL without rebuilding the cache causes VLC to ignore the new plugin.
   - The installer automatically executes:
     ```cmd
     "<VLC_INSTALL_DIR>\vlc-cache-gen.exe" "<VLC_INSTALL_DIR>\plugins"
     ```
     or falls back to `<VLC_INSTALL_DIR>\vlc.exe --reset-plugins-cache --version`.
3. **Worker & Model Installation Directory**:
   - Installed to `<VLC_INSTALL_DIR>\` or `C:\Program Files\VLC-Whisper\`.
   - The installer writes the installation path to the Windows Registry:
     `HKCU\Software\VLC-Whisper\InstallPath = "$INSTDIR"`
4. **Plugin Path Resolution in `vw_whisper_module.c`**:
   - 1. Ancestor walk (finds worker in `<VLC_DIR>` at `up = 2`).
   - 2. Windows Registry key `HKCU\Software\VLC-Whisper\InstallPath`.
   - 3. Environment path `%LOCALAPPDATA%\vlc-whisper\`.

---

### 4. Installer Flow Architecture

```
+-----------------------------------------------------------------------------------------------+
|                             VLC-WHISPER WINDOWS SETUP FLOW                                    |
+-----------------------------------------------------------------------------------------------+
|  1. Initialization: Verify 64-bit OS & query Registry HKLM\Software\VideoLAN\VLC\InstallDir   |
|  2. Stop Running Instances: taskkill /F /IM vlc.exe & taskkill /F /IM vlc-whisper-worker.exe   |
|  3. Deploy Plugin: Copy libvlc_whisper_plugin.dll -> <VLC>\plugins\audio_filter\             |
|  4. Deploy Worker & Models: Copy vlc-whisper-worker.exe, ggml-tiny.en.bin, ggml-silero-vad.bin|
|  5. Write Registry: HKCU\Software\VLC-Whisper\InstallPath & Windows Add/Remove Programs Key   |
|  6. Cache Invalidation: Execute vlc-cache-gen.exe "<VLC>\plugins"                             |
|  7. Create Shortcuts: "VLC (with AI Whisper Captions).lnk" with --audio-filter=vlc_whisper   |
+-----------------------------------------------------------------------------------------------+
```

---

## Acceptance Criteria
- [ ] Root `LICENSE` (MIT) and `THIRD_PARTY_NOTICES.md` authored and verified compliant with all dependencies.
- [ ] `plugin/src/vw_whisper_module.c` updated with Windows Registry (`HKCU\Software\VLC-Whisper\InstallPath`) and `%LOCALAPPDATA%` worker/model discovery.
- [ ] NSIS installer script template `cmake/vlc_whisper_installer.nsi.in` authored with VLC auto-detection, plugin placement, cache regeneration, and uninstaller.
- [ ] CMake packaging module `cmake/Packaging.cmake` wired into `CMakeLists.txt` supporting both standalone NSIS installer (`installer` target) and portable release ZIP (`cpack -G ZIP`).
- [ ] Windows MinGW cross-compilation builds `libvlc_whisper_plugin.dll` and `vlc-whisper-worker.exe` with zero errors.
- [ ] Packaging generation verified producing clean standalone distribution archives.
- [ ] Acceptance testing protocol documented covering local media playback, live streams, seeking, rate changes, and uninstallation.
- [ ] Documentation updated across `docs/architecture.md`, `docs/source-layout.md`, `docs/roadmap.md`, `docs/test-strategy.md`, and `README.md`.

---

## Implementation Breakdown (Step 18 Tasks)

### Task 18.1: Open-Source Licensing & Legal Notices
- Create root `LICENSE` with standard MIT License.
- Create root `THIRD_PARTY_NOTICES.md` with complete attribution and license texts for whisper.cpp, ggml, OpenAI Whisper weights, Silero VAD weights, VLC plugin API, FFmpeg, Vulkan, and MinGW-w64 runtime exception.

### Task 18.2: Plugin Auto-Discovery & Registry Probing
- In `plugin/src/vw_whisper_module.c`, implement `vw_plugin_probe_registry_install_dir` on Windows to read `HKCU\Software\VLC-Whisper\InstallPath` and `HKLM\Software\VLC-Whisper\InstallPath`.
- Extend `vw_plugin_resolve_worker_path` and `vw_plugin_resolve_model_path` to probe the registry install directory and `%LOCALAPPDATA%\vlc-whisper\` alongside ancestor walks.

### Task 18.3: NSIS Installer & CMake Packaging Pipeline
- Create `cmake/vlc_whisper_installer.nsi.in` incorporating:
  - 64-bit architecture verification.
  - VLC installation registry search (`HKLM\Software\VideoLAN\VLC` and Uninstall key).
  - Plugin deployment to `$INSTDIR\plugins\audio_filter\`.
  - Worker and model deployment to `$INSTDIR\` or `$LOCALAPPDATA\vlc-whisper\`.
  - Automatic invocation of `vlc-cache-gen.exe "$INSTDIR\plugins"` to regenerate `plugins.dat`.
  - Windows Add/Remove Programs uninstaller registration (`uninstall-vlc-whisper.exe`).
  - Start Menu & Desktop shortcuts (`vlc.exe --audio-filter=vlc_whisper`).
- Create `cmake/Packaging.cmake` with CPack configuration and optional `makensis` custom target.
- Include `cmake/Packaging.cmake` in root `CMakeLists.txt`.

### Task 18.4: Windows Release Packaging & Build Verification
- Execute native Linux debug build and test suite (`cmake --preset linux-x64-debug && ctest --preset linux-x64-debug`).
- Execute Windows MinGW cross-build (`cmake --preset windows-x64-release && cmake --build --preset windows-x64-release`).
- Generate distribution package (`cpack --config build/windows-x64-release/CPackConfig.cmake`).
- Verify contents of generated distribution archive against manifest.

### Task 18.5: End-to-End Local Video & Live Stream Acceptance Testing
- **Local File Acceptance Matrix**:
  - Video: `.mp4` (H.264/AAC), `.mkv` (HEVC/AC3), `.avi` (MPEG-4/MP3), `.mov` (ProRes/PCM).
  - Audio-only: `.mp3`, `.flac`, `.wav`, `.m4a`.
- **Live Stream Acceptance Matrix**:
  - HTTP live audio stream (e.g. icecast/shoutcast internet radio).
  - HLS / RTSP video stream with live audio.
- **Interactive Controls & Lifecycle**:
  - Play / Pause / Resume timeline synchronization.
  - Seeking forward/backward across speech and silence boundaries ($\pm 10\text{s}$, $\pm 60\text{s}$, rapid scrubbing).
  - Variable playback rates ($0.5\times$, $1.0\times$, $1.5\times$, $2.0\times$) verifying $\ge 1.0\,\text{s}$ wall-clock subtitle display floor.
- **Uninstallation Acceptance**:
  - Run uninstaller; verify `libvlc_whisper_plugin.dll`, worker, models, shortcuts, and registry keys are cleanly removed.
  - Verify VLC returns to stock state without errors or stale filter warnings.

---

## Test Plan

### 1. Unit & Build Verification
```bash
# 1. Format check
clang-format --dry-run --Werror plugin/src/vw_whisper_module.c

# 2. Linux native build and 20/20 test suite
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug --output-on-failure

# 3. Valgrind memcheck
ctest --test-dir build/linux-x64-debug -T memcheck

# 4. Windows MinGW Release build & CPack packaging
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release
cpack --config build/windows-x64-release/CPackConfig.cmake
```

### 2. Windows Manual Testing Protocol (VLC 3.0.23 x64)
1. **Installation**:
   - Run setup executable or extract release package into VLC directory.
   - Verify `plugins.dat` timestamp updates.
2. **VLC Execution**:
   - Launch VLC via shortcut `vlc.exe --audio-filter=vlc_whisper`.
   - Open test video `test_video.mp4` with dialogue and background music.
3. **Observations**:
   - **Expect**: Captions appear in bottom center within 1.0s of speech onset.
   - **Expect**: Subtitles remain on screen for $\ge 1.0\,\text{s}$ wall-clock reading floor.
   - **Expect**: Screen blanks cleanly during music/silence intervals.
   - **Expect**: Seeking forward or backward immediately clears old captions and re-anchors to new playhead with zero audio stutter.

---

## Definition of Done
- [ ] C17 standard compliance enforced (`-std=c17`).
- [ ] No blocking locks, allocations, or IPC in VLC realtime callback.
- [ ] Offline privacy invariant preserved (zero network calls, zero telemetry).
- [ ] Root `LICENSE` and `THIRD_PARTY_NOTICES.md` present in repository.
- [ ] `vw_whisper_module.c` probes registry and local app data for worker/model discovery.
- [ ] Windows packaging scripts (`Packaging.cmake`, `vlc_whisper_installer.nsi.in`) integrated.
- [ ] 20/20 automated tests pass on Linux and Windows cross-builds cleanly.
- [ ] Valgrind memcheck clean (0 leaks).
- [ ] `docs/roadmap.md`, `docs/architecture.md`, `docs/source-layout.md`, and `README.md` updated in the same change.
