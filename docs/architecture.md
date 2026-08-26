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

| Component                          | Owns                                                                                                                                                                                                                                                                                | Must not do                                                             |
| ---------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| Capture module                     | Audio-format validation, PTS mapping, bounded PCM enqueue                                                                                                                                                                                                                           | Wait for worker, infer, write pipe, allocate per audio block            |
| Plugin sender (14c, 15, 16)        | Session handshake, PCM framing, queue drain, backpressure, drain worker SEGMENT/STATUS/ERROR, dispatch SEGMENT frames to the caption presenter, poll input pause state and send PAUSE/RESUME                                                                                        | Block on inference; call VLC presentation API from the audio callback   |
| Worker IPC Reader (14c, `ADR-013`) | Pipe frame reading, protocol validation, worker frame queue enqueue                                                                                                                                                                                                                 | Block on whisper.cpp inference or delay transport reading; send replies |
| Worker frame queue (14c)           | Bounded FIFO of `{type, payload}` frames; drop-oldest AUDIO; controls never evicted for audio; overflow evicts only PAUSE/RESUME, same-type, or the oldest non-SHUTDOWN control for a required incoming (START/STOP); a queued SHUTDOWN is never evicted by a non-SHUTDOWN incoming | Block; allocate unbounded                                               |
| Worker Engine (`ADR-015`, 17a)     | Model-once lifetime, VAD/windowing, GPU/CPU inference, builder; Vulkan backend default ON with transparent CPU fallback (`--backend auto|gpu|cpu`, `--gpu-device`)                                                                                                                    | Read arbitrary paths, expose network service, control VLC               |
| Caption receiver/presenter         | Validate worker messages, schedule/show/clear captions                                                                                                                                                                                                                              | Trust malformed text/timestamps or block VLC playback                   |
| Supervisor                         | Worker start/stop/restart policy and status                                                                                                                                                                                                                                         | Restart endlessly or conceal a fatal compatibility error                |

## Time and buffering

All protocol times are signed 64-bit microseconds (`pts_us`). VLC 3.0's audio output stamps audio-filter block PTS in the **system-date domain** (µs since boot on Windows; `aout_DecPlay` compares block PTS against `mdate()`), so the wire carries that domain. The caption presenter schedules SPU subpictures in the OSD clock domain (`i_start = mdate()`) — the clock the 3.0.23 Windows build renders filter-pushed subpictures against; media-domain scheduling (`b_subtitle = true`, picture-PTS clock) is the 17c target, currently blocked on the subtitle clock silently dropping such subpictures (evidence chain in `docs/plans/step17b_plan.md` §3 and `docs/vlc-api-essentials.md` §7). PCM is canonical 16 kHz, mono, signed 16-bit little-endian before it leaves the plugin; conversion belongs off the realtime callback if VLC cannot deliver it already.

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

Seeking & discontinuity policy (shipped in step 17; hardened in steps 17c & 17d):
- In **Live Streaming Mode**, when a non-monotonic PTS, seek event (`BLOCK_FLAG_DISCONTINUITY`), rate change, or media swap occurs, the plugin clears active presenter captions, sends `STOP` (`SEEK_DISCONTINUITY`), discards the SPSC queue, and starts a new session epoch seamlessly without disabling captions or interrupting VLC media playback.
- In **Look-Ahead Source Mode (Steps 17c & 17d)**, the worker natively decodes the local source file ahead of the playhead (maintaining a 30s horizon). The plugin extracts the media URL (`input_item_GetURI`) and periodically sends `POSITION` messages to pace worker decoding. On seek events, the plugin transmits a `POSITION` message with `VW_POSITION_FLAG_SEEK`; the worker repositions its native demuxer (`IMFSourceReader::SetCurrentPosition` on Windows / `av_seek_frame` on Linux) and clears in-flight hypotheses without tearing down the worker process or IPC pipe.
- **5-Second Clock Jump Gate (Step 17d)**: In both the realtime audio callback and throttled position-poll detectors, forward timeline jumps are gated by `VW_INPUT_JUMP_DISCONTINUITY_US = 5000000LL` (5.0s). Minor network transport jitter, packet slips, and re-buffering ($|\Delta\text{PTS}| < 5\text{s}$) are suppressed to prevent false-positive caption dropouts, while backward jumps ($> 500\text{ms}$) and true macroscopic seeks ($\ge 5\text{s}$) trigger instant seek re-sync and SPU channel flushing. Live PCM capture and IPC streaming are gated when source mode is confirmed active via `VW_MSG_STARTED` (`source_active = 1`).
- **Phrase-by-Phrase Timing & Segmentation (Step 17d.1, `ADR-017`, `ADR-018`)**: Whisper's internal sub-segments ($t_0, t_1$ centiseconds scaled by `10000LL`) are extracted directly instead of concatenating the entire 8-second window. A decoupled `vw_segment_builder` maintains a 16-slot committed history ring buffer across 2s window hops, deduplicating candidate phrases within $500\,\text{ms}$ tolerance. Discrete non-overlapping SPU subpictures are scheduled with `i_start = mdate() + lead_us` and `i_stop = i_start + dur_us / rate`, naturally blanking the screen during conversational pauses (e.g. 0.6s gap) for a natural PotPlayer / Netflix visual cadence.
- **Multi-Tier Voice Activity Detection & Silence Gating (Step 17e.1, `ADR-019`)**:
  - **Tier 1 (Pre-Inference VAD)**: Employs vendored Silero GGML VAD (`whisper_vad_detect_speech` / `whisper_vad_segments_from_probs`) across all worker audio ingestion paths. Auto-discovers `ggml-silero-vad.bin` in the model directory alongside the selected catalog model or via CLI `--vad-model <path>`, gracefully falling back to zero-config RMS Energy VAD (`0.01f`) if absent. Silent/music windows skip Whisper inference completely, cutting idle CPU/GPU usage by up to 80%.
  - **Tier 2 (Post-Inference Acoustic Confidence Gating)**: Configures `wparams.no_speech_thold = 0.60f` and `wparams.suppress_nst = true`. Evaluates `whisper_full_get_segment_no_speech_prob` and discards sub-segments with $P(\text{no\_speech}) \ge 0.60$ for mixed speech/silence windows.
  - **Tier 3 (Formatting & Non-Speech Tag Cleanliness)**: Encapsulated in `vw_hallucination_filter.c`. Strips standalone non-speech descriptors (`[Music]`, `(applause)`, `♪`, `♫`, etc.) and isolated punctuation (`...`, `---`) with zero alphanumeric characters, preserving 100% of spoken words and sentence punctuation.
  - **Discontinuity LSTM Resets**: `whisper_vad_reset_state()` clears recurrent cell and hidden states on seek, pause, resume, and epoch restarts.
- **VAD-Guided Non-Overlapping Audio Chunking (Step 17e.1 No-Hop, `ADR-020`)**:
  - In **Lookahead Source Mode**, replaces fixed 2-second sliding hops with dynamic VAD-guided non-overlapping audio chunking (`vw_vad_find_chunk_boundary`). Audio is partitioned along natural conversational pauses ($\ge 300\text{ms}$ silence gap between sentences) bounded between $6.0\text{s}$ and $24.0\text{s}$ ($150\text{ms}$ acoustic padding).
  - Each speech chunk is transcribed **exactly once** and drained 100% without overlap, eliminating duplicate/stuttering subtitles, mid-word clipping, and cross-hop timestamp jitter while reducing worker compute by 75%. Leading or all-silence intervals are drained with zero Whisper calls. Live streaming mode retains low-latency sliding windows for real-time responsiveness.
- **Subtitle Reading Floor & Decoding Optimization (Step 17e.2, `ADR-021`)**:
  - In `vw_caption_presenter.c`, enforces `VW_CAPTION_MIN_DISPLAY_DURATION_US = 1000000LL` (1.0s) rate-scaled wall-clock minimum display floor ($\text{duration\_us} = \max(\text{raw\_dur}, \lfloor 1000000 \times \text{rate} \rfloor)$), eliminating unreadable sub-second flash cues while preserving authentic acoustic timing for long phrases.
  - In `vw_whisper_engine.c`, configures deterministic greedy decoding (`strategy = GREEDY`, `temperature = 0.0f`, `temperature_inc = 0.2f`, `entropy_thold = 2.40f`, `no_context = true`, `suppress_nst = true`), isolating audio windows and preventing latency spikes or hallucination cascades.

## IPC protocol

Use a Windows **message-mode named pipe** with a random pipe name and a one-time 256-bit authentication token passed only on the worker command line/handle setup. Linux maps the same framed byte protocol to a Unix-domain socket. Bind only locally; no TCP fallback.

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
| `HELLO` / `HELLO_ACK`     | both                     | version range, 32-byte token, capabilities (`VW_CAPABILITY_SOURCE_MODE`)               |
| `START` / `STARTED`       | plugin -> worker / reply | media identity hash (optional), audio format, model ID, language `en`, timeline origin, optional `source_url` |
| `POSITION`                | plugin -> worker         | session ID, `current_pts_us`, `input_time_us`, `playback_rate`, `flags` (SEEK/PAUSED)  |
| `AUDIO`                   | plugin -> worker         | session ID, `start_pts_us`, `duration_us`, PCM byte count, PCM bytes                   |
| `PAUSE`, `RESUME`, `STOP` | plugin -> worker         | session ID, reason where applicable                                                    |
| `SEGMENT`                 | worker -> plugin         | segment ID, start/end PTS, `final`, UTF-8 text, optional confidence                    |
| `STATUS`                  | worker -> plugin         | state, queue depth, inference latency, dropped audio                                   |
| `ERROR`                   | both                     | session ID, error code, recoverable flag, redacted message                             |
| `MODEL_CTRL`              | plugin -> worker         | worker-scoped download/abort request; zero session ID is valid before `START`          |
| `MODEL_PROGRESS`          | worker -> plugin         | download stage, percent, byte counters, catalog model ID                             |

## Data model

The worker manages catalog models. With no explicit user selection, resolution prioritizes the bundled multilingual
`ggml-tiny.bin`; an explicit `model-path` selection takes precedence. Lazy downloads use the per-user model
directory, so the plugin never performs network I/O.

The Lua settings dialog may perform bounded local existence checks for a selected catalog filename in the bundled
`models/` directory and the per-user download directory. It does not hash large files on VLC's UI thread; the worker
remains responsible for SHA-256 verification during download. VLC 3.0's Lua widgets expose neither dropdown-change
callbacks nor button enabled/disabled state, so `.en` language enforcement occurs on Apply while model availability
presentation is refreshed by dialog construction and bounded action callbacks. The full language list remains visible
while a model is being selected because Lua cannot react to dropdown changes.

`MODEL_PROGRESS(IDLE)` is an initial state snapshot emitted before the worker's asynchronous downloader changes to
`DOWNLOADING`; it is not a failed or completed command. The plugin sender keeps the pending catalog-id correlation
through that snapshot and activates the exact verified per-user path only after `DONE`. The worker writes diagnostic
events to its temp log, while the plugin mirrors bounded progress and lifecycle events to VLC Messages. On startup,
the worker first uses an existing configured model path; if that relative path is absent, it tries the same filename
under `--model-dir` so a verified per-user download is loadable by the next worker instance.

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
- **Network boundary:** normal captioning is local-only. A user-initiated `MODEL_CTRL` is the sole documented
  exception: the worker downloads one pinned catalog model, verifies SHA-256, and atomically installs it in the
  per-user model directory. Lua and the plugin remain network-free; see ADR-023.
- Resource limits: worker memory is capped by the selected single-model allocation (the bundled `tiny` model is the
  default). Worker CPU thread count is capped by configuration (default 2 threads; graph compute uses ggml's
  pthread-based threadpool — on Windows OpenMP is disabled at build time so the worker exe stays free of MinGW
  runtime DLLs, ADR-010).
- Audio buffer limit: plugin drops audio chunks when the queue reaches 16 chunks (8 s capacity) rather than consuming unbounded memory.
- Input bounds: header payload length strictly capped at 1 MB. Malformed UTF-8 text or impossible PTS values are rejected.
- Caption queueing: plugin maintains no internal caption queue (ADR-016). Timed captions are submitted directly to
  VLC's native SPU pipeline (`vout_PutSubpicture`), which manages PTS display scheduling. Model-download status
  uses a separate wall-clock SPU channel, so `vw_caption_presenter_blank()` clears captions on pause/seek without
  hiding download progress; teardown or worker death flushes that channel.

## Deployment & Packaging

- **Windows Installer (NSIS)**: Standalone installer (`vlc-whisper-win64-setup.exe`) auto-detects VLC 64-bit installation paths from `HKLM\Software\VideoLAN\VLC`, installs the plugin DLL to `<VLC>\plugins\audio_filter\`, worker executable and models to `<VLC>\`, regenerates VLC's plugin cache (`vlc-cache-gen.exe`), registers an uninstaller, removes the app-owned `%LOCALAPPDATA%\vlc-whisper\models` download directory during uninstall, and creates shortcuts (`vlc.exe --audio-filter=vlc_whisper`).
- **Path Resolution Hierarchy**:
  1. Plugin DLL directory ancestors (`plugins/audio_filter` $\to$ `plugins` $\to$ `<VLC_ROOT>`).
  2. VLC process executable directory (`GetModuleFileNameA(NULL)`).
  3. Windows Registry keys `HKCU\Software\VLC-Whisper\InstallPath` and `HKLM\Software\VLC-Whisper\InstallPath`.
  4. Environment paths `%LOCALAPPDATA%\vlc-whisper\` and `%PROGRAMFILES%\vlc-whisper\`.
- **Licensing & Offline Discipline**: Root permissive MIT License with full third-party attributions (`THIRD_PARTY_NOTICES.md`). Completely zero network connectivity or cloud APIs.
