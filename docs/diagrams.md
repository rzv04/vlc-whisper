# VLC-whisper Diagrams

These diagrams are the visual companion to the accepted foundation. They are normative where they repeat an accepted decision in `architecture.md` or `decisions.md`; if a diagram and a contract disagree, the contract wins until the diagram is corrected.

## System context

```mermaid
flowchart LR
    U[User] -->|Opens and controls local video| V[VLC: pinned Windows build]
    V -->|Decoded PCM plus media PTS| C[Capture module: C]
    C --> Q[Bounded SPSC audio queue]
    Q --> S[IPC sender thread]
    S <-->|Authenticated local named pipe \n versioned binary protocol| W[vlc-whisper-worker.exe: C host]
    W --> H[whisper.cpp C API\npinned dependency]
    H --> M[(Local tiny.en model)]
    W -->|Timed final caption segments| R[IPC receiver and presenter: C]
    R -->|Timed subtitle or OSD path| V
    V -->|Captions over video| U

    classDef boundary fill:#fff4e5,stroke:#d97706,color:#111;
    classDef local fill:#ecfdf5,stroke:#059669,color:#111;
    class W,H,M local;
    class S,W,R boundary;
```

The user, VLC, plugin, worker, and model all remain on the same machine. There is no HTTP endpoint, cloud transcription service, telemetry destination, or TCP listener.

## Thread and ownership boundaries

```mermaid
flowchart TB
    A[VLC audio callback] -->|Non-blocking enqueue only| Q[(Bounded SPSC queue)]
    A -. prohibited .-> X[Pipe I/O, inference, waits, per-block allocation]
    T1[IPC sender thread] -->|Drains queue| P[Named-pipe client]
    P --> W[Worker process]
    W --> P2[Named-pipe client]
    P2 --> T2[IPC receiver thread]
    T2 -->|Validated timed segments| PR[Caption presenter]
    PR --> V[VLC rendering path]

    classDef forbidden fill:#fee2e2,stroke:#dc2626,color:#111;
    class X forbidden;
```

**Invariant:** a captioning failure may remove captions, but it must not block, glitch, or crash VLC playback.

## Playback and caption state

```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> STARTING: eligible local English media starts
    STARTING --> READY: worker handshake and STARTED
    STARTING --> FAILED: worker/model/protocol failure
    READY --> PLAYING: play
    PLAYING --> PAUSED: pause
    PAUSED --> PLAYING: resume
    PLAYING --> STOPPING: stop or end
    PAUSED --> STOPPING: stop or end
    STOPPING --> IDLE: captions cleared, IPC closed
    PLAYING --> FAILED: seek, rate/title/source discontinuity
    PAUSED --> FAILED: seek, rate/title/source discontinuity
    FAILED --> IDLE: item stops or changes
```

For the MVP, `FAILED` means **caption session disabled for this item**. It never means VLC playback itself has failed.

## Session startup sequence

```mermaid
sequenceDiagram
    participant VLC as VLC
    participant CAP as Capture module
    participant IPC as Sender/receiver
    participant W as Worker
    participant WH as whisper.cpp

    VLC->>CAP: Open eligible local file and begin playback
    CAP->>IPC: Create session ID and launch worker
    IPC->>W: HELLO(protocol range, 32-byte token)
    W-->>IPC: HELLO_ACK(capabilities, version)
    IPC->>W: START(session, 16kHz mono S16LE, tiny.en, en)
    W->>WH: Load validated local model
    W-->>IPC: STARTED
    loop while playing and PTS is monotonic
        CAP->>CAP: Enqueue bounded PCM + PTS
        IPC->>W: AUDIO(start PTS, duration, PCM)
        W->>WH: VAD, rolling-window inference, deduplication
        W-->>IPC: SEGMENT(final, start/end PTS, UTF-8 text)
        IPC->>VLC: Schedule/display caption
    end
    VLC->>CAP: Pause, stop, or end
    CAP->>IPC: PAUSE / STOP
    IPC->>W: PAUSE / STOP
    W-->>IPC: status/connection close
    IPC->>VLC: Clear generated captions
```

## Backpressure behavior

```mermaid
flowchart TD
    IN[PCM arrives with media PTS] --> SPACE{Queue has capacity?}
    SPACE -->|Yes| ENQ[Enqueue chunk]
    SPACE -->|No| DROP[Drop oldest unprocessed audio]
    DROP --> COUNT[Increase audio_dropped_us\nand rate-limited diagnostic counter]
    COUNT --> ENQ
    ENQ --> SEND[Sender forwards chunks to worker]
    SEND --> HEALTH{Worker available and valid?}
    HEALTH -->|Yes| CAP[Receive timed captions]
    HEALTH -->|No| DISABLE[Clear captions and disable caption session]

    classDef safe fill:#ecfdf5,stroke:#059669,color:#111;
    classDef warn fill:#fff7ed,stroke:#ea580c,color:#111;
    class ENQ,SEND,CAP safe;
    class DROP,COUNT,DISABLE warn;
```

The queue is capped at 15 seconds of unprocessed audio in the initial design. Loss under overload is explicit and measurable; slowing playback is never an overload strategy.

## Protocol frame

```mermaid
block-beta
  columns 6
  block:header:6
    A[magic:32 bits] B[major:16 bits] C[type:16 bits] D[payload length:32 bits] E[sequence:64 bits]
  end
  F[Payload: schema depends on message type]:6
```

```text
magic: 0x564C4357 (VLCW)
maximum payload: 1,048,576 bytes
transport: Windows message-mode named pipe
Linux port: Unix-domain SOCK_SEQPACKET, same frame format
```

The receiver validates protocol version, token, session ID, sequence, payload bounds, UTF-8, and timestamp invariants before acting on a message.

## Delivery roadmap

```mermaid
gantt
    title VLC-whisper delivery order
    dateFormat  YYYY-MM-DD
    axisFormat  %b
    section Foundation
    Pin target and toolchain                 :done, m0a, 2026-07-23, 7d
    Cross-build worker and validate metadata :m0b, after m0a, 7d
    section Worker proof
    Offline inference and IPC contract       :m1a, after m0b, 14d
    Fuzzing and diagnostics                  :m1b, after m1a, 7d
    section VLC feasibility
    PCM capture proof                        :m2a, after m1b, 7d
    Timed caption presentation proof         :m2b, after m2a, 7d
    section Local-file MVP
    End-to-end lifecycle and failures        :m3a, after m2b, 14d
    Windows acceptance and packaging         :m3b, after m3a, 7d
    section Post-MVP
    Seek reset and local-file seeking        :m4a, after m3b, 14d
    Streams, settings UI, Linux port         :m4b, after m4a, 30d
```

Dates are illustrative sequencing anchors, not delivery commitments. The required gates are feasibility proof, bounded behavior, and Windows VLC end-to-end validation—not calendar completion.

## Feature scope map

```mermaid
quadrantChart
    title Feature placement by value and implementation risk
    x-axis Lower user value --> Higher user value
    y-axis Lower implementation risk --> Higher implementation risk
    quadrant-1 Prove deliberately
    quadrant-2 Schedule after MVP
    quadrant-3 Avoid or defer
    quadrant-4 MVP priority
    Local English captions: [0.90, 0.25]
    Pause and resume: [0.75, 0.25]
    Worker crash safety: [0.85, 0.45]
    Seeking support: [0.85, 0.75]
    Network VOD: [0.65, 0.75]
    IPTV livestreams: [0.65, 0.90]
    Model and language GUI: [0.55, 0.55]
    Large models and GPU variants: [0.50, 0.75]
    Translation and speaker labels: [0.30, 0.70]
```

This map is intentionally a prioritization aid, not a scientific measurement. It reinforces that the smallest useful MVP is local English captioning with reliable failure behavior, before source diversity or UI breadth.
