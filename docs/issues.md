# Known Issues

## Issue #1 — Linux worker discovery skips the plugin's own directory

- **Priority**: P1 (breaks the documented same-directory install layout; captions silently disabled)
- **Status**: Open (introduced by `bbaee41` `feat(plugin): resolve worker path relative to plugin location`)
- **Symptom**: With the worker co-located with the plugin (`<dir>/vlc-whisper-worker` next to `libvlc_whisper_plugin.so`), module open logs `PLUGIN_WORKER_UNAVAILABLE` and playback runs passthrough-only.
- **Reproduce**: Install the plugin and worker in the same directory (the documented layout), start VLC from any directory, and confirm the worker is never spawned.
- **Root cause**: The Linux ancestor walk in `vw_plugin_resolve_worker_path()` (`plugin/src/vlc_whisper_module.c`) starts at `up = 1`, so the plugin's own directory is never probed. The Windows branch starts at `up = 0` and finds a co-located worker; Linux does not.
- **Suggested fix / upgrade path**:
  - Start the Linux loop at `up = 0` so the same-directory case is probed first, mirroring the Windows behavior.
  - Add an integration test that places `vlc-whisper-worker` next to the plugin and asserts `vw_plugin_resolve_worker_path` returns that path.
