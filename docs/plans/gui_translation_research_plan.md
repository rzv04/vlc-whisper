# Implementation Task Template

# Task: Research & Feasibility Spikes — Settings GUI Architecture and Real-Time Subtitle Translation

## Goal
Two decision-grade research deliverables that unblock Milestone 4 implementation: an ADR fixing the settings/control GUI's process architecture and its channel to the running plugin DLL (Phase 1), and an ADR fixing a privacy-compliant, per-user real-time subtitle translation design with Daum PotPlayer parity (Phase 2). No production code ships from this task; the outputs are decisions, measured evidence, and re-scoped implementation items (roadmap 21c–23).

## Context
- Relevant docs/ADR:
  - `docs/decisions.md` ADR-011 (standalone settings GUI binary — to be validated or revised by Phase 1), ADR-013 (worker IPC reader thread), ADR-016 (native SPU pipeline), ADR-021 (subtitle pacing).
  - `docs/architecture.md` (ensemble boundaries: plugin must not block on inference/UI; worker owns inference), `docs/product.md` Constraints (privacy boundary: "no cloud inference, telemetry, automatic model download, remote logging, or network listener"; installing a model is user-controlled offline/package step), `docs/source-layout.md` ownership rules.
  - Baseline reference behavior: Daum PotPlayer's Whisper integration exposes engine (CPU/GPU), model size, language, thread count; its subtitle translator defaults to Google Translate and translates each finalized cue in near-real-time.
- VLC/worker/protocol version affected:
  - Protocol v1.2 unchanged by this research task. Any chosen control/translation design that alters framing requires a compatible-minor or major bump decided in the follow-up implementation steps (21c/23), not here.
- Assumptions and explicit non-goals:
  - The plugin DLL remains the sole VLC-facing component; the worker remains the sole inference component.
  - Non-goal (this task): implementing any GUI widget, network request, or translation pipeline. Research + ADR only.
  - Non-goal: shipping any shared/embedded API key. Key custody models evaluated in Phase 2 are strictly per-user.
  - Privacy invariant is non-negotiable: the shipped plugin/worker perform zero network I/O at playback time unless the user explicitly enables translation in a later step (which will require a documented product-boundary amendment in `docs/product.md`).

## Scope
- In scope:
  - **Phase 1 — GUI process architecture**: evaluate candidate architectures against VLC 3.0.23 realities: (a) standalone `vlc-whisper-settings.exe` launched out-of-process via a plugin-registered menu entry (ADR-011 baseline); (b) VLC interface/extension module (Lua/extension or interface plugin) inside the ensemble; (c) dialog hosted in-DLL from the audio filter. For each: crash/thread isolation from VLC's Qt main loop, packaging/installer impact, and — decisively — the control channel to the *running* plugin DLL: reuse of the existing authenticated framed-pipe pattern for a plugin-owned control endpoint vs a separate local socket vs config-file + session-restart handshake; live-apply semantics per setting (threads → worker restart; language/model → session restart; backend → worker respawn) mapped onto existing START/STOP/POSITION epoch machinery.
  - **Phase 2 — translation service feasibility**: reverse-engineer/document PotPlayer's observed mechanism (per-cue HTTP requests against Google endpoints as cues finalize); map compliant alternatives: Google Cloud Translation API v2/v3 free tier (~500k chars/month per billing account), DeepL API Free (~500k chars/month), user-supplied BYO key stored in local config; quantify latency budget against the ≤1s-per-final-cue target given typical cue lengths; define failure degradation (untranslated caption passthrough) and data-flow disclosure requirements.
  - Deliverables: ADR-022 (GUI architecture + control channel), ADR-023 (translation provider + custody + opt-in boundary amendment text), updated roadmap item scoping (21c/21d/23), product-boundary amendment draft if translation proceeds.
- Out of scope:
  - Any C/C++/Lua implementation of the GUI or translation path.
  - Choosing shipping model sizes beyond what `models/manifest.json` already declares.
  - On-device/local MT models (explicitly deferred; noted as future alternative if cloud tiers prove unsuitable).
- Files/components expected to change:
  - `docs/decisions.md` (+ADR-022, +ADR-023), `docs/plans/gui_translation_research_plan.md` (findings appended per phase), `docs/product.md` (boundary amendment draft, if translation is green-lit), `docs/roadmap.md` (item scoping refinements only).

## Design
- Inputs and outputs:
  - Inputs: pinned VLC 3.0.23 headers + observed Windows build behavior (`docs/vlc-api-essentials.md`, prior spike notes in `docs/plans/step17b_plan.md` §3), existing IPC/protocol sources (`protocol/`, `plugin/src/vw_worker_client.c`, `worker/src/vw_worker.c`), PotPlayer public documentation/community knowledge, provider pricing/ToS pages captured as evidence links.
  - Outputs: two ADRs with rejected-alternatives sections; a control-message sketch (names/payload fields only, no wire change committed); a translation request-shape sketch (endpoint class, auth header placement, per-cue payload size math at ~200 chars/cue × cue rate); latency measurements from a manual probe script (not shipped code) if needed to validate the ≤1s budget.
- Ownership/threading model:
  - Unchanged at runtime by this task. The ADR must however state the future ownership rule: whatever GUI form wins, it may never run code inside VLC's audio callback thread, and any plugin-owned control endpoint must be serviced by the existing sender/receiver threads or a new dedicated thread — never the capture callback (Rule 4).
- Bounds, time units, and failure behavior:
  - Translation latency budget: ≤1.0s wall-clock per finalized cue at 2× playback; queue depth for pending translations bounded (drop-oldest, mirroring audio backpressure policy); character-count ceiling per request = one cue (≤ protocol text cap 1KB) to stay within free-tier economics.
  - GUI control channel bounds: bounded command queue; commands are idempotent or explicitly sequenced via the existing epoch/session-id mechanism.
- Privacy/security implications:
  - Runtime product: zero network I/O unless translation explicitly enabled post-implementation; keys stored in local per-user config with ACL-equivalent protection (never in installer, never logged, redacted from diagnostics); translated text leaves the machine only after opt-in and only cue text (no paths, no identifiers) — all of which must appear verbatim in the ADR and the future privacy statement.
  - Developer/build time: provisioning downloads remain sha256-pinned and fire only on explicit `installer`/`provision_models` targets (per §7.8 semantics).
- Protocol change: none (research). Follow-up implementation may propose a compatible minor bump for GUI control messages.

## Acceptance criteria
- [ ] ADR-022 records the chosen GUI process architecture with at least three evaluated alternatives, rejection rationale grounded in cited VLC/VLC-whisper constraints, and a concrete control-channel design incl. live-apply vs restart mapping per setting.
- [ ] ADR-023 records the chosen translation approach with provider comparison table (free tier limits, ToS summary, key custody, measured/proxy latency), the opt-in boundary amendment text for `docs/product.md`, and explicit non-goals (no shared key, no default-on, no silent fallback to network).
- [ ] Both ADRs list testable consequences that roadmap items 21c/21d/23 can be verified against.
- [ ] `docs/roadmap.md` items re-scoped consistently with the ADR outcomes.
- [ ] No source/build changes ship in this task (verified by diff scope).
- [ ] Documentation/version metadata updated (this plan + decisions + roadmap only).

## Test plan
Research task — verification is evidentiary, not automated:
1. Phase 1 probe (optional but recommended): compile a minimal Win32 harness against vendored VLC headers that registers a menu entry and opens a named pipe; record whether the entry appears and the pipe connects while media plays — evidence attached to ADR-022.
2. Phase 2 probe: scripted curl/PowerShell calls against the candidate free-tier endpoints with a synthetic cue ("This is a test sentence.") measuring round-trip latency ×5 runs each, captured in the plan's Evidence section; ToS quotes linked.
3. Reviewer reproduction: clean checkout + `docs/plans/gui_translation_research_plan.md` alone must suffice to reach the same ADR conclusions.

## Definition of done
- [ ] C17 code; no project-authored C++ introduced — N/A (no code).
- [ ] No blocking work in VLC audio callback — asserted in ADR-022 ownership rules.
- [ ] No network access, telemetry, transcript/PCM persistence, or sensitive logs introduced — confirmed by diff scope (docs-only) and by ADR-023's opt-in boundary design.
- [ ] Memory, audio queue, frame, text, and retry limits are bounded — bounded-control-queue requirement recorded in ADR-022.
- [ ] Error path is safe: captions may stop, playback does not — reaffirmed in ADR-022 consequences.
- [ ] Unit/contract/integration tests pass as applicable — full suite still green (docs-only change).
- [ ] Formatting, warnings-as-errors, and static checks pass — N/A beyond markdown.
- [ ] Protocol contract and compatibility version updated if needed — explicitly "none" for this task; bump decision deferred to implementation ADRs.
- [ ] `docs/decisions.md`, roadmap, and AI context updated when assumptions change — this task's core output.
- [ ] Reviewer can reproduce the result from a clean checkout — probes documented step-by-step in Evidence.

## Evidence
- Build/test outputs or CI links: docs-only; suite re-run recorded in commit message.
- Measured performance (if relevant): Phase 2 provider latency probes (to be appended during execution).
- Known limitations/follow-ups: live Vulkan detection API surface for the engine selector may require a small worker STATUS payload extension during 21c (compatible-minor bump candidate); PotPlayer's exact Google endpoint is undocumented — Phase 2 records observed behavior + compliant alternatives rather than a byte-exact clone.
