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
IPC sender thread ---- local named pipe ---- worker.exe (C application)
                                                  |
                                             whisper.cpp C API
                                                  |
caption receiver thread -- timed segments --> caption presenter (C)
                                                  |
                                             VLC subtitle/OSD path
```

| Component | Owns | Must not do |
|---|---|---|
| Capture module | Audio-format validation, PTS mapping, bounded PCM enqueue | Wait for worker, infer, write pipe, allocate per audio block |
| IPC sender | Session handshake, PCM framing, queue drain, backpressure | Call VLC presentation API |
| Worker | Model lifetime, VAD/windowing, inference, segment deduplication | Read arbitrary paths, expose network service, control VLC |
| Caption receiver/presenter | Validate worker messages, schedule/show/clear captions | Trust malformed text/timestamps or block VLC playback |
| Supervisor | Worker start/stop/restart policy and status | Restart endlessly or conceal a fatal compatibility error |

**Open technical spike:** prove the exact VLC module combination that can both observe decoded PCM and inject properly timed text on the pinned Windows VLC. Do this before committing to an out-of-tree distribution strategy. If native timed subtitle injection cannot be made reliable, the temporary MVP fallback is a video-overlay/OSD renderer; it is not equivalent to a selectable native subtitle track.

## Time and buffering

All protocol times are signed 64-bit microseconds in the media timeline (`pts_us`), never wall-clock time. PCM is canonical 16 kHz, mono, signed 16-bit little-endian before it leaves the plugin; conversion belongs off the realtime callback if VLC cannot deliver it already.

Start with an 8-second analysis window, 2-second hop, and a hard 15-second audio backlog. These are configuration defaults, not compatibility guarantees. whisper.cpp offers a C-style API, VAD support, CPU-only operation, quantized models, and an example that repeatedly transcribes short real-time windows; its own stream example is described as naive, so overlap/deduplication and latency measurement are product work. [page:0]

Backpressure rule: playback wins. If the audio queue is full, discard the oldest unprocessed audio, increment `audio_dropped_us`, emit a rate-limited warning, and continue. Never slow VLC. Captions after a gap may be missing; they must never be timestamped as if they were complete.

## Session state

```text
IDLE -> STARTING -> READY -> PLAYING <-> PAUSED -> STOPPING -> IDLE
                    |             |
                    v             v
                  FAILED <------ DISCONTINUITY (MVP: fail/disable session)
```

A session is identified by a random 128-bit `session_id`; each playback start creates a new one. `sequence` is monotonic per direction. The plugin ignores stale session messages. A pause sends `PAUSE`, stops forwarding audio, and clears partial captions; final captions already scheduled may remain until their end PTS. Resume sends `RESUME`. Stop clears all generated captions before closing IPC.

MVP discontinuity policy: detect a non-monotonic PTS, seek event, rate change, source replacement, or title change; clear generated captions, send `STOP`, transition to `FAILED`/disabled for that item, show one local diagnostic, and leave VLC playback intact. This is deliberate graceful degradation, not a crash.

## IPC protocol

Use a Windows **message-mode named pipe** with a random pipe name and a one-time 256-bit capability token passed only on the worker command line/handle setup. Linux later maps the same framed byte protocol to a Unix-domain `SOCK_SEQPACKET` socket. Bind only locally; no TCP fallback.

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

| Type | Direction | Required payload |
|---|---|---|
| `HELLO` / `HELLO_ACK` | both | version range, session ID, 32-byte token, capabilities |
| `START` / `STARTED` | plugin -> worker / reply | media identity hash (optional), audio format, model ID, language `en`, timeline origin |
| `AUDIO` | plugin -> worker | session ID, `start_pts_us`, `duration_us`, PCM byte count, PCM bytes |
| `PAUSE`, `RESUME`, `STOP` | plugin -> worker | session ID, reason where applicable |
| `SEGMENT` | worker -> plugin | segment ID, start/end PTS, `final`, UTF-8 text, optional confidence |
| `STATUS` | worker -> plugin | state, queue depth, inference latency, dropped audio |
| `ERROR` | both | stable code, recoverability, safe diagnostic text |
| `PING` / `PONG` | both | nonce |

The worker emits only final segments in MVP. Partial/revision messages are reserved for protocol major 1 now so seek/live support does not force a new transport; the presenter may ignore them safely.

## Data model

```c
typedef struct { uint8_t bytes[16]; } vlcw_session_id;
typedef struct { int64_t start_pts_us, end_pts_us; uint64_t id; bool final; char *utf8; } vlcw_caption_segment;
typedef struct { int64_t start_pts_us, duration_us; uint32_t sample_rate, channels; uint32_t bytes; } vlcw_audio_chunk;
```

Text is UTF-8, normalized only as required for display, with a conservative maximum of 1,024 bytes per segment. Segment IDs are worker-monotonic within a session. The plugin keeps only a small time-ordered caption cache, e.g. 60 seconds, and never persists audio or transcript in MVP.

## Security and privacy

There is no user auth because there is no user-facing service or remote API. The security boundary is local process-to-process authority: create the pipe with current-user-only ACLs, use a random name/token, launch the worker with a fixed executable path, and validate every message. Do not accept a pre-existing worker connection.

Treat model files and worker binaries as trusted package inputs: verify manifest hashes at install/startup and reject paths outside the application data directory. Avoid logging audio, raw transcript, full media path, or command lines containing the capability token.

## Deployment

Package a versioned set: plugin DLL(s), worker EXE, required runtime DLLs, a pinned model manifest, licenses/notices, and installer/uninstaller. Models are optional separate local assets because sizes are material: whisper.cpp lists roughly 75 MiB disk/273 MB memory for tiny and 2.9 GiB disk/3.9 GB memory for large, which makes “choose any model” a real storage/RAM and performance commitment. [page:0]

The supported matrix must name: Windows build, x64 architecture, VLC exact version/build hash, plugin ABI/build toolchain, worker version, whisper.cpp commit, model hash, and protocol version. The Linux port uses the same C core and protocol but gets its own packaging and test matrix.

## Observability

Default diagnostics are local, opt-in, and redacted. Emit structured event IDs and counters: worker launch time, handshake result, queue high-water mark, dropped audio duration, inference duration, caption latency (`display_pts - segment_end_pts`), protocol rejection, and session end reason. A user can export a diagnostic bundle containing versions and counters, never PCM or caption text unless they explicitly choose a separate future debug mode.

## Testing strategy

- Unit-test queues, framing, parser bounds, timestamp arithmetic, caption deduplication, and state transitions in native C.
- Contract-test plugin and worker independently from golden binary frames; fuzz the frame decoder and malformed UTF-8/lengths.
- Run worker integration tests with deterministic PCM fixtures and a pinned tiny.en model.
- Run Windows end-to-end tests in an actual VLC installation: plugin load, local fixture playback, pause/resume, end, worker missing, malformed worker, and seek rejection.
- Measure real-time factor and caption latency on a declared reference PC; do not call hardware “decent” without an explicit benchmark profile.
