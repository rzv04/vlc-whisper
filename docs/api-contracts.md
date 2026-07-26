# Local Protocol Contracts

## Scope

This project has **no HTTP endpoints, cloud API, database, account, or authentication API**. “API” means the local versioned IPC protocol between the VLC integration and `vlc-whisper-worker.exe`.

All integers are unsigned/signed little-endian fixed-width fields. Text is strict UTF-8 without NUL terminators. The initial protocol is `major=1, minor=0`; a peer must reject unsupported major versions and may ignore optional fields added in a compatible minor version.

## Terminology & Abbreviations

| Term / Abbreviation | Definition |
|---|---|
| **PTS** | **Presentation TimeStamp** — The exact position on the media playback timeline (not wall-clock time) at which audio, video, or captions must be presented. |
| **`pts_us`** | **PTS in Microseconds** — Signed 64-bit integer (`int64_t`) representing PTS in microseconds ($1\text{ s} = 1,000,000\,\mu\text{s}$). |
| **`duration_us`** | **Duration in Microseconds** — Signed 64-bit integer (`int64_t`) representing duration in microseconds. |
| **IPC** | **Inter-Process Communication** — Local authenticated binary message-mode transport (named pipe on Windows, Unix domain socket on Linux). |

## Envelope

```c
struct vlcw_frame_header {
  uint32_t magic;          // 0x564C4357, 'VLCW'
  uint16_t major;          // 1
  uint16_t type;
  uint32_t payload_length; // <= 1,048,576
  uint64_t sequence;       // starts at 1, rises per sender/session
};
```

`payload_length` is checked before allocation. The receiver reads exactly one complete message from the message-oriented transport, checks header and payload schema, then acts. No JSON: binary avoids encoding ambiguity and makes PCM transfer efficient; a human-readable protocol trace tool can decode frames for development.

## Common fields

Every payload begins with `session_id[16]`, except initial `HELLO`. A string is `u16 byte_length` followed by bytes, maximum specified per field. PTS/duration values use `i64` microseconds; valid duration is greater than zero and no larger than 30 seconds per AUDIO message.

## Messages

### HELLO

Plugin to worker. Payload: `u16 min_major`, `u16 max_major`, `u8 token[32]`, `u16 client_version_len`, `client_version`. The worker must compare the token in constant time and reply within 3 seconds.

### HELLO_ACK

Worker to plugin. Payload: `u16 selected_major`, `u16 selected_minor`, `u32 capability_flags`, `u16 worker_version_len`, `worker_version`. Required flag `PCM_S16LE_16K_MONO`; optional flags include `PARTIAL_SEGMENTS` and `SEEK_RESET`.

### START

Plugin to worker. Payload: session ID, `i64 timeline_origin_pts_us`, `u32 sample_rate` (=16000), `u16 channels` (=1), `u16 sample_format` (=1, S16LE), model ID string (max 64), language string (`en`), and source-kind enum (`LOCAL_FILE=1`). `STARTED` either confirms effective settings or responds with `ERROR`.

### AUDIO

Plugin to worker. Payload: session ID, `i64 start_pts_us`, `i64 duration_us`, `u32 pcm_bytes`, then PCM. `pcm_bytes` must equal `duration_us * 16000 / 1_000_000 * 2`, subject only to explicitly documented whole-sample rounding. The worker must not infer from audio whose PTS overlaps an acknowledged discontinuity.

### SEGMENT

Worker to plugin. Payload: session ID, `u64 segment_id`, `i64 start_pts_us`, `i64 end_pts_us`, `u8 flags` (`bit0=final`, `bit1=replace` reserved), `u16 text_bytes`, UTF-8 text. Valid segments have `end_pts_us > start_pts_us`, text no longer than 1,024 bytes, and no control characters other than spaces/newlines allowed by the renderer.

Example semantic value, shown as JSON only for readability:

```json
{"session_id":"d4...","segment_id":42,"start_pts_us":12000000,"end_pts_us":14100000,"final":true,"text":"Example caption."}
```

### Control and status

`PAUSE`, `RESUME`, and `STOP` contain session ID plus a `u16 reason`; `STOP` is idempotent. `STATUS` contains state enum, queued audio microseconds, inference time microseconds, and dropped audio microseconds. `ERROR` contains session ID, stable error code, `u8 recoverable`, and a safe message capped at 256 UTF-8 bytes.

## Error catalog

| Code | Meaning | Plugin action |
|---|---|---|
| `E_PROTOCOL_VERSION` | No common major protocol | Disable captions for item; show compatibility diagnostic |
| `E_AUTH` | Pipe token/ACL validation failed | Close pipe; never fall back to network |
| `E_MODEL_MISSING` | Selected local model absent | Disable captions; explain local model requirement |
| `E_MODEL_INVALID` | Hash/load failed | Disable captions; retain playback |
| `E_AUDIO_FORMAT` | Canonical PCM cannot be produced | Disable captions |
| `E_BACKPRESSURE` | Audio was discarded | Continue; rate-limit diagnostic |
| `E_DISCONTINUITY` | Seek/rate/source timeline changed | Clear captions and end MVP session gracefully |
| `E_WORKER_CRASH` | Worker exit/pipe close | Clear captions; one bounded restart only before first audio, otherwise disable |
| `E_INTERNAL` | Unclassified worker failure | Disable captions; offer redacted diagnostics |

## Compatibility rules

Protocol changes that alter framing, time units, authentication, or message meaning require a major bump. Adding a bounded optional field or optional message requires a minor bump and capability flag. The worker and plugin must expose their protocol/build versions in diagnostics and reject accidental mixed-package installations.
