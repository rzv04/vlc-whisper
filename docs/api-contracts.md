# Local Protocol Contracts

VLC-Whisper has no public HTTP/cloud API. This document covers the local versioned IPC between the VLC plugin and worker. Current protocol: **v1.6** (`major=1`, `minor=6`). Framing/auth/time-unit changes require a major bump; bounded optional additions require a minor bump plus negotiated capability/minor gating where needed.

## Transport

- Windows: authenticated message-mode named pipe.
- Linux: owner-only Unix-domain socket using the same binary frame format.
- Accept timeout: 10 s.
- Normal read/write timeout: 3 s.
- Receive result: `>0` bytes, `VW_IPC_RECV_TIMEOUT (-1)` = retryable/no data, `VW_IPC_RECV_FATAL (-2)` = EOF/fatal/disconnected; callers must not collapse timeout into EOF.
- No TCP fallback.

## Frame envelope

```text
u32 magic = 0x564C4357   # VLCW
u16 protocol_major
u16 message_type
u32 payload_length       # <= 1,048,576 and message-specific bounds
u64 sequence             # strictly increasing per direction
payload
```

All fixed-width integers are little-endian. Text is strict UTF-8. Receivers validate header, payload length, message schema, semantic bounds, session/sequence, and relevant PTS/time domains before acting.

## Time domains

- Live `AUDIO.start_pts_us`: VLC audio-filter/system-date presentation domain.
- Source-decoder segment/position timestamps: media-relative.
- `POSITION.input_time_us`: VLC media position.
- Never mix these domains implicitly; presentation code performs the explicit mapping owned by the relevant mode.

## Session model

Most session messages carry `session_id[16]`; `HELLO` is pre-session. A fresh playback start and each accepted seek/source epoch use a new random ID. Stale session messages are ignored/rejected as appropriate. The worker process/transport may remain alive across caption-epoch restarts.

## Messages

| Message | Direction | Core contract |
| --- | --- | --- |
| `HELLO` | plugin→worker | Protocol range + 32-byte token + client version. Token checked in constant time. |
| `HELLO_ACK` | worker→plugin | Negotiated major/minor, capability flags, worker version. Optional features are capability/minor gated. |
| `START` | plugin→worker | Session, timeline origin, exact 16 kHz mono S16LE format, model/language, source kind, optional bounded source URL. Semantic validation is mandatory. |
| `STARTED` | worker→plugin | v1.6 correlates `session_id` and reports `source_active`; legacy smaller payload accepted only for negotiated older minor. |
| `POSITION` | plugin→worker | Source pacing: media playhead, sampled input time, positive finite playback rate, strict flag mask. Source seek from the plugin client normally becomes STOP(old) → fresh START → reapply translation → POSITION(new). |
| `AUDIO` | plugin→worker | Session + start PTS + duration + PCM bytes. Payload/sample/duration relation must be exact; stale/overlapping discontinuity audio is rejected. |
| `SEGMENT` | worker→plugin | Session, segment ID, start/end PTS, final flag, bounded UTF-8 source text, optional translated text/latency/tier. Plugin renders only current-session valid final cues. |
| `PAUSE` / `RESUME` | plugin→worker | Session-scoped lifecycle controls. Pause clears/invalidate in-flight partial work as defined by worker lifecycle. |
| `STOP` | plugin→worker | Session-scoped stop; reasons include user stop, seek discontinuity, media end. Current-session STOP is idempotent. |
| `SHUTDOWN` | plugin→worker | Header-only process shutdown. Worker closes transport and exits cleanly. |
| `STATUS` | worker→plugin | Session state plus queue/inference/drop metrics and actual resolved backend. Metrics require authoritative producers, documented units/reset scope. |
| `ERROR` | primarily worker→plugin | Session, code, recoverable flag, bounded redacted message. Nonrecoverable failure disables caption session/transport, never VLC playback. |
| `MODEL_CTRL` | plugin→worker | Worker-scoped explicit download/abort command; zero session ID is allowed when caption START cannot proceed. |
| `MODEL_PROGRESS` | worker→plugin | Download stage, percentage/bytes, model ID. Initial IDLE snapshot is informational; terminal correlation persists until DONE/FAILED/abort/transport death. |
| `TRANSLATE_CTRL` | plugin→worker | Session-scoped enabled/source/target/display mode. Sent only when translation capability is negotiated. |

## Important message semantics

### START / source identity

`sample_rate=16000`, `channels=1`, `sample_format=S16LE`. Model/language/source fields must be complete and terminated where fixed-size storage applies; oversized identities are rejected rather than truncated. Source-kind and URL presence must agree.

### Source seek

For source mode, the plugin client treats accepted seek as a **new caption epoch**: `STOP(SEEK_DISCONTINUITY)` old ID → fresh `START` with new random ID and new origin → require correlated `STARTED(source_active=1)` → reapply cached translation → `POSITION`. Buffered source/translated cues from the old ID are therefore stale by construction. Failure to re-enter a trustworthy source state drops/restarts the transport rather than continuing ambiguously.

### SEGMENT / translation

`end_pts_us > start_pts_us`; text is bounded valid UTF-8 and renderer-safe. `is_final=true` means immutable displayable cue. Translation runs off the main inference/control path through a bounded queue and one total cue deadline; failure emits the source cue without translated text. PCM/audio is never sent to translation endpoints.

### STATUS metrics

`resolved_backend` reports the backend actually used (`gpu`/`cpu`), not the requested backend. `inference_us` is cumulative time owned by the inference engine. Queue/drop fields are owned by their queue implementation; protocol codec tests alone do not prove values are meaningful.

### MODEL_CTRL / storage

Downloads are worker-owned, user-initiated, single-flight, catalog-limited, SHA-256 verified, written to `.part`, then atomically renamed. Per-user model storage is used by default; configured/adjacent install paths participate in model/VAD discovery. Download failure must not activate unverified bytes.

## Error catalog

| Code | Meaning | Expected action |
| --- | --- | --- |
| `E_PROTOCOL_VERSION` | no compatible protocol | disable captions for item |
| `E_AUTH` | token/ACL failure | close local transport; no network fallback |
| `E_MODEL_MISSING` / `E_MODEL_INVALID` | absent or invalid model | disable caption session; playback continues |
| `E_AUDIO_FORMAT` | canonical PCM contract failed | disable caption session |
| `E_BACKPRESSURE` | audio discarded | continue with explicit drop accounting |
| `E_DISCONTINUITY` | timeline/source epoch changed | clear/resync caption epoch |
| `E_WORKER_CRASH` | worker/pipe failed | bounded recovery or disable captions |
| `E_SOURCE_OPEN` | native source decode unavailable | explicit safe fallback when allowed |
| `E_INTERNAL` | unclassified worker failure | fail session/process according to context |

## Worker CLI

```text
vlc-whisper-worker --pipe <path> --token <64_hex> [--model <path>]
  [--model-dir <path>] [--vad-model <path>]
  [--backend auto|gpu|cpu] [--gpu-device <id>]
  [--log-file <path>] [--enable-logging]
```

Required identity arguments reject overflow/truncation. `--log-file` implies diagnostics. VAD resolution prefers explicit path, then model-related/install/user candidates, then documented fallback behavior. Backend reporting uses actual inference outcome.

## Logging/privacy

Diagnostics are disabled by default. Enabled logs may contain bounded operational paths/IDs/counters but never authentication tokens, credentials, PCM, source subtitle bodies, or translated subtitle bodies.
