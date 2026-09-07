# Implementation Roadmap

This is the current product/engineering sequence. Completed milestone details belong in git history, plans, ADRs, and release notes—not duplicated here.

## Completed foundation

| Milestone | Result |
| --- | --- |
| **0 — Core scaffold** | C17 build, pinned dependencies, binary authenticated IPC, protocol validation, bounded queues. |
| **1 — Worker transcription** | Local Whisper worker, timed segments, source/audio processing foundation. |
| **2 — VLC feasibility** | Native VLC module, decoded PCM capture, generated caption presentation, packaging direction. |
| **3 — Local/live MVP** | Real-time streaming, play/pause, seek/session epochs, Vulkan/CPU backend, SPU presentation, source look-ahead, VAD, phrase timing, installer/package path. |
| **4 — Settings/provisioning/translation** | Lua settings, model selection/download, backend status, explicit verified model provisioning, opt-in finalized-text translation. |

The known-defect master ledger and subsequent hardening are tracked in issue #47 / `docs/issues.md`; do not copy the defect ledger into this roadmap.

## Milestone 5 — Quality baseline and reliability gate

Before expanding feature surface, establish reproducible quality/reliability evidence while preserving local-file seeking, live behavior, caption stability, privacy, and playback safety.

### 24. Finish regression corpus and quality/latency baseline

Maintain a small copyright-safe local corpus (~20 clips, at least English/Romanian) without committing media. Exercise offline/source, look-ahead, and live paths as applicable. Track WER/CER plus latency, RTF, timing error, duplicate/hallucination behavior, drops/queue pressure, translation timing, and visible stability. Corpus identity/hash/schema must be verified before scoring.

### 25. Optional measured transcription-quality pass

Only if the corpus shows worthwhile headroom, compare decoding/VAD/context/streaming approaches (including LocalAgreement-/AlignAtt-like or right-edge strategies where relevant). Adopt only measured net wins without material regressions to seek recovery, local/live latency, stability, or playback safety. This phase may be explicitly skipped.

### 26. Final bug hunt / release gate

No-new-features reliability pass across local/look-ahead/live media, seek/pause/rate/swap, translation, provisioning, CPU/GPU fallback, worker/plugin lifecycle, installer/uninstaller, logging, and failure recovery. Run the full existing verification/release checks and classify remaining blockers before Milestone 6.

**Exit:** reproducible corpus baseline; quality pass completed or deliberately skipped; no known release-blocking regression.

## Milestone 6 — Native Settings & Subtitle Hub

### 27. Native Qt settings/subtitle hub
Replace Lua with a packaged native Qt control plane while preserving plugin/worker ownership and safe apply/restart semantics.

### 28. Settings and automation policies
Independent automatic transcription/translation controls, existing-subtitle preference policy, language behavior, and user-facing latency/quality profiles.

### 29. Canonical subtitle document + SRT/WebVTT import/export
Format-independent timed cues for generated/translated/imported subtitles; export is explicit and preserves authentic media timing.

### 30. Full-media transcription to subtitle file
Seekable-media batch workflow using existing source decode/timing components, isolated from playback-time scheduling.

### 31. Existing subtitle integration/translation
Discover/manage textual subtitle sources and optionally translate them without unnecessarily running ASR; avoid duplicate display paths.

### 32. Subtitle search/download providers
Provider-neutral acquisition layer in the Qt hub; OpenSubtitles.com is an initial candidate. Keep provider/network/credential work out of realtime paths.

### 33. Subtitle synchronization research
Research alignment/drift/confidence approaches before committing to an algorithm or automatic-apply threshold.

### 34. Hub UX/model management/diagnostics
Coherent current-media state, models/download progress, errors/privacy disclosures, diagnostics, export status, and version information.

**Exit:** packaged native subtitle hub with coherent generated/existing subtitle workflows and preserved core invariants.

## Milestone 7 — Optional BYOK inference providers

Local `whisper.cpp` remains the default and this milestone is optional.

### 35. Provider-neutral ASR interface
Worker-side boundary that normalizes provider audio submission/results into existing caption/session semantics.

### 36. Capability negotiation
Explicitly model streaming/chunked input, segment/word timestamps, partials, language detection, limits, and cancellation.

### 37. Protected BYOK credential storage
Native UI entry with platform-appropriate protected persistence; keys never enter logs/reports.

### 38. First remote provider adapter
Implement one opt-in provider with chunk adaptation, seek/session cancellation, timestamp degradation policy, and regression coverage.

### 39. Provider privacy/failure UX
Explicit audio-egress disclosure and fail-closed or explicitly configured local fallback—never silent transmission/provider switching.

**Exit:** if pursued, remote ASR is explicit, capability-aware, privacy-visible, and cannot weaken the local path.

## Milestone 8 — VLC ecosystem/upstream compatibility

### 40. VLC 4 compatibility validation
Track stabilized VLC 4 APIs and classify unchanged/shimmed/superseded VLC-Whisper behavior without prematurely dropping VLC 3.

### 41. Native STT coexistence
Avoid duplicate AI captions when VLC-native STT is present; prefer explicit user choice.

### 42. Upstream contribution assessment
Identify self-contained work useful to VideoLAN (lifecycle, source/seek handling, provisioning, regression tooling, Windows fixes) and upstream only where architecture/maintainer interest align.

### 43. Platform/distribution expansion
Polish Linux packaging, then evaluate macOS/additional formats based on demand; every supported platform needs reproducible artifacts and the same lifecycle/privacy/regression gates.

**Exit:** dependable VLC 3 product, tested VLC 4 coexistence strategy, and demand-driven platform expansion.
