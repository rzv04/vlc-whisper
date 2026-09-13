# VLC-Whisper Windows Manual Testing Checklist

This checklist provides an exhaustive, step-by-step verification plan for **VLC-Whisper on Windows (x64)**. It covers the complete user lifecycle: installation, first-run activation, real-time transcription, timeline synchronization, settings & model provisioning, subtitle translation, edge cases, and failure paths.

---

## Pre-Requisites & Test Environment Setup

- [ ] **Operating System**: 64-bit Windows 10 (Build 19041+) or Windows 11.
- [ ] **VLC Media Player**: Official 64-bit VLC 3.0.x installed (e.g. `C:\Program Files\VideoLAN\VLC`). *(Note: 32-bit VLC is unsupported).*
- [ ] **Hardware Backends**:
  - [ ] Dedicated GPU with updated Vulkan drivers (NVIDIA, AMD, or Intel Arc) for GPU verification.
  - [ ] Standard multi-core CPU (Intel / AMD) for CPU fallback verification.
- [ ] **Test Media Files**:
  - [ ] Short clean speech audio/video (e.g. `harvard.wav` or standard English MP4/MKV).
  - [ ] Media with ambient noise / music / long silence gaps to verify Silero VAD suppression.
  - [ ] Non-English speech video (e.g. Romanian, Spanish, French) for language & translation tests.
  - [ ] Live / streaming media URL (e.g. public IPTV stream, HLS radio, or RTSP feed).
- [ ] **Release Artifacts**:
  - [ ] Installer: `vlc-whisper-0.1.0-win64-setup.exe`
  - [ ] Portable archive: `vlc-whisper-0.1.0-win64.zip`

---

## Phase 1: Installation & Setup Wizard Testing

### 1.1 Fresh Standalone Installation (Happy Path)
- [ ] **Pre-check**: Ensure VLC is completely closed before running installer.
- [ ] **Execution**: Launch `vlc-whisper-0.1.0-win64-setup.exe`.
  - [ ] UAC prompt appears requesting administrator privileges.
  - [ ] Welcome page displays project branding and description.
  - [ ] License agreement page displays the MIT License.
  - [ ] Directory selection page automatically detects 64-bit VLC at `C:\Program Files\VideoLAN\VLC`.
- [ ] **Installation Process**:
  - [ ] Files extract without error or antivirus false-positive alerts.
  - [ ] Setup finishes cleanly with a "Completed" status.
- [ ] **File System Verification**:
  - [ ] Plugin DLL exists: `<VLC_DIR>\plugins\audio_filter\libvlc_whisper_plugin.dll`
  - [ ] GPU worker exists: `<VLC_DIR>\vlc-whisper-worker.exe`
  - [ ] CPU worker exists: `<VLC_DIR>\vlc-whisper-worker-cpu.exe`
  - [ ] Bundled models exist:
    - [ ] `<VLC_DIR>\models\ggml-tiny.bin` (~75 MB)
    - [ ] `<VLC_DIR>\models\ggml-silero-vad.bin` (~865 KB)
    - [ ] `<VLC_DIR>\models\manifest.json`
  - [ ] Lua extension exists: `<VLC_DIR>\lua\extensions\vlc_whisper_settings.lua`
  - [ ] Uninstaller exists: `<VLC_DIR>\uninstall-vlc-whisper.exe`
  - [ ] Plugin cache invalidated: `<VLC_DIR>\plugins\plugins.dat` is deleted or regenerated.
- [ ] **Shortcut Verification**:
  - [ ] Desktop shortcut exists: **"VLC (with AI Whisper Captions)"**.
    - [ ] Inspect shortcut target: confirms `vlc.exe --audio-filter=vlc_whisper`.
  - [ ] Start Menu folder `VLC-Whisper` contains shortcuts for VLC launch and Uninstaller.
- [ ] **Windows Registry Verification**:
  - [ ] `HKLM\Software\VLC-Whisper` contains `InstallPath` and `Version`.
  - [ ] `HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\VLC-Whisper` is registered:
    - [ ] Appears in Windows **Settings > Apps > Installed apps** with version `0.1.0` and icon.

---

### 1.2 Installation Edge Cases & Guardrails
- [ ] **VLC Running During Install**:
  - [ ] Open VLC and start playing audio.
  - [ ] Run `vlc-whisper-0.1.0-win64-setup.exe`.
  - [ ] Installer displays warning: *"VLC is currently running. Please close VLC before installing."*
  - [ ] Clicking "Cancel" cleanly aborts installation without partial file damage.
  - [ ] Closing VLC and clicking "OK" allows installation to proceed successfully.
- [ ] **Invalid Directory Selection**:
  - [ ] In the Directory page, browse to an arbitrary folder without VLC (e.g. `C:\Windows\Temp`).
  - [ ] Click "Next/Install": installer blocks with message: *"vlc.exe was not found in '...' Please select your VLC installation folder."*
- [ ] **Non-64-bit VLC Detection**:
  - [ ] Point installer to a 32-bit VLC directory (if available).
  - [ ] Installer blocks with message: *"The selected VLC executable is not 64-bit. VLC-Whisper requires 64-bit VLC."*
- [ ] **Upgrade / Re-install Over Locked Files**:
  - [ ] Simulate locked worker or plugin.
  - [ ] Run installer again: installer writes to temporary `.vw-new` staging files, flags files for reboot replacement (`/REBOOTOK`), and notifies user.

---

### 1.3 Portable Archive (.zip) Manual Deployment
- [ ] Extract `vlc-whisper-0.1.0-win64.zip` directly into a standalone VLC test directory.
- [ ] Run `vlc-cache-gen.exe "<VLC_DIR>\plugins"` from Command Prompt.
- [ ] Verify that VLC launches with `--audio-filter=vlc_whisper` and plugin loads correctly.

---

## Phase 2: Plugin Activation & Process Lifecycle

### 2.1 First Launch & Worker Spawning
- [ ] Launch VLC via **"VLC (with AI Whisper Captions)"** shortcut.
- [ ] Open **Windows Task Manager (Details tab)**:
  - [ ] `vlc.exe` is running.
  - [ ] `vlc-whisper-worker.exe` (or `vlc-whisper-worker-cpu.exe`) spawns in the background.
  - [ ] **No command prompt or black console window pops up** (verified `CREATE_NO_WINDOW` flag).
  - [ ] Worker CPU/RAM usage stays quiet during idle state (< 80 MB RAM before model load).
- [ ] **Alternative Launch (VLC Preferences)**:
  - [ ] Open VLC normally without shortcut.
  - [ ] Go to **Tools > Preferences (Ctrl+P) > Show settings: All > Audio > Filters**.
  - [ ] Confirm **Offline Whisper AI Captions Filter** (`vlc_whisper`) checkbox is present.
  - [ ] Checking the box and saving preferences enables captions for standard VLC launches.

---

## Phase 3: Core Transcription Verification (Happy Path)

### 3.1 Local Video / Audio Playback
- [ ] Open a local speech video/audio file (e.g. English dialog).
- [ ] **Warm-up & First Cue**:
  - [ ] Subtitles begin rendering on screen within 2–4 seconds of playback start.
  - [ ] Subtitles render via native VLC SPU subpicture channel at bottom-center of the screen.
  - [ ] Font rendering is crisp, legible, with proper contrast outline.
- [ ] **Speech Accuracy & Timing**:
  - [ ] Spoken words closely match caption text.
  - [ ] Subtitle timestamps match spoken audio without noticeable drift or lag.
  - [ ] **Minimum Duration Floor**: Short single words (e.g. "Yes", "Okay") remain visible for at least 1.0 second rather than vanishing in a flicker.

---

### 3.2 Voice Activity Detection (Silero VAD) & Anti-Hallucination
- [ ] **Silence & Pauses**:
  - [ ] Play media with long gaps between sentences (3–10 seconds of silence).
  - [ ] Screen blanks during silence; no phantom text is generated.
- [ ] **Background Music / Noise**:
  - [ ] Play audio with instrumental music, crowd noise, or typing.
  - [ ] Whisper hallucination filters suppress non-speech sound tags (e.g. no infinite loops of `[Music]`, `♪♪`, or `...`).
  - [ ] When speech resumes over background music, dialogue is transcribed cleanly.

---

## Phase 4: Playback Controls & Timeline Synchronization

### 4.1 Pause & Resume
- [ ] Click **Pause (Space)** while captions are displaying:
  - [ ] Current on-screen caption clears or holds cleanly; audio streaming to worker halts.
  - [ ] Worker memory and CPU drop to idle.
- [ ] Click **Play (Space)**:
  - [ ] Playback resumes instantly with smooth audio (no stuttering, popping, or crackling).
  - [ ] New captions resume for newly played audio. Stale pre-pause captions do NOT replay.

---

### 4.2 Seeking & Scrubbing Discontinuities
- [ ] **Forward Jump (+10s or +1min)**:
  - [ ] Jump forward in media.
  - [ ] Subtitles clear instantly from the screen.
  - [ ] No residual cues from the skipped section are shown.
  - [ ] New captions appear at the new position within ~1-2 seconds.
- [ ] **Backward Jump (-10s or -1min)**:
  - [ ] Jump back to previously watched section.
  - [ ] Subtitles clear instantly, session epoch resets, and captions for the earlier section regenerate accurately.
- [ ] **Seek to Timeline Origin (00:00:00)**:
  - [ ] Seek directly to 00:00:00.
  - [ ] Captions reset and begin cleanly from the beginning.
- [ ] **Rapid Scrubbing Burst**:
  - [ ] Drag the VLC seek slider back and forth rapidly 5–10 times.
  - [ ] **Pass criteria**: VLC does not crash, audio pipeline does not deadlock, and worker queue does not explode. Within 1-2 seconds of releasing the slider, transcription stabilizes.

---

### 4.3 Variable Playback Speeds
- [ ] Change playback speed to **0.5x**: Captions remain synchronized with slowed speech.
- [ ] Change playback speed to **1.5x** and **2.0x**: Captions keep pace with accelerated speech without overlapping or dropping out.

---

### 4.4 Playlist & Media Swap
- [ ] In VLC, skip to the **Next** track in playlist while media is playing:
  - [ ] Old session terminates cleanly; old captions vanish.
  - [ ] New media initializes a fresh session with correct timestamps starting from 0.
- [ ] Drag and drop an entirely new video file into VLC window:
  - [ ] Re-initializes seamlessly without requiring VLC restart.

---

### 4.5 Teardown & Clean Process Exit
- [ ] Stop playback (Stop button): Captions clear, worker returns to idle.
- [ ] Exit VLC (`Ctrl+Q` or close window):
  - [ ] `vlc.exe` closes completely.
  - [ ] Check Task Manager: `vlc-whisper-worker.exe` shuts down within 1-2 seconds.
  - [ ] **Zero orphan worker processes left running in background.**

---

## Phase 5: Streaming & Live Network Media

### 5.1 Network Video-on-Demand (HTTP/HTTPS URL)
- [ ] Open a seekable remote MP4/MKV via **Media > Open Network Stream**.
- [ ] Verify that look-ahead source decoding or PCM streaming transcribes correctly.
- [ ] Verify seeking across network buffers behaves safely.

---

### 5.2 Non-Seekable Live Streams (IPTV / Live Radio)
- [ ] Open a live stream (e.g. live HLS or Icecast stream).
- [ ] Verify progressive live scheduling:
  - [ ] First caption appears after initial ~2-second buffer.
  - [ ] Context window grows smoothly (2s -> 4s -> 8s) with 1-second steady-state hops.
  - [ ] No audio latency or stuttering introduced into the live broadcast.

---

## Phase 6: In-VLC Settings GUI & Model Provisioning

### 6.1 Settings Dialog Verification
- [ ] In VLC menu, click **View > VLC-Whisper Settings** (or **Tools > Extensions > VLC-Whisper Settings**).
- [ ] Verify dialog components render properly:
  - [ ] **Engine (Backend)** dropdown: `auto`, `gpu`, `cpu`.
  - [ ] **Speech Model** dropdown: lists bundled `tiny`, plus `tiny.en`, `base`, `small`, `medium`, `large-v3`.
  - [ ] **Audio Language** dropdown: lists supported languages (`English`, `Romanian`, `Spanish`, `French`, `German`, etc.).
  - [ ] **CPU Threads** input: default `4` (accepts `1` to `16`).
  - [ ] **Translation** options: Checkbox, Source language, Target language, Placement mode (Dual line / Translation only).
  - [ ] Action buttons: **[ Apply Settings ]** and **[ Download Selected Model ]**.

---

### 6.2 Settings Persistence
- [ ] Change CPU threads to `6` and change language to `Spanish`.
- [ ] Click **[ Apply Settings ]**.
- [ ] Close the settings dialog and restart VLC.
- [ ] Reopen **View > VLC-Whisper Settings**:
  - [ ] CPU threads still shows `6`.
  - [ ] Language still shows `Spanish`.

---

### 6.3 On-Demand Model Downloading (Network Path)
- [ ] In the settings dialog, select an uninstalled model (e.g. `base`).
- [ ] Click **[ Download Selected Model ]**:
  - [ ] **Playback must NOT freeze**: VLC continues playing smoothly while download runs in background.
  - [ ] On-screen OSD or dialog shows download progress percentage (0% -> 100%).
  - [ ] Upon completion: Model is verified with SHA-256 and stored in `%LOCALAPPDATA%\vlc-whisper\models\ggml-base.bin`.
  - [ ] Worker hot-swaps to the new `base` model without requiring VLC restart.
- [ ] **Download Abort / Failure**:
  - [ ] Start downloading a large model (`large-v3`).
  - [ ] Disconnect network or cancel: worker cleans up partial `.part` file, logs error, and falls back to previous model. VLC does not crash.

---

## Phase 7: Real-Time Subtitle Translation (Opt-In)

### 7.1 Keyless Translation Engine
- [ ] Open **View > VLC-Whisper Settings**.
- [ ] Check **[x] Enable Real-Time Translation**.
- [ ] Set **Target Language** to your desired language (e.g. `Romanian (ro)` or `Spanish (es)`).
- [ ] Select **Placement Mode**:
  - [ ] **Dual line mode**: Subtitles display original speech on top line, translated speech on bottom line.
  - [ ] **Translation only mode**: Subtitles display translated speech only.
- [ ] Click **[ Apply Settings ]**.
- [ ] Play English video:
  - [ ] Captions appear translated in real-time into the target language.
  - [ ] Translation uses keyless 3-tier fallback (Web RPC -> GTX -> Mobile scrape) with zero API keys required.

---

### 7.2 Translation Under Seeking & Saturation
- [ ] Seek forward while translation is actively running:
  - [ ] In-flight translation jobs for old timestamps are immediately cancelled/dropped.
  - [ ] No delayed translated cues from before the seek pop onto the screen.
- [ ] Saturated dialogue test (rapid talking scene):
  - [ ] Bounded queue holds up to 4 pending translation cues.
  - [ ] If translation takes longer than threshold, queue safely drops or falls back without lagging behind playback.

---

## Phase 8: Failure Paths & Resilience Testing (Negative Scenarios)

| Failure Scenario | Test Action | Expected Safe Behavior | Pass/Fail |
| :--- | :--- | :--- | :--- |
| **Worker Process Killed** | In Task Manager, terminate `vlc-whisper-worker.exe` while video is playing. | VLC media continues playing uninterrupted. Audio does NOT glitch or loop. Worker respawns on next media/session. | [ ] |
| **Missing Model File** | Rename `models\ggml-tiny.bin` to `ggml-tiny.bin.bak` while VLC is closed, then start VLC. | VLC plays media normally. Subtitles simply do not appear. Single diagnostic warning in log; no crash or hang. | [ ] |
| **Corrupt Model File** | Replace `ggml-tiny.bin` with a 0-byte or text file. | Worker fails model initialization cleanly, informs plugin, VLC playback remains 100% unaffected. | [ ] |
| **Corrupted Pipe Communication** | Kill named pipe or send garbage data. | Plugin detects EOF/broken pipe, closes transport safely, and does not block the real-time audio thread. | [ ] |
| **Extended 2+ Hour Session** | Play a 2-hour movie with subtitles enabled. | Monitor Task Manager: VLC memory remains flat; worker memory stays bounded (< 500 MB for tiny/base); zero audio drift. | [ ] |
| **No Audio Device / Muted** | Play video with audio track disabled or muted. | Zero CPU spikes; VAD detects pure silence; worker remains quiescent. | [ ] |

---

## Phase 9: Clean Uninstallation Verification

### 9.1 Uninstaller Execution
- [ ] Close VLC.
- [ ] Go to Windows **Settings > Apps > Installed apps > VLC-Whisper AI Subtitle Plugin > Uninstall** (or run `<VLC_DIR>\uninstall-vlc-whisper.exe`).
- [ ] UAC prompt appears and uninstaller opens.
- [ ] Click **Uninstall**.

### 9.2 Post-Uninstall Verification
- [ ] **Files Removed**:
  - [ ] `<VLC_DIR>\plugins\audio_filter\libvlc_whisper_plugin.dll` is gone.
  - [ ] `<VLC_DIR>\vlc-whisper-worker.exe` and `-cpu.exe` are gone.
  - [ ] `<VLC_DIR>\models\` folder and bundled `.bin` models are gone.
  - [ ] `<VLC_DIR>\lua\extensions\vlc_whisper_settings.lua` is gone.
  - [ ] `<VLC_DIR>\vlc-whisper\` legal notices folder is gone.
  - [ ] `<VLC_DIR>\uninstall-vlc-whisper.exe` is deleted.
- [ ] **User Cache Cleaned**:
  - [ ] `%LOCALAPPDATA%\vlc-whisper\models` (downloaded models) is completely removed.
- [ ] **Shortcuts Removed**:
  - [ ] Desktop shortcut "VLC (with AI Whisper Captions)" is deleted.
  - [ ] Start Menu `VLC-Whisper` folder is deleted.
- [ ] **Registry Cleaned**:
  - [ ] `HKLM\Software\VLC-Whisper` is deleted.
  - [ ] `HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\VLC-Whisper` is deleted.
- [ ] **VLC Health Post-Uninstall**:
  - [ ] Launch VLC normally.
  - [ ] VLC opens cleanly without errors about missing plugins or corrupted cache.
