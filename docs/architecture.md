# Architecture

## Decision summary

VLC-whisper is an **ensemble**, not one plugin: a native C VLC capture/display integration and a separate local worker executable. The worker isolates inference failures and links whisper.cpp outside VLC, while the plugin remains responsible for real-time safety and timed caption presentation.

VLC's codebase is predominantly C and organizes most plugin code in `modules/`; its developer documentation warns that VLC evolves quickly. Build and package against an exact VLC version/commit, inspecting the matching source APIs rather than treating internal module interfaces as stable. [page:1][page:2]

## Components

```text
VLC decode pipeline
  | PCM frames + media PTS
  v
capture module (C, non-blocking producer)
  | bounded in-process SPSC queue
  v
plugin sender thread (14c, single send+receive: 5/20 ms cadence)
  | ---- local named pipe ---- worker.exe (C application)
                                        |
                                 worker IPC reader thread (14c, ADR-013)
                                        |
                                 worker frame queue (bounded, drop-oldest AUDIO)
                                        |
                                 worker session+inference main loop
                                        |
                                   whisper.cpp C API (Model-once ADR-015)
                                        |
caption receiver thread (step 15) -- timed segments --> caption presenter (C)
                                        |
                                   VLC subtitle/SPU/OSD path
```

| Component                     | Owns                                                           | Must not do                                                  |
| ----------------------------- | -------------------------------------------------------------- | ------------------------------------------------------------ |
| Capture module                | Audio-format validation, PTS mapping, bounded PCM enqueue      | Wait for worker, infer, write pipe, allocate per audio block |
| Plugin sender (14c, 15)       | Session handshake, PCM framing, queue drain, backpressure, drain worker SEGMENT/STATUS/ERROR, dispatch SEGMENT frames to the caption presenter | Block on inference; call VLC presentation API from the audio callback |
| Worker IPC Reader (14c, `ADR-013`) | Pipe frame reading, protocol validation, worker frame queue enqueue | Block on whisper.cpp inference or delay transport reading; send replies |
| Worker frame queue (14c)      | Bounded FIFO of `{type, payload}` frames; drop-oldest AUDIO; controls never evicted for audio; overflow evicts only PAUSE/RESUME, same-type, or the oldest non-SHUTDOWN control for a required incoming (START/STOP); a queued SHUTDOWN is never evicted by a non-SHUTDOWN incoming | Block; allocate unbounded |
| Worker Engine (`ADR-015`)     | Model-once lifetime, VAD/windowing, GPU/CPU inference, builder | Read arbitrary paths, expose network service, control VLC    |
| Caption receiver/presenter    | Validate worker messages, schedule/show/clear captions         | Trust malformed text/timestamps or block VLC playback        |
| Supervisor                    | Worker start/stop/restart policy and status                    | Restart endlessly or conceal a fatal compatibility error     |

## Time and buffering

All protocol times are signed 64-bit microseconds in the media timeline (`pts_us`), never wall-clock time. PCM is canonical 16 kHz, mono, signed 16-bit little-endian before it leaves the plugin; conversion belongs off the realtime callback if VLC cannot deliver it already.

Start with an 8-second analysis window, 2-second hop, and a hard 15-second audio backlog. These are configuration defaults, not compatibility guarantees. whisper.cpp offers a C-style API, VAD support, CPU-only operation, quantized models, and an example that repeatedly transcribes short real-time windows; its own stream example is described as naive, so overlap/deduplication and latency measurement are product work. [page:0]

### Audio chunk granularity

The plugin splits incoming PCM into fixed-size chunks of `VW_AUDIO_CHUNK_MAX_PCM_BYTES` (16384 bytes), which holds 8192 `int16_t` samples — **512 ms at 16 kHz**. This is ~8x headroom over a typical VLC audio block (up to 4096 frames at 48 kHz, yielding ~1365 samples after downsampling to 16 kHz). Chunks are stack-allocated inside the realtime callback and carry PCM inline to guarantee zero heap allocation (Rule 4).

The bounded SPSC queue defaults to **16 chunks** capacity. At 512 ms per chunk this provides an 8-second buffer capacity.

| Parameter                | Duration | In chunks (512 ms each) |
| ------------------------ | -------- | ----------------------- |
| Analysis window          | 8 s      | 16 chunks               |
| Hop                      | 2 s      | 4 chunks                |
| Backlog (queue capacity) | 8 s      | 16 chunks               |

Backpressure rule: playback wins. If the audio queue is full, discard the newest unprocessed audio, increment `audio_dropped_us`, emit a rate-limited warning, and continue. Never slow VLC. Captions after a gap may be missing; they must never be timestamped as if they were complete.

## Session state

```text
IDLE -> STARTING -> READY -> PLAYING <-> PAUSED -> STOPPING -> IDLE
                    |             |
                    v             v
                  FAILED <------ DISCONTINUITY (Epoch Reset / Re-sync)
```

A session is identified by a random 128-bit `session_id`; each playback start creates a new one. `sequence` is monotonic per direction. The plugin ignores stale session messages. A pause sends `PAUSE`, stops forwarding audio, and clears partial captions; final captions already scheduled may remain until their end PTS. Resume sends `RESUME`. Stop clears all generated captions before closing IPC.

MVP seeking & discontinuity policy: when a non-monotonic PTS, seek event (`BLOCK_FLAG_DISCONTINUITY`), rate change, or media swap occurs, the plugin clears active presenter captions, sends `STOP` (`SEEK_DISCONTINUITY`), resets the SPSC queue & VAD state, and initializes a new session epoch (`timeline_origin_pts_us`) seamlessly without disabling captions or interrupting VLC media playback.

## IPC protocol

Use a Windows **message-mode named pipe** with a random pipe name and a one-time 256-bit authentication token passed only on the worker command line/handle setup. Linux later maps the same framed byte protocol to a Unix-domain `SOCK_SEQPACKET` socket. Bind only locally; no TCP fallback.

### Transport Timeouts & Return Semantics

- **Connection Accept Timeout**: 10 seconds. `vw_ipc_listen()` waits up to 10s (`poll()` on POSIX, `WaitForSingleObject` on Win32) for an incoming plugin connection before closing the socket/pipe and self-terminating (returns `NULL`).
- **I/O Read/Write Timeout**: 3 seconds. `vw_ipc_receive()` and `vw_ipc_send()` enforce a 3-second timeout (`SO_RCVTIMEO`/`SO_SNDTIMEO` on POSIX, overlapped `WaitForSingleObject(3000)` on Win32).
- **Receive Return Semantics**: `vw_ipc_receive()` returns `> 0` for bytes read, `VW_IPC_RECV_TIMEOUT` (`-1`) on 3s read timeout (connection stays open; callers retry / keep waiting, e.g. during long video pauses), and `VW_IPC_RECV_FATAL` (`-2`) on fatal error or peer disconnect (EOF / broken pipe) — the handle is dead and the caller must abort.

Each frame is binary and little-endian:

```text
u32 magic = 0x564C4357  // VLCW
u16 protocol_major
u16 message_type
u32 payload_length       // 0..1,048,576; message-specific lower limits apply
u64 sequence
u8[payload_length] payload
```

Reject a wrong major version, unknown mandatory type, oversized payload, bad token, invalid UTF-8, impossible PTS range, non-monotonic sequence, or incorrect session ID. Close the connection and mark the session failed; do not retry unboundedly.

| Type                      | Direction                | Required payload                                                                       |
| ------------------------- | ------------------------ | -------------------------------------------------------------------------------------- |
| `HELLO` / `HELLO_ACK`     | both                     | version range, 32-byte token, capabilities                                             |
| `START` / `STARTED`       | plugin -> worker / reply | media identity hash (optional), audio format, model ID, language `en`, timeline origin |
| `AUDIO`                   | plugin -> worker         | session ID, `start_pts_us`, `duration_us`, PCM byte count, PCM bytes                   |
| `PAUSE`, `RESUME`, `STOP` | plugin -> worker         | session ID, reason where applicable                                                    |
| `SEGMENT`                 | worker -> plugin         | segment ID, start/end PTS, `final`, UTF-8 text, optional confidence                    |
| `STATUS`                  | worker -> plugin         | state, queue depth, inference latency, dropped audio                                   |
| `ERROR`                   | both                     | session ID, error code, recoverable flag, redacted message                             |

## Data model

The worker manages models; the plugin knows only a model ID string (`tiny.en`).

Incoming audio frames carry:

- `pcm_data`: Raw sample bytes (S16LE, FL32, or S32LE)
- `frame_count`: Number of audio frames in the block
- `pts_us`: Signed 64-bit microsecond PTS
- `sample_rate`: e.g., 44100, 48000, or 16000 Hz
- `channels`: e.g., 1 or 2

Converted SPSC queue chunks carry:

- `start_pts_us`: Signed 64-bit microsecond PTS
- `duration_us`: Duration of the chunk in microseconds
- `sample_rate`: 16000 Hz
- `channels`: 1 (Mono)
- `bytes`: Number of valid PCM bytes (up to 16384 bytes = 512 ms at 16 kHz S16LE)
- `pcm_data`: `int16_t` inline sample array (zero allocation)

Transcribed segments carry:

- `segment_id`: Monotonic 64-bit integer per session
- `start_pts_us` / `end_pts_us`: microsecond media timeline bounds
- `is_final`: Boolean flag
- `text_utf8`: Sanitized UTF-8 string

```text
[VLC audio block] ──> [vw_audio_capture_process_block] ──> [vw_audio_chunk_t (16k S16LE)]
                                                                      │
                                                             vw_spsc_queue_push
                                                                      │
                                                                      v
                                                             [vw_spsc_queue_t]
```

## Security, isolation, and limits

- Non-elevated: worker runs as the user running VLC.
- No network: local IPC only. Token authentication prevents unauthorized local processes from connecting.
- Resource limits: worker memory capped by single model model allocation (~39 MB for `tiny.en`). Worker CPU thread count capped by configuration (default 2 threads; graph compute uses ggml's pthread-based threadpool — on Windows OpenMP is disabled at build time so the worker exe stays free of MinGW runtime DLLs, ADR-010).
- Audio buffer limit: plugin drops audio chunks when the queue reaches 16 chunks (8 s capacity) rather than consuming unbounded memory.
- Input bounds: header payload length strictly capped at 1 MB. Malformed UTF-8 text or impossible PTS values are rejected.
- Caption queueing: plugin maintains no internal caption queue (ADR-016). Timed subpictures are submitted directly to VLC's native SPU pipeline (`vout_PutSubpicture`), which manages PTS display scheduling.
