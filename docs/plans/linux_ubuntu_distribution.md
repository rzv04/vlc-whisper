# Task: Ship Ubuntu x64 distribution

## Outcome
Ubuntu x64 users can install VLC-Whisper from a release `.deb` or the one-line installer without changing runtime architecture.

## Scope
- In: Linux x64 Release preset, required production deps, DEB layout/cache hooks, minimal installer, Ubuntu README.
- Out: CI changes, Snap/Flatpak packaging, runtime architecture changes.
- Components/files: CMake presets/packaging, worker dependency checks, Lua model discovery, installer, README.

## Contract map
`release artifact -> Debian/VLC filesystem layout -> existing plugin/worker/Lua discovery -> VLC-Whisper runtime`

- Invariants touched: identity, failure classification, privacy/network; realtime/runtime architecture unchanged.

## Failure semantics
| Boundary/failure | Behavior |
| --- | --- |
| Unsupported distro/arch/VLC source | fail install with actionable message |
| VLC below 3.0.23 and no suitable APT candidate | fail install |
| Missing release/checksum/dependency | fail install |
| Plugin-cache refresh failure | warn; package remains installed |

## Lifecycle impact
None. Runtime START/STOP/seek/EOF/respawn behavior is unchanged.

## Identity / metrics / hot path
- Install paths are discovered from Debian package metadata/multiarch, not fixed x86 paths.
- No metrics change.
- No realtime code change.

## Tests first
- Validate installer shell syntax and its rejection/version/path checks.
- Validate CMake preset JSON and configure/package wiring.
- Preserve the existing Lua extension and validate its Linux model lookup path.

## Implementation
Add the smallest distribution-only changes required for Ubuntu x64.

## Verification
- [ ] Shell syntax passes
- [ ] Preset JSON parses
- [ ] Linux debug build/tests remain unchanged
- [ ] Linux release configure/build/package paths are coherent
- [ ] Lua extension remains installed and resolves bundled models
- [ ] CI workflow untouched

## Evidence
- Commands/results recorded in PR.
- Snap/Flatpak VLC remain explicitly unsupported for this first package.
