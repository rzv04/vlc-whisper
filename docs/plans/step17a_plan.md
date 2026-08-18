# Step 17a Plan: GPU Whisper (Vulkan) Acceleration — default ON

## Goal
Ship Vulkan GPU acceleration for the worker's whisper.cpp inference **by default**: `ggml-vulkan`
backend compiled into the standard build (`VW_WITH_VULKAN` default ON), worker CLI gains
`--backend auto|gpu|cpu` and `--gpu-device <id>`, with whisper.cpp's built-in transparent CPU
fallback when Vulkan is unavailable at runtime. No plugin IPC or rendering contract changes.

## Context
- **Relevant docs/ADRs**: `docs/roadmap.md:52` (17a: `ggml-vulkan`, `--backend auto|gpu|cpu`,
  `--gpu-device`, automatic CPU fallback, parallel-build memory-limit docs),
  `docs/plans/milestone3_postmortem.md` §2 + Phase B (archived `gemini/gpu-directml` branch:
  `VW_WITH_VULKAN` CMake option, same CLI, CPU fallback, glslc build-memory spikes documented as
  OUT-of-scope fix; quality pass explicitly NOT bundled — that is 17e), `worker/CMakeLists.txt`
  (GGML_VULKAN currently pinned OFF with a step-17a TODO comment), ADR-010 (Windows standalone
  statically-linked worker), ADR-015 (model-once engine lifetime).
- **VLC/worker/protocol version affected**: whisper.cpp 1.9.1 (pinned `worker/third_party/`).
  No protocol change. VLC 3.0.23 unchanged.
- **Source-verified whisper.cpp 1.9.1 facts** (librarian research, `agent://WhisperVulkanResearch`):
  - Backend selection is INTERNAL: `whisper_context_params` exposes only `use_gpu` (bool) and
    `gpu_device` (int ordinal into enumerated GPU/IGPU devices). `whisper_init_from_file_with_params`
    is the entry point the worker already uses; it honors both fields.
  - CPU auto-fallback is built-in and transparent: `whisper_backend_init_gpu` returns null when no
    GPU/IGPU device exists (`no GPU found` INFO log, whisper.cpp:1315-1318), then
    `whisper_backend_init` always appends a CPU backend (1352-1355) → `whisper_init_*` returns a
    VALID context. Caller cannot detect the fallback from the return value.
  - `--gpu-device <id>` maps to `params.gpu_device` — an ordinal, NOT the Vulkan physical-device
    index; resolve friendly labels via generic `ggml_backend_dev_count/dev_get/dev_type/dev_name`
    (public C ABI) or `ggml_backend_vk_get_device_count/description` (Vulkan-specific).
  - CMake: `GGML_VULKAN` cache option (default OFF) gates the `ggml-vulkan` subdirectory; it
    requires `find_package(Vulkan COMPONENTS glslc REQUIRED)` + SPIRV-Headers, and compiles
    shaders at build time via the `vulkan-shaders-gen` ExternalProject (glslc hard requirement).
  - Default build (`GGML_BACKEND_DL=OFF`) links `Vulkan::Vulkan` at LINK time → the worker exe
    imports `vulkan-1.dll`/`libvulkan.so` at load, even when the CPU fallback path is used.
    `GGML_BACKEND_DL=ON` + shared libs would lazily dlopen the backend instead (out of scope —
    note only).
  - Device ordering: `gpu_device` indexes the deduped `device_indices` list (UUID/driver dedup),
    reorderable via `GGML_VK_VISIBLE_DEVICES` env var (no `GGML_VULKAN_DEVICE` var exists).
- **Assumptions and explicit non-goals**:
  - "By default" = `VW_WITH_VULKAN` default ON, degrading to a CPU-only build (with a build
    warning) when the build host lacks a Vulkan SDK / glslc — preserves per-host reproducibility
    (postmortem constraint) while defaulting to GPU on capable hosts.
  - Runtime `vulkan-1.dll` dependency on Windows is accepted and DOCUMENTED (ADR-010 deploy note);
    lazy dlopen via `GGML_BACKEND_DL` is out of scope.
  - No beam-search/quality tuning here (17e). No SPU (17b). No source-mode (17c).
  - `--backend cpu` sets `use_gpu=false`; `--backend gpu`/`auto` set `use_gpu=true` and rely on
    whisper's internal fallback; `auto` is the default. There is no public API to force a named
    backend into whisper context creation — the three-level CLI is fully expressible via
    `use_gpu` + `gpu_device` (verified).

## Scope
- **In scope**:
  - CMake: `VW_WITH_VULKAN` cache option (default ON) controlling `GGML_VULKAN`; SDK-presence
    guard (warning + CPU-only when absent); replace the pinned-OFF comment block.
  - Worker config: `backend` enum (`auto|gpu|cpu`, default auto) + `gpu_device` (int, default 0);
    CLI `--backend <auto|gpu|cpu>` and `--gpu-device <id>` parsing with usage errors.
  - Engine: `vw_whisper_engine_init(model_path, backend, gpu_device)` sets `cparams.use_gpu` +
    `cparams.gpu_device`; a startup log records which backend/device whisper selected (pre-probe
    via `ggml_backend_dev_count`/`dev_get`/`dev_type`/`dev_name` and log chosen GPU + the
    fallback signal).
  - Docs: roadmap 17a check, architecture note, test-strategy, parallel-build memory-limit note
    (glslc `-j` OOM), ADR-010 runtime-dependency note (vulkan-1.dll), plan file.
- **Out of scope**: GGML_BACKEND_DL lazy loading, quality pass (17e), SPU (17b), device-list
  interactive picker (document ordinal + `GGML_VK_VISIBLE_DEVICES` instead), CUDA/Metal.
- **Files/components expected to change**:
  - `worker/CMakeLists.txt`, `CMakePresets.json` (optional presets)
  - `worker/include/vw_worker_config.h`, `worker/src/vw_worker_config.c`
  - `worker/include/vw_whisper_engine.h`, `worker/src/vw_whisper_engine.c`
  - `worker/src/main.c` (engine init call passes backend config)
  - `docs/roadmap.md`, `docs/architecture.md`, `docs/test-strategy.md`, `docs/plans/step17a_plan.md`

## Design
- **Inputs and outputs**: `--backend`/`--gpu-device` → config fields → `cparams.use_gpu` /
  `cparams.gpu_device` → whisper.cpp internal GPU-or-CPU selection → inference unchanged
  (float32 PCM in, segments out). No IPC/wire change.
- **Ownership/threading model**: unchanged — config parsed once at worker start (main), engine
  created once (ADR-015), inference on the main loop. No new threads.
- **Bounds, time units, and failure behavior**: `gpu_device` is clamped to `>= 0`; out-of-range
  ordinals behave per whisper (throws inside ggml-vulkan → registry skips → CPU fallback). Model
  load on GPU allocates VRAM; keep the existing E_MODEL_MISSING / NULL-context handling. CPU
  fallback is invisible to the caller (valid context) — the startup log makes the actual backend
  observable. Build-time: no glslc → CPU-only build with a CMake warning, never a hard failure
  unless `VW_WITH_VULKAN=ON` is explicitly forced.
- **Privacy/security implications**: none (local inference).
- **Protocol change**: none.

## Acceptance criteria
- [ ] Default build on a Vulkan-capable host produces a worker that uses the GPU: startup log
      shows the chosen Vulkan device (name) and `no GPU found` absent.
- [ ] `--backend cpu` produces a CPU-only worker (log confirms, no GPU device listed).
- [ ] `--backend gpu` / `auto` on a host WITHOUT Vulkan (or with GPU init failure) still starts,
      logs the fallback, and transcribes correctly on CPU.
- [ ] `--gpu-device <id>` selects the Nth GPU/IGPU device; out-of-range falls back safely.
- [ ] Host without a Vulkan SDK builds CPU-only with a warning (no hard failure unless forced ON).
- [ ] ctest 16/16 + memcheck + Windows cross-build pass with the new defaults.

## Test plan
- Automated gate (AGENTS.md rule 10): clang-format; cmake build + ctest; memcheck; Windows
  cross-build. Existing engine/lifecycle/ipc tests unchanged (backend-agnostic).
- Build-matrix smoke: (a) `VW_WITH_VULKAN=ON` on a host with the SDK → GPU log line;
  (b) `--backend cpu` → CPU; (c) host without SDK → CPU-only build warning.
- Manual (live VLC, required): captions work with GPU selected; same behavior as CPU; VRAM
  footprint visible in Task Manager; no crash on device switch.
- Document glslc parallel-build memory limits (postmortem out-of-scope lesson: keep `-j` modest
  or document swap pressure) — docs only, no build-system workaround.

## Build memory limits (glslc / Vulkan shaders)
Compiling ggml-vulkan shaders (glslc + the `vulkan-shaders-gen` ExternalProject) is RAM-hungry
and spikes host memory during a parallel build. The milestone-3 postmortem documents this exact
failure: `ninja -j` on the ggml-vulkan target caused host RAM/swap pressure and OOM on low-RAM
machines. Limits for this project's build hosts (e.g. the 8GB VMware VM used for development):
- Keep the build parallel factor modest: `cmake --build --preset <p> -j2` (documented and used
  throughout this step's verification); avoid `-j` auto (all cores) when compiling the Vulkan
  shader targets.
- First configure+build of a Vulkan-enabled tree compiles shaders once; subsequent incremental
  builds do not re-run shader generation (ExternalProject `BUILD_ALWAYS TRUE` re-checks, but the
  heavy work is the initial shader compile).
- CPU-only presets (`*-cpu`) never invoke glslc — they are the low-memory build path.

## Definition of done
- [ ] C17; no project-authored C++ (whisper.cpp stays behind its public C API)
- [ ] No blocking work in VLC audio callback; inference off-thread as today
- [ ] No network access, telemetry, transcript/PCM persistence, or sensitive logs
- [ ] Memory/queue/frame/text limits bounded; GPU memory documented in the build note
- [ ] Error path safe: GPU failure → CPU fallback → captions continue; playback never affected
- [ ] Postmortem lessons honored: backend acceleration isolated from quality tuning (17e stays
      separate); glslc build-memory documented, not fixed in-branch; CPU fallback validated
- [ ] ADR-010 deploy note updated (worker now imports vulkan-1.dll on Windows)
