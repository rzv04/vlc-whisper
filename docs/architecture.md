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

See `source-layout.md` for directory ownership and `api-contracts.md` for wire details.

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

The required lifecycle contract is that EOF/media end flushes eligible residual speech exactly once before final session teardown. The current PR base does not yet guarantee that behavior for live/non-seekable `MEDIA_END`; the runtime fix and regression are tracked in PR #50. Until that lands, treat tail flush as a known lifecycle defect rather than established behavior. Worker failure/respawn must rebuild state rather than reuse stale session fields.

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
