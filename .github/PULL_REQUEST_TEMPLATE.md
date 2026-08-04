## Description

Briefly describe the changes introduced by this pull request.

---

## Type of Change

- [ ] Bug fix (non-breaking change fixing an issue)
- [ ] New feature (non-breaking change adding functionality)
- [ ] Refactoring / Performance improvement
- [ ] Documentation update

---

## Architectural & Project Rule Checklist

- [ ] **C17 Standard**: Code is standard C17 (`-std=c17`) with no project-authored C++.
- [ ] **Symbol Namespacing**: All functions, structs, enums, and macros use the `vw_` prefix.
- [ ] **Realtime Safety**: Zero heap allocations (`malloc`), IPC reads/writes, or blocking locks in VLC audio callbacks.
- [ ] **Timeline Sync**: Captions use signed 64-bit microsecond media timestamps (`pts_us`), never wall-clock time.
- [ ] **Privacy Invariants**: Local authenticated IPC only; zero network requests or disk logging of transcripts/PCM.
- [ ] **Header Documentation**: Every function in `.h` files includes a 20–30 word doc comment.

---

## Verification

- [ ] `clang-format --dry-run --Werror` passed cleanly.
- [ ] `cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug` passed 100%.
- [ ] `ctest --test-dir build/linux-x64-debug -T memcheck` verified 0 memory leaks.
