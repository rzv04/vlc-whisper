# Architecture

VLC-Whisper is an ensemble: a native C VLC integration and a separate local worker. The split isolates inference/network failures from VLC's realtime playback path and keeps `whisper.cpp` out of the plugin process.

## Components

| Component | Owns | Must not do |
| --- | --- | --- |
| VLC audio filter | PCM capture/normalization, timestamp observation, bounded enqueue | infer, block on IPC/locks, touch filesystem, heap-allocate |
| Plugin sender/control thread | worker launch/handshake, queue drain, control frames, worker replies, caption dispatch, bounded metrics | block the VLC audio callback |
| Worker IPC reader + queue | receive/validate frames, bounded handoff | infer or delay transport reads on long computation |
| Worker main/session loop | lifecycle, source decode, VAD/windowing, inference, segment construction, translation coordination | control VLC/render directly |
| Caption presenter | validate/schedule/clear generated captions | trust stale session IDs or malformed timing/text |
| Model/translation workers | explicit model download; optional finalized-text translation | receive PCM for network egress |

See `api-contracts.md` for wire details.

## Audio and backpressure

The callback feeds a bounded SPSC queue; playback always wins. The plugin never waits for inference. Queue/worker overload may drop audio according to the owning queue policy, but time gaps must remain observable so later PCM is not timestamped as contiguous.

Canonical worker input is 16 kHz mono S16LE over IPC. Whisper itself receives normalized float32 PCM inside the worker.

Live/non-seekable mode uses progressive rolling inference: ~2 s initial eligibility, context growth to an 8 s maximum, then ~1 s steady-state hops with right-edge holdback for immature hypotheses. Seekable source mode may decode ahead of playback and uses VAD-guided source chunking. A local source that falls back to PCM keeps its source classification/policy rather than silently becoming a live algorithm.

These are implementation policies, not protocol compatibility guarantees; tests own their observable behavior.

## Clock domains

Do not assume every timestamp called `PTS` shares one clock:

- VLC 3 audio-filter block timestamps used by live PCM are in VLC's system-date presentation domain.
- Native source-decoder timestamps are media-relative.
- VLC input position (`INPUT_GET_TIME`) is media-relative.
- The current reliable filter-pushed caption rendering path on pinned VLC 3.0.23 uses the OSD/system-date render domain; look-ahead scheduling maps source media time relative to sampled playhead state.

Never subtract or compare values from different domains without an explicit mapping. `vlc-api-essentials.md` contains the pinned-VLC gotchas; `api-contracts.md` states each wire field's domain.

## Session lifecycle

A caption epoch has a random 128-bit `session_id`. Stale worker replies are rejected by session ID. Sequence numbers are monotonic per IPC direction and can survive caption-session restarts while the transport remains alive.

High-level transitions:

```text
IDLE -> STARTING -> PLAYING <-> PAUSED
                    |   |
                    |   +-> RESYNC -> STARTING   (seek/discontinuity/media epoch)
                    +----> STOPPING -> IDLE
                    +----> FAILED -> IDLE/recovery
```

Seek/discontinuity/source-epoch reset clears stale captions and buffered state, sends `STOP(SEEK_DISCONTINUITY)`, and starts a fresh caption session. In source look-ahead mode the worker process/IPC transport can remain alive while the caption epoch changes; translation settings are reapplied after the fresh `START`.

The required lifecycle contract is that EOF/media end flushes eligible residual speech exactly once before final session teardown. This branch introduces tail flush handling for live/non-seekable `MEDIA_END` (`vw_worker_flush_audio_tail`). Worker failure/respawn must rebuild state rather than reuse stale session fields.

## Reliability and Teardown Boundaries

- Caption SPU channel IDs belong to the held video output. Blanking and output replacement flush that output's
  private channel before releasing its reference, never an unrelated channel on a newly discovered output.
- Media swaps and worker recovery discard the previous source-seek filter anchor. Discontinuities also invalidate
  the live benchmark clock mapping; the next live audio chunk reanchors latency without erasing aggregate counters.
- Decoder seek pre-roll is discarded at sample granularity before returning PCM. FFmpeg prepares replacement
  resampling state before seeking, so resampler initialization failure leaves the previous decoder usable.
  Failed source seeks retain the decoded anchor and do not invalidate translation; repeated implicit retries are suppressed.
- Negative internal PCM timestamps remain valid buffer anchors. VAD trailing silence is capped at 300 ms from
  the raw speech endpoint, including end padding. Inference failure is fatal rather than a successful silent drain.
- Teardown joins the sender, drains queued live PCM, then orders `STOP(MEDIA_END)` before `SHUTDOWN`. The worker
  sends final speech through the same opt-in asynchronous translation path, including source-only timeout fallback,
  and finishes accepted translation work before closing IPC. Each translation request retains its 800 ms budget.
  The plugin receives through EOF and accounts close-path frames, captions, translation results, and presentation
  in the existing benchmark. EOF is the completion barrier, not a three-second inference assumption or frame-count cap.
  A **120-second hung-worker watchdog** bounds this receive phase: filter teardown can wait that long on a stuck
  worker, and exceeding it can still lose the tail. This is outside the realtime audio callback; it does not promise
  cancellation of an in-flight Whisper call. Process cleanup retains its separate bounded termination policy.
- Large translation scratch buffers are heap-owned on the worker translation thread, preserving operation on a
  128 KiB thread stack. Default diagnostic logs use exclusive per-process files; caller-selected log paths retain
  their explicit overwrite semantics. POSIX worker launch accepts only absolute executable paths, never `PATH` lookup.

## Source modes

**Live/non-seekable:** plugin forwards paced PCM; late captions render against the live presentation clock. Audio/session discontinuities create a fresh epoch.

**Look-ahead source:** plugin supplies source identity plus periodic `POSITION`; worker decodes ahead of the playhead. Accepted seeks create a fresh `START` epoch so buffered source/translated cues from the old epoch are stale by construction.

**Fallback:** failure to activate source decoding may fall back to plugin PCM when explicitly permitted; it must not convert a fatal/ambiguous source state into clean EOF or a mislabeled benchmark result.

## VAD, inference, and caption construction

The worker owns VAD and Whisper policy. It can use pinned Silero VAD with bounded fallback behavior; seek/pause/session transitions reset recurrent VAD state when required. Whisper segment timestamps are converted from centiseconds to microseconds and fed to the segment builder, which owns cross-window deduplication and immutable final cues. The presenter may extend visible duration for readability without changing authentic cue timing used for quality/export semantics.

## Network and privacy

Authenticated IPC is local only: Windows named pipe / Linux Unix-domain socket. There is no TCP fallback or network listener.

Network use is worker-confined:

- model download: explicit user action, catalog URL, SHA-256 verification, temp file + atomic publish;
- translation: explicit opt-in, finalized text only, bounded async queue/deadline.

No cloud transcription, telemetry, PCM egress, or implicit runtime transcript persistence. Explicit user-initiated subtitle exports and local git-ignored developer benchmark text artifacts are permitted; captured runtime PCM is not persisted.

## Dependency discipline

VLC and `whisper.cpp` are pinned dependencies. Dependency-sensitive claims must be verified against the exact pin, not remembered/current upstream behavior. Exhaustive vendor API copies are intentionally not maintained in project docs; see `vlc-api-essentials.md` and `whisper-api.md` for the project-specific subset.
