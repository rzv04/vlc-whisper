# Task: Implement Ahead-of-Time Source File Decoding (Step 17c)

## Goal
Implement ahead-of-time media source file decoding in the worker process (using Windows Media Foundation and Linux FFmpeg) and Protocol v1.1 extensions (`VW_CAPABILITY_SOURCE_MODE`, `source_url`, `POSITION`/`SEEK` pacing) to transcribe speech in advance of VLC playback position, eliminating the 8-second live batch window lag and achieving zero perceived caption latency on local media files.

---

## Context
- **Relevant docs/ADRs**:
  - `docs/plans/milestone3_postmortem.md` §2.2 & Phase D (Media Foundation `IMFSourceReader`, FFmpeg demuxer, Model-Once `ADR-015`, process-wide `MFStartup`/`MFShutdown`).
  - `docs/plans/step17_restart_deprecation_plan.md` (fire-and-forget `POSITION`/`SEEK` message replacing MVP STOP→START teardown).
  - `docs/vlc-api-essentials.md` §3.4, §5 & §7 (input MRL extraction, S→M timestamp translation, pace control).
  - `docs/api-contracts.md` (Protocol v1.1 envelope, capability flags, `START` `source_url`, `POSITION` frame schema).
  - `docs/architecture.md` (Worker isolation, SPU presentation architecture, ADR-016 zero plugin caption queue).
  - `docs/roadmap.md` (Step 17c deliverable).
- **VLC/worker/protocol version affected**:
  - Protocol: Upgrade from `major=1, minor=0` to `major=1, minor=1` (`VW_CAPABILITY_SOURCE_MODE = (1U << 3) = 0x08`, `VW_MSG_POSITION = 13 = 0x000D`).
  - VLC: Pinned VLC 3.0.23 (Windows x64 / Linux x64).
  - Worker: `vlc-whisper-worker` (Vulkan GPU / CPU).
- **Assumptions and explicit non-goals**:
  - **Local Media First**: Ahead-of-time decoding applies to seekable local files (`file://` MRLs). Live IPTV/network streams automatically fall back to the Step 17b real-time PCM audio filter pipeline.
  - **Non-goal (Step 17d)**: 5-second clock jump heuristic for network jitter vs user seek discrimination is deferred to Step 17d.
  - **Non-goal (Step 17e)**: Beam search decoding and model switching are deferred to Step 17e.
  - **Bounded Lead Buffer**: Worker decodes a rolling look-ahead horizon (30–60s ahead of playhead) rather than transcribing entire 3-hour movies upfront.

---

## Scope
- **In scope**:
  - **Protocol v1.1**:
    - Add `VW_CAPABILITY_SOURCE_MODE = (1U << 3)` to `HELLO_ACK` capability bitmask.
    - Extend `vw_msg_start_t` to include `source_url` (up to 1024 bytes UTF-8) and `source_url_len`.
    - Define `vw_msg_position_t` (`VW_MSG_POSITION = 13`) with `session_id[16]`, `current_pts_us`, `input_time_us`, `playback_rate`, and `flags`.
    - Update protocol encoders, decoders, and validators in `vw_protocol_codec.c`.
  - **Worker Native Source Demuxers (`vw_source_decoder`)**:
    - Common interface `vw_source_decoder_t` (`open`, `seek`, `read_samples`, `close`, `get_duration_us`).
    - **Windows**: `vw_source_decoder_mf.c` using Windows Media Foundation `IMFSourceReader` configured for PCM 16kHz 16-bit Mono. Process-wide `MFStartup`/`MFShutdown` in worker lifecycle (`ADR-015`).
    - **Linux**: `vw_source_decoder_ffmpeg.c` using `libavformat`, `libavcodec`, `libswresample` to decode and resample audio streams to 16kHz float32.
  - **Worker Look-Ahead Decoding & Pacing Loop (`vw_worker.c`)**:
    - When `source_url` is present and source decoder opens successfully, worker enters **Source Mode**.
    - Maintains look-ahead lead of 30s (`VW_LOOKAHEAD_HORIZON_US = 30000000LL`) ahead of `current_pts_us`.
    - Reads audio at hardware decode speed, feeds into `vw_whisper_engine`, pushes hypotheses to `vw_segment_builder`, and emits `VW_MSG_CAPTION_SEGMENT` frames over IPC.
    - Handles `VW_MSG_POSITION` to update playhead position and re-seek demuxer (`IMFSourceReader::SetCurrentPosition` / `av_seek_frame`) on jumps without session teardown.
    - Transparent fallback to Live Streaming Mode if `source_url` is absent or demuxer fails.
  - **Plugin Source Mode Integration (`vw_whisper_module.c`)**:
    - In `vw_plugin_open` / session start, inspects `input_thread_t` for media URL (`input_item_GetURI`).
    - If local file URL is detected and worker advertises `VW_CAPABILITY_SOURCE_MODE`, passes `source_url` in `START`.
    - Sender thread periodically transmits `VW_MSG_POSITION` with `INPUT_GET_TIME` / `current_position_us` to pace worker decoding.
  - **Presenter Future SPU Scheduling (`vw_caption_presenter.c`)**:
    - Maps future segment timestamps against current input playhead:
      $$\text{start\_tick} = \text{mdate}() + (\text{segment->start\_pts\_us} - \text{input\_time\_us})$$
      $$\text{stop\_tick} = \text{mdate}() + (\text{segment->end\_pts\_us} - \text{input\_time\_us})$$
    - Submits future subpictures to VLC's SPU engine on the registered channel (`b_subtitle = false`, `i_start = start_tick`, `i_stop = stop_tick`), enabling VLC compositor to display them at the exact millisecond the playhead reaches speech.
- **Out of scope**:
  - Transport-level jitter 5-second jump gating (Step 17d).
  - Multi-lingual model dynamic switching (Step 17e / Milestone 4).
  - Standalone GUI configuration tool (Step 21).

---

## Design

### 1. Architectural Architecture & Pipeline Flow

```text
[VLC Media Playback]
  │ (MRL: file:///C:/video.mp4)
  ▼
[Plugin: vw_whisper_module]
  ├── Extracts MRL from input_item_t
  ├── Sends START (source_url="C:/video.mp4")
  └── Sender Thread: Emits periodic POSITION (current_pts_us, input_time_us)
        │
        │ IPC (Named Pipe / Unix Domain Socket - Protocol v1.1)
        ▼
[Worker: vw_worker]
  ├── Mode Selector: Source Mode vs Live PCM Mode
  ├── Source Decoder (MF on Win32 / FFmpeg on Linux):
  │     Decodes audio ahead of playback (30s horizon @ 10x-50x speed)
  ├── Whisper Engine (GPU Vulkan / CPU):
  │     Transcribes audio windows in advance
  └── Segment Builder:
        Emits CAPTION_SEGMENT (future start_pts_us, end_pts_us)
        │
        │ IPC (CAPTION_SEGMENT)
        ▼
[Presenter: vw_caption_presenter]
  ├── Computes start_tick = mdate() + (start_pts - input_time)
  └── Submits future subpicture to VLC SPU Channel 9
        │
        ▼
[VLC Video Output / SPU Compositor]
  └── Renders caption at exact video timestamp (0ms perceived latency!)
```

### 2. Protocol v1.1 Schema

#### `HELLO_ACK` Capability Flag
```c
#define VW_CAPABILITY_SOURCE_MODE (UINT32_C(1) << 3)
```

#### `START` Message Extension
```c
typedef struct vw_msg_start {
  vw_session_id_t session_id;
  int64_t timeline_origin_pts_us;
  uint32_t sample_rate;
  uint16_t channels;
  uint16_t sample_format;
  uint16_t model_id_len;
  char model_id[64];
  uint16_t language_len;
  char language[16];
  uint16_t source_kind;
  uint16_t source_url_len;
  char source_url[1024];  // UTF-8 file path or media MRL
} vw_msg_start_t;
```

#### `POSITION` Message Schema (`VW_MSG_POSITION = 0x000A`)
```c
typedef struct vw_msg_position {
  vw_session_id_t session_id;
  int64_t current_pts_us;    // Current media playback position (µs)
  int64_t input_time_us;     // Sampled input_Control(INPUT_GET_TIME)
  float playback_rate;       // 1.0 = normal, 2.0 = fast, 0.5 = slow
  uint32_t flags;            // Bit 0: is_seeking / position jump
} vw_msg_position_t;
```

### 3. Native Source Decoder Interface (`vw_source_decoder.h`)

```c
typedef struct vw_source_decoder vw_source_decoder_t;

typedef struct vw_source_decoder_info {
  int64_t duration_us;
  uint32_t channels;
  uint32_t sample_rate;
  char container_format[32];
} vw_source_decoder_info_t;

vw_source_decoder_t* vw_source_decoder_open(const char* url, vw_source_decoder_info_t* info);
bool vw_source_decoder_seek(vw_source_decoder_t* decoder, int64_t target_pts_us);
size_t vw_source_decoder_read_s16le(vw_source_decoder_t* decoder, int16_t* out_pcm, size_t max_samples, int64_t* out_pts_us);
void vw_source_decoder_close(vw_source_decoder_t* decoder);
```

### 4. Ownership, Threading & Privacy Model
- **Realtime Safety**: The audio filter callback remains strictly non-blocking. MRL resolution, `START` messaging, and `POSITION` polling occur on the background sender thread.
- **Worker Concurrency**: IPC reader thread continues to drain frames into `vw_worker_queue`. Main worker loop executes the look-ahead decode-and-transcribe pipeline in bounded chunks.
- **Privacy Boundary (Rule 5)**: Zero disk logging of decoded PCM or recognized transcript text. Diagnostic logs record only metadata (`text_len`, `pts_us`, `duration_us`).

---

## Acceptance Criteria
- [x] **Protocol v1.1 Validation**: `HELLO_ACK` advertises `VW_CAPABILITY_SOURCE_MODE`; `START` encodes/decodes `source_url`; `POSITION` frames encode, decode, and validate cleanly.
- [x] **Native Source Demuxing**:
  - Windows Media Foundation demuxer opens `.mp4`, `.mkv`, `.mp3`, `.wav` and decodes 16kHz S16LE PCM with accurate stream timestamps.
  - Linux FFmpeg demuxer opens local media files and outputs 16kHz S16LE PCM.
- [x] **Look-Ahead Pacing**: Worker maintains a 30s look-ahead lead ahead of reported `POSITION` and idles when the buffer is saturated.
- [x] **Zero Perceived Latency**: In local playback, captions are displayed synchronously with spoken audio without the 8-second live batch window delay.
- [x] **Seek Repositioning without Teardown**: Position jumps seek the demuxer (`IMFSourceReader::SetCurrentPosition` / `av_seek_frame`) and clear in-flight hypotheses without tearing down the worker process or IPC pipe.
- [x] **Live Stream Passthrough**: Non-file MRLs (live IPTV/streams) transparently use the Step 17b real-time PCM audio streaming path.
- [x] **Documentation**: `docs/api-contracts.md`, `docs/architecture.md`, `docs/roadmap.md`, `docs/source-layout.md`, `docs/test-strategy.md` updated per Rule 14.

---

## Test Plan

### 1. Automated Unit & Integration Tests
- `test_protocol_codec`: Unit test serialization, deserialization, and schema validation for Protocol v1.1 (`START` with `source_url`, `POSITION` message).
- `test_source_decoder`: Test demuxing, sample conversion, and seeking on local audio/video fixtures (`harvard.wav` / test media) using both MF and FFmpeg backends.
- `test_worker_source_mode`: Integration test verifying worker opens source file, decodes ahead, receives `POSITION` updates, emits future captions, and handles seek repositioning.
- `test_caption_presenter`: Unit test verifying future timestamp mapping (`mdate() + (pts - input_time)`) for look-ahead subpictures.

### 2. Multi-Platform Build Verification
- Linux: `cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug`
- Valgrind: `ctest --test-dir build/linux-x64-debug -T memcheck` (0 memory leaks)
- Windows Cross-Compilation: `cmake --build --preset windows-x64-release-cpu` (MinGW linking with `mfplat`, `mfreadwrite`, `mfuuid`, `ole32`, `propsys`).

### 3. Manual VLC Verification (Windows & Linux)
- Play local MP4 file: `vlc --audio-filter=vlc_whisper sample.mp4`.
- Verify captions appear in exact real-time sync with speech (0 ms perceived delay).
- Perform seek jumps (forward +30s, backward -15s) and verify captions resume rapidly without worker process restart.
- Test live network stream or stream without file URL to confirm seamless fallback to live audio filter capture.

---

## Definition of Done
- [x] Standard C17 code (`-std=c17`), no project-authored C++.
- [x] Zero blocking locks, allocations, or I/O in VLC audio filter callback.
- [x] Privacy invariant preserved: no raw transcripts or audio samples in log files.
- [x] Media Foundation `MFStartup`/`MFShutdown` managed safely across worker process lifetime (`ADR-015`).
- [x] Linux FFmpeg demuxer integration clean with `pkg-config` conditional detection.
- [x] 100% passing test suite across Linux and Windows presets with zero Valgrind leaks.
- [x] Format verification clean (`clang-format --dry-run --Werror`).
- [x] Documentation updated across all required files in `docs/` (Rule 14).
