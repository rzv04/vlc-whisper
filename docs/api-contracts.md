# Local Protocol Contracts

## Scope

This project has **no HTTP endpoints, cloud API, database, account, or authentication API**. “API” means the local versioned IPC protocol between the VLC integration and `vlc-whisper-worker.exe`.

All integers are unsigned/signed little-endian fixed-width fields. Text is strict UTF-8 without NUL terminators. The current protocol is `major=1, minor=4` (Protocol v1.4); a peer must reject unsupported major versions and may ignore optional fields added in a compatible minor version.

## Transport Timeouts & Guarantees

- **Accept Connection Timeout**: 10 seconds (`vw_ipc_listen()` waits up to 10,000 ms for plugin connection, returning `NULL` on timeout).
- **Frame Read / Write Timeout**: 3 seconds (`vw_ipc_receive()` and `vw_ipc_send()` enforce 3,000 ms timeout per I/O call on both POSIX and Win32). Custom read timeouts can be specified via `vw_ipc_receive_timeout(handle, buffer, size, timeout_us)`.
- **Receive Return Semantics**: `vw_ipc_receive()` and `vw_ipc_receive_timeout()` return `> 0` for bytes read, `-1` (`VW_IPC_RECV_TIMEOUT`) for a read timeout (connection remains open during video pause; the receiver should retry/keep waiting), and `-2` (`VW_IPC_RECV_FATAL`) for a fatal error or peer disconnect (EOF / broken pipe), after which the handle must be treated as dead.

## Terminology & Abbreviations

| Term / Abbreviation | Definition                                                                                                                                                 |
| ------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **PTS**             | **Presentation TimeStamp** — The exact position on the media playback timeline (not wall-clock time) at which audio, video, or captions must be presented. |
| **`pts_us`**        | **PTS in Microseconds** — Signed 64-bit integer (`int64_t`) representing PTS in microseconds ($1\text{ s} = 1,000,000\,\mu\text{s}$). |
| **`duration_us`**   | **Duration in Microseconds** — Signed 64-bit integer (`int64_t`) representing duration in microseconds. |
| **IPC**             | **Inter-Process Communication** — Local authenticated binary message-mode transport (named pipe on Windows, Unix domain socket on Linux).                  |

> **Wire `pts_us` domain (v1.1):**
> - In **Live Streaming Mode** (or live IPTV), AUDIO chunk timestamps are stamped by the plugin from VLC's audio-filter block PTS in the system-date domain.
> - In **Look-Ahead Source Mode** (v1.1), `start_pts_us` and `end_pts_us` are media-relative PTS timestamps decoded directly by the native demuxer. The plugin translates them to the SPU presentation time using the sampled `input_time_us` from VLC (`start_tick = mdate() + (start_pts_us - input_time_us)`).
> - The plugin explicitly marks whether a segment uses the media timeline. Live mode anchors finalized cues at `mdate()` and must never subtract `INPUT_GET_TIME` media position from a live system-date segment PTS. In source mode, position zero is valid; only `-1` means unavailable.

## Envelope

```c
struct vlcw_frame_header {
  uint32_t magic;          // 0x564C4357, 'VLCW'
  uint16_t major;          // 1
  vw_message_type_t type;
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

Worker to plugin. Payload: `u16 selected_major`, `u16 selected_minor`, `u32 capability_flags`, `u16 worker_version_len`, `worker_version`. Required flag `PCM_S16LE_16K_MONO` (`1U << 0`); optional flags include `PARTIAL_SEGMENTS` (`1U << 1`), `SEEK_RESET` (`1U << 2`), and `SOURCE_MODE` (`VW_CAPABILITY_SOURCE_MODE = 1U << 3`).

### START

Plugin to worker. Payload: session ID, `i64 timeline_origin_pts_us`, `u32 sample_rate` (=16000), `u16 channels` (=1), `u16 sample_format` (=1, S16LE), model ID string (max 64), language string (`en`), source-kind enum (`LOCAL_FILE=1`, `LIVE_AUDIO=0`), and optional `u16 source_url_len`, `char source_url[1024]`. `STARTED` either confirms effective settings or responds with `ERROR`.

### POSITION (v1.1)

Plugin to worker. Payload: session ID, `i64 current_pts_us`, `i64 input_time_us`, `float playback_rate`, `u32 flags` (`VW_POSITION_FLAG_SEEK = 1`, `VW_POSITION_FLAG_PAUSED = 2`). Paces worker look-ahead decoding (30s horizon) and handles seeks without session teardown.

### AUDIO

Plugin to worker. Payload: session ID, `i64 start_pts_us`, `i64 duration_us`, `u32 pcm_bytes`, then PCM. `pcm_bytes` must equal `duration_us * 16000 / 1_000_000 * 2`, subject only to explicitly documented whole-sample rounding: the receiver accepts ±1 byte of the computed value (half a sample at 16 kHz S16LE = 1 byte), since producers may round the duration up or down for odd-length partial blocks. The worker must not infer from audio whose PTS overlaps an acknowledged discontinuity.

### SEGMENT

Worker to plugin. Payload: session ID, `u64 segment_id`, `i64 start_pts_us`, `i64 end_pts_us`, `bool is_final`, `u16 text_bytes`, UTF-8 text. Valid segments have `end_pts_us > start_pts_us`, text no longer than 1,024 bytes, and no control characters other than spaces/newlines allowed by the renderer.

- **Phrase-by-Phrase Timing (`ADR-017`)**: Segment timing is derived from internal Whisper sub-segment boundaries ($t_0, t_1$ in centiseconds scaled by `10000LL` to microsecond PTS), rather than coarse 8-second window spans.
- **`is_final` Invariant**: `is_final == true` denotes an immutable, committed subtitle cue to be rendered on screen via SPU (`vout_PutSubpicture`). Uncommitted/in-flight hypotheses are held until their window onset is finalized or drained.
- **Silence Screen Blanking**: Non-contiguous phrases (e.g. 0.6s pause between speakers) generate distinct non-overlapping subpictures, naturally blanking the screen during conversational pauses.

Example semantic value, shown as JSON only for readability:

```json
{
  "session_id": "d4...",
  "segment_id": 42,
  "start_pts_us": 12000000,
  "end_pts_us": 14100000,
  "final": true,
  "text": "Example caption."
}
```

### STARTED (v1.2)

Worker to plugin. Payload: `u8 source_active` (`VW_SOURCE_ACTIVE_ACTIVE = 1` if source file look-ahead mode initialized successfully; `VW_SOURCE_ACTIVE_INACTIVE = 0` if live streaming mode). Confirms session initialization and effective settings after `START`.

### CONTROL MESSAGES (`PAUSE`, `RESUME`, `STOP`)

Plugin to worker. Payload: session ID, `u16 reason`.

- `PAUSE`: Suspends active transcription processing and clears the in-flight analysis window (a fresh window starts on `RESUME`); the session timeline/epoch is preserved. Reason codes: `USER_PAUSE=1`.
- `RESUME`: Resumes active transcription processing after pause. Reason codes: `USER_RESUME=1`.
- `STOP`: Terminates active captioning session, clears buffers, and resets VAD state. Reason codes: `USER_STOP=1`, `SEEK_DISCONTINUITY=2` (`VW_CTRL_REASON_SEEK_DISCONTINUITY`, sent on seek/discontinuity before a fresh `START` epoch), `MEDIA_END=3`. **Idempotent**: Calling `STOP` multiple times or on an idle session is a safe no-op.

### SHUTDOWN

Plugin to worker. Payload: Empty (header only). Instructs worker to close transport handles and exit process cleanly with code `0`.

### STATUS (v1.3)

Worker to plugin. Payload: session ID, `u32 state`, `i64 queued_audio_us`, `i64 inference_us`, `i64 dropped_audio_us`, `char resolved_backend[16]` — 60 bytes on the wire in v1.3.

- `resolved_backend`: NUL-padded `"gpu"` or `"cpu"` — the backend **actually used for inference**, not the requested one. A Vulkan-enabled worker in `auto`/`gpu` mode without a usable GPU/IGPU device transparently falls back to CPU at runtime (whisper.cpp behavior) and MUST report `"cpu"`. The plugin mirrors this value into the read-only `whisper-backend-active` config var, which the settings GUI displays.
- Emission: one `STATUS` is sent immediately after every `STARTED` reply carrying the resolved backend for the fresh session; further `STATUS` frames are emitted after inference work. `inference_us` is cumulative wall time spent inside `whisper_full()` for the worker lifetime, excluding VAD, segment filtering, and IPC presentation.
- Compatibility: v1.2 (44-byte) STATUS payloads remain decodable — a v1.3 decoder zero-fills the missing tail, yielding an empty `resolved_backend`; a v1.3 encoder always writes the full 60-byte payload. Same major version, so no capability flag is required (both peers ship together).

### MODEL_CTRL (v1.4)

Plugin to worker. Payload 49 bytes: session ID, `u8 action` (`DOWNLOAD=1`, `ABORT=2`), `char model_id[32]` (NUL-padded catalog id: `tiny.en|tiny|base.en|base|small|medium|large`; ignored for `ABORT`) — 49 bytes on the wire.

- Semantics: user-initiated model fetch. This is worker-scoped, so a zero session ID is valid when `START` was
  rejected because the selected model is missing. The worker downloads the requested catalog model to the per-user
  directory, streaming sha256 verification against the committed catalog (`worker/include/vw_model_catalog.h`),
  writing to `.part` and atomically renaming on success. Single-flight: a second `DOWNLOAD` while active yields an
  immediate `MODEL_PROGRESS` `FAILED` response. Unknown `model_id` → `MODEL_PROGRESS` `FAILED`. `ABORT` cancels
  the download thread and removes its partial file; worker shutdown or IPC disconnect performs the same cleanup.

### MODEL_PROGRESS (v1.4)

Worker to plugin. Payload 66 bytes: session ID, `u8 stage` (`IDLE=0`, `DOWNLOADING=1`, `VERIFYING=2`, `DONE=3`, `FAILED=4`, `ABORTING=5`), `u8 pct` (0–100), `u64 bytes_done`, `u64 bytes_total`, `char model_id[32]` (NUL-padded) — 66 bytes on the wire.

- Emission: at least 1 Hz while a download is active and on every stage transition (`IDLE` → `DOWNLOADING` → `VERIFYING` → `DONE`/`FAILED`, `ABORTING` → `IDLE`). The initial `IDLE` snapshot is informational, not a terminal result: the plugin must retain the matching pending command until `DONE`, `FAILED`, `ABORTING`, worker shutdown, or transport death. Terminal `FAILED` frames may report `bytes_total = 0` when failure occurs before catalog or destination inspection. Plugin mirrors fields into the read-only config vars `whisper-model-progress` (pct) and `whisper-model-status` (`"<stage>:<model_id>"`) and renders progress through a dedicated C presenter SPU channel. Lua only submits commands; it does not poll, sleep, or refresh the dialog in a loop, so playback pause does not pause downloading.

### Model storage

Models are stored per-user: `%LOCALAPPDATA%\vlc-whisper\models` on Windows, `$XDG_DATA_HOME/vlc-whisper/models` (`$HOME/.local/share/vlc-whisper/models` fallback) on Linux; `--model-dir` overrides. Downloads write to `<dest>/<filename>.part` with streaming sha256 and are atomically renamed on verified success (`MoveFileExW` / `rename`). Each destination is protected by an OS-level interprocess lock for the transfer lifetime; worker startup never deletes another worker's partial file. Resolve order: explicit `model-path` config → install `models/` directory → per-user directory. At worker startup, an existing configured path wins; when a relative configured path is absent, its filename is also tried under `--model-dir`, allowing a downloaded catalog model to load after a worker restart. VAD resolution occurs after model resolution: explicit `--vad-model`, a sibling of the effective model, `--model-dir`, the worker executable's adjacent `models/` directory (for example, `<VLC>\models` on Windows), then legacy working-directory candidates. The executable-directory probe uses the process image path rather than the launcher's current directory, so IPTVnator or another launcher cannot hide the bundled VAD by changing CWD. Network policy: see ADR-023 — egress is worker-only, explicit, pinned-URL, and hash-verified.

Diagnostic logging is disabled by default. When enabled by the `whisper-logging` config key, the worker records model-download diagnostics in `%TEMP%\vlc-whisper-worker.log` on Windows (or the platform temp directory), and the plugin emits events through VLC Messages. These diagnostics may include bounded local paths and byte counters, but never auth tokens, PCM, transcripts, or network credentials.

Worker startup also emits `WORKER_VAD_RESOLVE` diagnostics: the worker current directory, effective Whisper model path, configured model directory, explicit VAD override, every VAD candidate and hit/miss result, and the selected path or RMS fallback. This is intended to diagnose launcher-dependent path issues without logging media content or secrets.

### ERROR

Bi-directional (primarily Worker to Plugin). Payload: session ID, `u32 error_code`, `u8 recoverable`, `char message[256]` (safe redacted UTF-8 message). The message content is at most 255 bytes and MUST carry its own NUL terminator within the fixed-size field: the encoder rejects unterminated strings, and the decoder force-terminates only payloads that contain no NUL anywhere (defensive; never emitted by a conforming peer).

- If `recoverable == 0`: Fatal failure. Plugin disables captions for item, closes transport; VLC media playback continues uninterrupted.
- If `recoverable == 1`: Non-fatal warning (e.g. `E_BACKPRESSURE`, `E_SOURCE_OPEN`); plugin logs diagnostic, session continues.

## Error catalog

| Code                 | Meaning                           | Plugin action                                                                  |
| -------------------- | --------------------------------- | ------------------------------------------------------------------------------ |
| `E_PROTOCOL_VERSION` | No common major protocol          | Disable captions for item; show compatibility diagnostic                       |
| `E_AUTH`             | Pipe token/ACL validation failed  | Close pipe; never fall back to network                                         |
| `E_MODEL_MISSING`    | Selected local model absent       | Disable captions; explain local model requirement                              |
| `E_MODEL_INVALID`    | Hash/load failed                  | Disable captions; retain playback                                              |
| `E_AUDIO_FORMAT`     | Canonical PCM cannot be produced  | Disable captions                                                               |
| `E_BACKPRESSURE`     | Audio was discarded               | Continue; rate-limit diagnostic                                                |
| `E_DISCONTINUITY`    | Seek/rate/source timeline changed | Clear captions and end MVP session gracefully                                  |
| `E_WORKER_CRASH`     | Worker exit/pipe close            | Clear captions; one bounded restart only before first audio, otherwise disable |
| `E_INTERNAL`         | Unclassified worker failure       | Disable captions; offer redacted diagnostics                                   |
| `E_SOURCE_OPEN`      | Native source demuxer open failed | Non-fatal; plugin falls back transparently to live PCM stream capture          |

## Worker CLI Contracts

The worker executable (`vlc-whisper-worker.exe` / `vlc-whisper-worker`) is spawned by the plugin or launched manually during testing with the following command-line interface:

```text
vlc-whisper-worker --pipe <path> --token <64_hex_chars> [--model <model_path>] [--vad-model <vad_path>] [--backend auto|gpu|cpu] [--gpu-device <id>] [--log-file <log_path>] [--enable-logging]
```

| Parameter | Required | Default | Description |
|---|---|---|---|
| `--pipe <path>` | Yes | (none) | Named pipe name (Win32) or Unix domain socket path (POSIX). |
| `--token <64_hex>` | Yes | (none) | 32-byte secret authentication token in 64 hexadecimal characters. |
| `--model <path>` | No | bundled `models/ggml-tiny.bin` | Path to Whisper GGML model file. |
| `--vad-model <path>` | No | (auto-discovered) | Path to Silero VAD GGML model (`ggml-silero-vad.bin`). Explicit value wins; otherwise, after Whisper model resolution, the worker probes its sibling, `--model-dir`, the worker executable's adjacent `models/` directory, then legacy working-directory candidates. If absent, it gracefully falls back to RMS Energy VAD. |
| `--backend <type>` | No | `auto` | Inference accelerator backend: `auto`, `gpu`, or `cpu`. |
| `--gpu-device <id>` | No | `0` | GPU/IGPU device index for hardware acceleration. |
| `--log-file <path>` | No | disabled | Custom destination for diagnostic log output; implies `--enable-logging`. |
| `--enable-logging` | No | disabled | Enables worker diagnostic logging and its temp-file output. |
| `--model-dir <path>` | No | per-user model directory | Destination for explicit catalog downloads; creates the directory on demand. |

## Compatibility rules

Protocol changes that alter framing, time units, authentication, or message meaning require a major bump. Adding a bounded optional field or optional message requires a minor bump and capability flag. The worker and plugin must expose their protocol/build versions in diagnostics and reject accidental mixed-package installations.
