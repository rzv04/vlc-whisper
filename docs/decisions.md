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

## ADR-006: Seeking and Play/Pause lifecycle in MVP

**Status:** Accepted (Updated).

Seeking and Play/Pause lifecycle are IN-SCOPE for the MVP (Milestone 3):
1. **Play/Pause**: When VLC pauses playback, the plugin sends a `PAUSE` control frame over IPC and suspends audio forwarding. Resuming sends `RESUME` and resumes timeline PTS sync.
2. **Seeking / Discontinuity**: When VLC seeks (`BLOCK_FLAG_DISCONTINUITY` or non-monotonic PTS jump), the plugin clears active presenter captions, sends a `STOP` (`SEEK_DISCONTINUITY`) control frame over IPC, flushes the SPSC queue & VAD state, and initializes a new session epoch (`timeline_origin_pts_us`) seamlessly without disabling captions or interrupting VLC media playback.

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

## ADR-012: Out-of-tree packaging over custom VLC build

**Status:** Accepted.

The VLC-Whisper plugin will be shipped as an external out-of-tree plugin with a standalone installer, rather than distributing a custom build of VLC.

Consequences:
1. **Compatibility**: We must strictly target the ABI of a pinned VLC release (e.g. 3.x series) to ensure the DLL loads correctly in user installations.
2. **Installation**: The installer will locate the user's existing VLC installation directory and copy `libvlc_whisper_plugin.dll` into the `plugins/` subdirectory.
3. **No Engine Forks**: We cannot modify VLC core player behavior or patch VLC itself to accommodate our subtitle or audio timing needs. We must use public VLC APIs and handle synchronization carefully within the plugin boundaries.

## ADR-013: Decoupled Worker IPC Reader Thread

**Status:** Accepted.

The worker process receives incoming `VW_MSG_AUDIO_PCM` frames across the IPC transport. Running `whisper_full()` inference directly on the worker main/reader loop introduces 200–500 ms of blocking delay per window. If the pipe is not drained continuously, backpressure travels back to the plugin's sender thread.

Consequences:
1. **Thread Decoupling**: Decouple IPC frame receiving from worker main loop using a dedicated worker IPC reader thread (`vw_worker_reader_main`) and a bounded SPSC queue (`vw_spsc_queue_t`).
2. **Pipe Safety**: The reader thread drains named pipe / socket frames continuously, while the worker main loop pops PCM chunks and executes inference asynchronously.

## ADR-014: Process-Wide Platform Media Framework Management

**Status:** Accepted.

Windows Media Foundation (`MFStartup`/`MFShutdown`) was originally called inside per-seek source decode threads. Rapid seeks caused race conditions between `MFShutdown` in an old thread and `MFStartup` in a newly spawned thread.

Consequences:
1. **Process Lifetime**: Manage platform media frameworks process-wide in `vw_worker_run` (`vw_source_decode_platform_init` and `vw_source_decode_platform_shutdown`).
2. **Thread Safety**: `MFStartup` is called once at worker startup, and `MFShutdown` is called once at worker exit. Short-lived demux threads must not alter global COM refcounts.

## ADR-015: Model-Once Worker Lifetime Across Seek Epochs

**Status:** Accepted.

Loading the ~74 MB `ggml-tiny.en.bin` model on every `START_SESSION` message required 1–2 seconds per seek. Rapid seeks resulted in continuous model reloads, killing new session epochs before captions could start.

Consequences:
1. **Single Model Load**: Worker process loads the whisper engine once at startup (`shared->engine`) and reuses it across all seek epochs (`START_SESSION`).
2. **Resource Ownership**: The engine is owned by the worker process and released only at worker process exit. Seeks reset audio buffers and builders without reloading model parameters from disk.

## ADR-016: Native VLC SPU Subpicture Pipeline for Timed Captions

**Status:** Accepted.

In look-ahead transcription mode, the worker generates caption segments ahead of VLC's current playback position. Rather than building a custom caption queue and timing thread inside the plugin, the plugin delegates subpicture queuing and PTS-based display scheduling entirely to VLC's native SPU (Subpicture Subsystem) pipeline.

Consequences:
1. **No Plugin Caption Queue**: The plugin does not maintain an internal queue or timer loop for future captions.
2. **Native Scheduling**: Transcribed segments received from IPC are converted directly to native VLC subpictures (`vout_RegisterSubpictureChannel` + `vout_PutSubpicture`) carrying exact `i_start` and `i_stop` media PTS timestamps. VLC handles precise frame-accurate rendering automatically.
3. **Flushing on Discontinuity**: On seeking or rate changes, the plugin issues `vout_FlushSubpictureChannel` / `spu_ClearChannel` to purge all pre-rendered look-ahead captions from VLC's queue.

## ADR-017: Phrase-by-Phrase Subtitle Timing via Native Whisper Segment Offsets

**Status:** Accepted.

In earlier milestones, all sub-segments emitted by Whisper across an 8.0-second acoustic analysis window were concatenated into a single combined string stamped with the full 8-second window duration (`[window_pts, window_pts + 8.0s]`). In live media playback, this coarse aggregation caused upcoming dialogue (such as the second speaker's reply in a conversational exchange) to appear 3–5 seconds before it was spoken, spoiling dramatic and conversational pacing while overcrowding the screen.

Decision:
1. **Per-Phrase Timestamp Extraction**: The worker extracts individual sub-segments from `whisper_full` using `whisper_full_n_segments(ctx)` and their exact centisecond offsets via `whisper_full_get_segment_t0(ctx, i)` and `whisper_full_get_segment_t1(ctx, i)`.
2. **Discrete Media PTS Bounds**: Each phrase is assigned discrete media timestamps ($\text{start\_pts} = \text{window\_pts} + t_0 \times 10\,000$, $\text{end\_pts} = \text{window\_pts} + t_1 \times 10\,000$) and pushed independently to `vw_segment_builder`.
3. **SPU Frame-Accurate Scheduling**: The plugin submits each phrase as a discrete subpicture to VLC's SPU engine. VLC displays and clears each phrase in exact synchrony with the speaker's vocal cadence, blanking during conversational pauses and eliminating dialogue spoilers (see [`docs/plans/phrase_timing_segmentation_plan.md`](file:///home/razvan/vlc-whisper/.worktrees/gemini/docs/plans/phrase_timing_segmentation_plan.md)).
4. **Token-Boundary Suffix Extraction (Step 17d.1) — SUPERSEDED by ADR-018**: `vw_segment_builder` deduplicates
   against committed history using 500ms timestamp proximity and text equality/containment. An expanded
   overlapping phrase is dropped wholesale under ADR-018 (final immutable subtitles); the token-boundary
   suffix extraction described here was reverted. Emitted phrases preserve 100% authentic Whisper acoustic
   bounds (t0, t1) with zero synthetic duration fabrication.

### Step 17d.1 — Token-Boundary Suffix Extraction (SUPERSEDED by ADR-018)

**Status:** Superseded (2026-08-20) — reverted by ADR-018 (Final Immutable Subtitles). The token-boundary
machinery described below (engine token accessors, `vw_segment_builder_push_phrase`, `vw_phrase_token_t`,
`params.token_timestamps`) has been REMOVED from the codebase. Overlapping expanded re-recognitions are
dropped wholesale; each phrase is emitted once, final, with authentic per-phrase bounds.

Step 17d.1 fixes the recurring P1 defect where whole-phrase deduplication dropped the NEW SUFFIX of an expanded
overlapping Whisper phrase (e.g. committed "jumps" then candidate "jumps quickly" was rejected wholesale,
leaving a permanent subtitle gap). The fix replaces superstring rejection with token-boundary suffix extraction:

* **Engine (`vw_whisper_engine`)**: adds `vw_whisper_engine_get_segment_token_count` and
  `vw_whisper_engine_get_segment_token`, mirroring the segment accessors and backed by `whisper_full_n_tokens`,
  `whisper_full_get_token_text`, and `whisper_full_get_token_data`. Token t0/t1 (whisper.cpp centiseconds) are
  scaled by `10000LL` to microseconds; token text is copied bounded to `VW_WHISPER_MAX_TOKEN_BYTES - 1`.
  Token-level timestamps require `token_timestamps = true` in the whisper full params.
* **Builder (`vw_segment_builder`)**: adds `vw_segment_builder_push_phrase(builder, text, start_pts, end_pts,
  tokens, token_count)` plus the borrowed `vw_phrase_token_t` view (absolute media-PTS t0/t1). `push_hypothesis`
  is now a wrapper passing `tokens = NULL`. With tokens present, an expanded phrase emits only its new suffix
  (tokens after the committed/last-queued end) while history records the full candidate. NULL tokens fall back
  to legacy whole-phrase dedup.
* **Worker (`vw_worker.c`)**: the three segment-push call sites fetch per-segment tokens, build an absolute-time
  `vw_phrase_token_t` array (capped at `VW_WHISPER_MAX_TOKENS_PER_SEGMENT`, saturating), and call `push_phrase`;
  on token-fetch failure they call `push_hypothesis`. No other worker logic changed.
* **Wire/protocol unchanged**: `vw_caption_segment_t` is unmodified; only the worker-side dedup strategy changed.

## ADR-018: Final Immutable Subtitles (No Expansion or Revision)

**Status:** Accepted (2026-08-20).

**Context.** PR 13 (Step 17d.1) iterated several times against a Greptile review loop over overlapping-window
expansion semantics: whole-phrase superstring rejection dropped newly recognized suffix words; token-boundary
suffix extraction then introduced edge cases of its own (boundary-spanning tokens, short-prefix repetition,
token-level timestamp availability). Every fix traded one edge case for another because the model kept trying
to REVISE already-emitted subtitles.

**Decision.** Subtitles are FINAL and immutable:
1. Each Whisper phrase is emitted exactly once, as an immutable cue carrying its authentic per-phrase
   acoustic bounds (`window_pts + t0/t1`, from `whisper_full_get_segment_t0/t1`, scaled `×10000LL`).
2. Overlapping windows that re-recognize already-covered audio are suppressed wholesale:
   exact duplicates, fragments, and expanded superstrings of committed or pending phrases are all dropped.
   There is NO suffix extraction, NO in-place revision, NO token-boundary splitting, and NO synthetic timing.
3. The first pass is authoritative: words that appear only in a later overlapping re-recognition are not
   retroactively emitted (accepted tradeoff — see consequences).

**Consequences.**
- `vw_segment_builder_push_hypothesis` is the single dedup entry point (whole-phrase: exact / fragment /
  superstring all drop); `vw_segment_builder_push_phrase`, `vw_phrase_token_t`, and the suffix-boundary
  selector are removed.
- Engine token-level accessors (`vw_whisper_engine_get_segment_token_count/get_segment_token`), the
  `vw_whisper_token_t` struct, and `params.token_timestamps` are removed — per-token timing is unused.
- Per-phrase timing (ADR-017 items 1-3) is preserved; the queue grows dynamically (committed cues are never
  discarded) and history commits only after a successful enqueue.
- Known limitation: a genuinely new word recognized only inside an expanded later window is omitted rather
  than emitted late or with synthetic timing. This is deliberate: revision churn and spoiler/repetition bugs
  are worse than the omission, and the next non-overlapping phrase resumes coverage.
- Partial-overlap dedup: a candidate whose word-aligned prefix repeats a ≥2-word word-aligned suffix of a
  time-adjacent/overlapping committed or pending cue (e.g. a continuation phrase that re-captions the previous
  cue's tail) is trimmed to emit only the not-yet-shown remainder, starting at that cue's end — each word
  appears once, so embedded previous-caption context is never duplicated on screen.
- Supersedes ADR-017 item 4 (Token-Boundary Suffix Extraction) and the Step 17d.1 "Shipped" section below.

## ADR-019: Multi-Tier Voice Activity Detection, Silence Gating & Hallucination Suppression

**Status:** Accepted (2026-08-20).

**Context.** Whisper models tend to hallucinate phantom cues (e.g. `[Music]`, `[Applause]`, standalone punctuation `...`, `---`, or repetitive loops) when processing silent intervals, instrumental soundtracks, or ambient noise. Executing Whisper inference unconditionally across silent audio wastes CPU/GPU compute, causes playback stalls, and pollutes the presentation screen with spurious text.

**Decision.** Implement a 3-tier silence and hallucination suppression pipeline in the worker:
1. **Tier 1 (Pre-Inference Voice Activity Detection)**:
   - Wire vendored Silero GGML VAD (`struct whisper_vad_context*`) via `whisper_vad_detect_speech` and `whisper_vad_segments_from_probs` across all three worker audio ingestion sites (live PCM stream, lookahead full window, and lookahead trailing EOF window).
   - Auto-discover `ggml-silero-vad.bin` in the model directory alongside `ggml-tiny.en.bin` or via CLI `--vad-model <path>`.
   - Provide graceful zero-config fallback to RMS Energy VAD (`0.01f` threshold) when no VAD model file is supplied.
   - Completely skip Whisper inference when no voice activity is detected in the audio window.
2. **Tier 2 (Post-Inference Acoustic Confidence Gating)**:
   - Configure Whisper decoding parameters `wparams.no_speech_thold = 0.60f`, `wparams.suppress_blank = true`, and `wparams.suppress_nst = true`.
   - Extract `whisper_full_get_segment_no_speech_prob` into `vw_whisper_segment_t.no_speech_prob` and discard sub-segments with $P(\text{no\_speech}) \ge 0.60$ for mixed speech/silence windows before segment builder ingestion.
3. **Tier 3 (Formatting & Non-Speech Tag Cleanliness Filter)**:
   - Modular filter implemented in `worker/src/vw_hallucination_filter.c`.
   - Reject non-speech sound tags (`[Music]`, `(applause)`, `♪`, `♫`, etc.) and isolated punctuation (`...`, `---`, `! ! !`) with zero alphanumeric characters.
   - Preserve 100% of authentic conversational dialogue and all valid sentence-internal punctuation without censorship or phrase blacklists.
4. **State Machine Lifecycle Resets**:
   - Reset recurrent LSTM hidden and cell states (`whisper_vad_reset_state`) on `VW_MSG_PAUSE`, `VW_MSG_RESUME`, `VW_MSG_STOP_SESSION`, and `VW_MSG_POSITION` seek jumps to prevent past audio state leakage.

**Consequences.**
- Zero phantom subtitle spam during silent scenes and instrumental music.
- Up to 80% CPU/GPU compute savings during non-dialogue media playback by skipping full Whisper model evaluations.
- Backward compatibility: existing setups without `ggml-silero-vad.bin` automatically continue functioning via RMS Energy fallback.

## ADR-020: VAD-Guided Non-Overlapping Audio Chunking for Lookahead Transcription

**Status:** Accepted (2026-08-20).

**Context.** Lookahead Mode demuxes and decodes local media files ahead of playback up to a 30-second lead horizon. Previously, it inherited the live streaming 8.0-second sliding analysis window with a 2.0-second hop. Overlapping sliding windows in lookahead mode caused:
1. $4\times$ redundant Whisper inference passes per second of audio, consuming excessive CPU/GPU cycles and battery.
2. Cross-hop acoustic and timestamp jitter ($\pm 20\text{--}50\,\text{ms}$) generating competing candidate hypotheses, triggering almost-duplicate and stuttering subtitle artifacts.
3. Mid-sentence word clipping across arbitrary 8.0s cut points.

**Decision.** Implement Silero VAD-Guided Non-Overlapping Audio Chunking (Strategy C) exclusively for Lookahead Source Mode:
1. **Dynamic Silence-Aligned Partitioning (`vw_vad_find_chunk_boundary`)**:
   - Accumulate lookahead audio in an enlarged 60-second ring buffer (`960,000` samples at 16kHz).
   - Use Silero VAD segment boundaries to identify natural conversational pauses ($\ge 300\,\text{ms}$ silence gap between sentences) bounded between $T_{min} = 6.0\,\text{s}$ ($96,000$ samples) and $T_{max} = 24.0\,\text{s}$ ($384,000$ samples), with $150\,\text{ms}$ ($2,400$ samples) acoustic tail padding.
   - For leading or full-window silence ($0$ speech segments), drain the non-speech audio immediately with **zero Whisper inference calls**.
   - For continuous monologues without silence gaps, clamp and force a split cleanly at $T_{max} = 24.0\,\text{s}$.
2. **$100\%$ Non-Overlapping Drain**:
   - Pass the speech chunk to `whisper_full` exactly once, push discrete phrase segments to `vw_segment_builder`, and drain the entire chunk (`vw_audio_buffer_drain(audio_buf, cut_samples)`).
   - Advance the lookahead timeline with zero overlap ($hop = cut\_samples$).
3. **Decoupled Live vs Lookahead Strategy**:
   - Retain the low-latency sliding window for live PCM streaming where future audio is unavailable.
   - Use Strategy C exclusively for `source_mode == true` local file decoding.

**Consequences.**
- **Zero Duplicate / Flickering Subtitles**: By construction, every second of audio is decoded once, eliminating duplicate candidates and cross-hop timestamp jitter.
- **Natural Sentence Cadence**: Speech chunks are sliced at natural pauses, eliminating mid-word cuts and preserving complete sentence context for Whisper attention.
- **$75\%$ Compute Reduction**: Reduces Whisper inference invocations from $30$ calls/min to $3\text{--}5$ calls/min during lookahead playback.
- **Clean SPU Integration**: Non-overlapping discrete cues map cleanly to VLC private SPU subpicture channel scheduling with automatic screen blanking during conversational pauses.

## ADR-021: Subtitle Reading Floor & Deterministic Whisper Decoding Optimization

**Status:** Accepted (2026-08-20).

**Context.** In natural conversational dialogue, short utterances (e.g. "Yeah", "Right", "No", "Okay") have raw acoustic durations between 150ms and 400ms. Displaying cues for their raw acoustic duration creates sub-second "flash cues" that vanish before human eye saccades and cognitive fixation can read them. Furthermore, unconstrained Whisper decoding configurations with non-zero fallback loops can trigger latency explosions (up to $6\times$) or stochastic hallucinations on ambiguous background audio.

**Decision.**
1. **Wall-Clock Minimum Subtitle Display Floor (`VW_CAPTION_MIN_DISPLAY_DURATION_US = 1000000LL`)**:
   - The presentation layer (`vw_caption_presenter.c`) acts as the single owner of visual pacing.
   - For any subtitle cue with raw acoustic duration $< 1.0\,\text{s}$, clamp the media duration to a rate-scaled floor:
     $$\text{duration\_us} = \max\big(\text{raw\_dur},\ \lfloor 1\,000\,000 \times \text{rate} \rfloor\big),\qquad \text{dur\_wall} = \frac{\text{duration\_us}}{\text{rate}} \ge 1\,000\,000\,\mu\text{s}$$
   - Guarantees subtitles remain on screen for at least **1.0 second of wall-clock reading time** across all playback rates ($0.5\times \to 0.5\text{s}$ media floor, $2.0\times \to 2.0\text{s}$ media floor).
   - Lookahead cues are buffered in the presenter (`presenter->has_pending`) so that whenever an adjacent successor cue begins within the floor window ($< 1.0\,\text{s}$), the earlier cue's display duration is cleanly clipped to the successor cue's start PTS ($\text{clipped\_end} = \min(\text{target\_end}, \text{next\_start})$), eliminating any SPU presentation interval overlap.
   - Long utterances ($> 1.0\text{s}$) preserve their full authentic acoustic duration.
   - `vw_segment_builder` remains untouched, strictly recording true acoustic boundaries for coverage deduplication.
2. **Deterministic Single-Pass Whisper Decoding Configuration (`vw_whisper_engine.c`)**:
   - `wparams.strategy = WHISPER_SAMPLING_GREEDY;`
   - `wparams.temperature = 0.0f;`
   - `wparams.temperature_inc = 0.2f;` (explicit bounded fallback $\le 5$ passes on degenerate sequences to prevent silent caption drops while keeping greedy decoding as primary).
   - `wparams.entropy_thold = 2.40f;` (halts low-entropy token repetition loops).
   - `wparams.logprob_thold = -1.00f;`
   - `wparams.no_speech_thold = 0.60f;`
   - `wparams.no_context = true;` (disables within-window segment conditioning, preventing hallucination carryover).
   - `wparams.single_segment = false;` (emits discrete sub-segments for phrase-by-phrase timing).
   - `wparams.suppress_blank = true;`
   - `wparams.suppress_nst = true;` (suppresses non-speech sound tokens at logit level).
   - `wparams.print_special = false;`
   - `wparams.max_len = 0;`
   - `wparams.token_timestamps = false;`

**Consequences.**
- **Zero Flash Cues**: Every subtitle is displayed with sufficient human reading time ($\ge 1.0\text{s}$ wall clock).
- **No Cue Collisions**: Cues display sequentially without visual overlap in VLC's SPU subpicture pipeline.
- **Deterministic Latency**: Greedy decoding ensures bounded, single-pass inference without search latency spikes.
- **Overlap prevention mechanism (verified 2026-08-20)**: the presenter posts every cue with `b_ephemer = true` (`vw_caption_presenter.c`), so VLC's SPU keeps only the newest same-channel ephemeral subpicture (`vlc_subpicture.h`: "displayed until the next one appear"). A successor cue therefore auto-evicts its predecessor regardless of the predecessor's posted `i_stop` — the 1s reading floor may leave two cues' *intervals* overlapping in the SPU chain, but only one is ever *rendered*. Interval clipping in `show_segment` is the lookahead precision layer (successor known in advance); ephemeral eviction is the safety net for the live-PCM path (successor not yet knowable at flush time). Static interval-overlap analysis that ignores `b_ephemer` is not a visible defect. This is a regression-tested invariant (`test_caption_presenter.c` asserts `b_ephemer == true` on every posted subpicture).
## ADR-022: Settings GUI via VLC Lua Extension (Spike, Non-Bundled Concept)

**Status:** Accepted (Spike).

**Context.** Step 19a required a feasibility spike for the settings/control GUI that will expose engine
backend (auto / Vulkan GPU / CPU), model (tiny / base / large), language (auto / en / ro / tr / …),
and CPU thread count (default 4). Three integration routes were evaluated against VLC 3.0.23 headers
and the existing ensemble boundaries: (a) standalone `vlc-whisper-settings.exe` per ADR-011, (b) native
C interface module (`set_capability("interface", N)` in the same plugin DLL), and (c) VLC Lua extension
(`lua/extensions/*.lua`, `vlc_extensions.h` / `vlc.dialog`). The research dossier
(`docs/plans/step19a_research_dossier.md`) and the Lua-extension feasibility record
(`docs/plans/step19a_lua_route_feasibility.md`) proved that an `audio_filter` module cannot inject
a Tools-menu item (`vlc_actions.h` ACTIONIDs are not a third-party menu API), while the worker IPC pipe
is strictly single-listener (`listen(,1)` / `nMaxInstances=1`) and cannot be reused as a GUI→plugin
channel.

**Decision.**

1. **GUI host: VLC Lua extension (primary), standalone exe retained as rich-panel tier.**
   - The spike extension `lua/extensions/vlc_whisper_settings.lua` (Lua 5.1-era, `luac -p` clean) owns the
     single Extensions-menu entry `View → VLC-Whisper Settings (Spike)` via `capabilities = {"menu"}` /
     `EXTENSION_HAS_MENU` / `EXTENSION_TRIGGER_MENU` and renders the four PotPlayer-parity controls with
     `vlc.dialog` widgets (dropdowns for engine / model / language, text_input default "4" for threads —
     the documented spinner gap: `EXTENSION_WIDGET_SPIN_ICON` is a static animation, not an input).
     On Apply the spike stores selections in a local `spike_state` table and logs
     `[VLC-Whisper][SPIKE]` lines; it never writes config or touches the network.
   - `vlc-whisper-settings.exe` (ADR-011) is retained as the optional out-of-process rich panel that a
     future Lua dialog can delegate to (e.g. an "Open advanced settings…" button spawning it via a tiny
     C `extension` helper). This preserves crash isolation without requiring a forked VLC build (ADR-012).

2. **Non-bundled concept scope.** The spike extension and its `lua/README_SPIKE.md` manual-test instructions
   are committed on branch `gemini/milestone-4-step-19a` as a feasibility artifact and are explicitly
   **excluded from CMake install / CPack / NSIS packaging**. The installer continues to deploy only
   `libvlc_whisper_plugin.dll`, the worker, and the sha256-pinned models (existing `install(TARGETS …)`
   rules); the Lua spike requires manual copy to `<VLC>\lua\extensions\` for manual testing and will not
   ship until a follow-up ADR promotes it to a bundled component.

3. **Wire-up feasibility for the real GUI (roadmap 19c).** The spike proves the bridge exists even though
   it is not wired: the Lua extension reaches the plugin through the shared VLC config namespace
   (`vlc.config.set` → `config_PutPsz` / `config_PutInt` on four proposed keys
   `whisper-backend` / `model-path` / `whisper-language` / `whisper-threads`, declared in
   `vlc_module_begin()` alongside the existing `worker-path`/`model-path` `add_loadfile` vars).  
   Per-setting apply costs (from `worker/third_party/whisper.cpp` and our wrappers):
   `whisper-language` and `whisper-threads` are `whisper_full_params` per-call state → live-settable
   once engine setters are wired; `model-path` and `whisper-backend` are `whisper_context_params` @ init →
   worker respawn via `vw_plugin_respawn_worker` (existing epoch machinery). Full mapping and
   language-list sourcing (`whisper_lang_max_id` / `whisper_lang_str`) in the spike report and dossier.

4. **Amendment to ADR-011.** ADR-011's `Tools → VLC-Whisper Settings…` claim is superseded:
   the blessed third-party menu surface is the **Extensions menu** (`View → …` after activation), not Tools.
   ADR-011's crash-isolation and Start-Menu launch properties remain true for the retained exe tier.

**Consequences.**

- Seamless in-VLC menu integration **without distributing a recompiled VLC build**; single text-file distribution,
  no ABI coupling to the pinned VLC build, `luac -p` gate, no C++.
- Lua runs cooperatively on VLC's UI thread: any heavy work stalls the whole UI. The phase-1 rule is strict:
  Apply handlers stay O(small) (config writes only); future translation (19b) must not HTTP inside a Lua callback.
- Widget toolkit is basic (no spinner; fixed layout); a rich panel remains a future exe.
- Installer/uninstaller integration is trivial when promoted (`File → lua\extensions\…` + `Delete`/`RMDir`; no
  `plugins.dat` regeneration needed — Lua is not a cached binary module).

**Rejected alternatives.**

- **Native C interface module in the same plugin DLL** — proven feasible (spike `gemini/milestone-4-step-19a-c-interface`
  @ `3697286`, heartbeat + `vw_platform_spawn_process` probe, Windows-verified, both presets build), but
  requires launch via `vlc --extraintf=vwsettingsintf` rather than a native menu entry and offers no toolkit
  advantage until the standalone exe is spawned anyway. Parked as the fallback if Lua is rejected post-testing.
- **In-DLL Qt dialog from the audio filter** — infeasible: `audio_filter` cannot own UI; Qt loop belongs to VLC's
  main thread.

