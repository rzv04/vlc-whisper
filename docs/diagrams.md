# VLC-Whisper Diagrams

Supplemental visuals only. `architecture.md`, `api-contracts.md`, and ADRs are authoritative when prose/diagrams disagree. The root README contains the primary system flow.

## Ownership boundary

```mermaid
flowchart TB
    A[VLC audio callback] -->|bounded enqueue| Q[(Plugin SPSC queue)]
    Q --> S[Plugin sender/control thread]
    S <-->|authenticated local IPC| R[Worker IPC reader]
    R --> WQ[(Bounded worker queue)]
    WQ --> W[Worker session + VAD + Whisper]
    W --> B[Final segment builder]
    B --> S
    S --> P[VLC caption presenter]

    A -. forbidden .-> X[Inference / blocking IPC / file I/O / blocking locks]
```

## Caption-session lifecycle

```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> STARTING: START/new media
    STARTING --> PLAYING: STARTED
    STARTING --> FAILED: fatal start failure
    PLAYING --> PAUSED: PAUSE
    PAUSED --> PLAYING: RESUME
    PLAYING --> RESYNC: seek/discontinuity/media epoch
    PAUSED --> RESYNC: seek/discontinuity/media epoch
    RESYNC --> STARTING: STOP old + START fresh session ID
    PLAYING --> STOPPING: STOP/media end
    PAUSED --> STOPPING: STOP/media end
    STOPPING --> IDLE
    FAILED --> IDLE: teardown/recovery
```

## Source seek epoch

```mermaid
sequenceDiagram
    participant P as Plugin client
    participant W as Worker
    participant V as VLC presenter

    P->>W: STOP(SEEK_DISCONTINUITY, old session)
    P->>V: clear generated captions
    P->>W: START(new session, new origin)
    W-->>P: STARTED(new session, source_active=1)
    P->>W: TRANSLATE_CTRL (if enabled)
    P->>W: POSITION(new session)
    W-->>P: SEGMENT(new session)
    Note over P: any old-session segment is stale and rejected
```

## Overload rule

```mermaid
flowchart LR
    PCM[PCM arrives] --> Q{bounded queue capacity?}
    Q -->|yes| KEEP[enqueue]
    Q -->|no| DROP[apply queue drop policy + account lost duration]
    KEEP --> SEND[worker]
    DROP --> SEND
    SEND --> CAP[caption output]
```

Playback is never slowed to preserve caption completeness. Data loss must remain explicit in timeline/drop accounting.
