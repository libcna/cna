# Proposal: fix `CnaTests`' POSIX `::setenv()`/`::unsetenv()` blocker under MinGW-w64

**Status: proposal only, not started.** Written from `feature/dx9` (`D9-123`) because that is
where this session hit the blocker, but the problem and the fix are **not D3D9-specific** — it
already independently blocked `D3D11` (`plan_dx.md` `DX-15`) and `D3D12` (`DX-115`), both of which
deferred it with near-identical wording rather than fixing it inline. This document exists so the
next attempt (on whichever branch/worktree the project owner assigns it to) does not have to
re-derive the scope from scratch.

## The problem

`CnaTests` (the GoogleTest suite) fails to build under every Windows-cross-compiled backend
(`D3D9`/`D3D11`/`D3D12`, via MinGW-w64) because **13 test/tool files call POSIX-only
`::setenv()`/`::unsetenv()` directly**, which MinGW-w64's `<cstdlib>` does not declare. This is a
pure build-time blocker (compilation fails, nothing runs) — it has nothing to do with any one
backend's rendering correctness.

## Exact scope — 63 `setenv` + 2 `unsetenv` call sites, 13 files, 100% test setup/teardown

| File | Call sites | What it's setting |
|---|---|---|
| `tests/Microsoft/Xna/Framework/Audio/SoundEffectTests.cpp` | 18 `setenv` | `SDL_AUDIODRIVER=dummy` |
| `tests/Microsoft/Xna/Framework/Audio/CueTests.cpp` | 11 `setenv` | `SDL_AUDIODRIVER=dummy` |
| `tests/Microsoft/Xna/Framework/Audio/AudioCategoryTests.cpp` | 10 `setenv` | `SDL_AUDIODRIVER=dummy` |
| `tests/Microsoft/Xna/Framework/Audio/WaveBankTests.cpp` | 7 `setenv` | `SDL_AUDIODRIVER=dummy` |
| `tests/Microsoft/Xna/Framework/Audio/AudioEngineTests.cpp` | 3 `setenv` | `SDL_AUDIODRIVER=dummy` |
| `examples/headless_coverage_gaps_test.cpp` | 4 `setenv`, 1 `unsetenv` | `CNA_HEADLESS_MODE` (Fast/TRACE/validation/bogus) |
| `tests/Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstanceTests.cpp` | 2 `setenv` | `SDL_AUDIODRIVER=dummy` |
| `tests/Microsoft/Xna/Framework/Audio/SoundBankTests.cpp` | 2 `setenv` | `SDL_AUDIODRIVER=dummy` |
| `tests/Microsoft/Xna/Framework/Graphics/Texture2DTests.cpp` | 1 `setenv`, 1 `unsetenv` | `FNA_GRAPHICS_JPEG_SAVE_QUALITY=50` |
| `tests/Microsoft/Xna/Framework/FrameworkDispatcherTests.cpp` | 1 `setenv` | `SDL_AUDIODRIVER=dummy` |
| `tests/Microsoft/Xna/Framework/Audio/MicrophoneTests.cpp` | 1 `setenv` | `SDL_AUDIODRIVER=dummy` |
| `tests/Microsoft/Xna/Framework/Audio/SoundEffectInstanceTests.cpp` | 1 `setenv` | `SDL_AUDIODRIVER=dummy` |
| `tools/audio/audio_no_hardware_harness.cpp` | 1 `setenv` | `SDL_AUDIODRIVER=<nonexistent>` |

Every call site is test setup/teardown, forcing a dummy audio driver (or a headless-mode flag)
before constructing an `AudioEngine`/similar — there are **zero product-code call sites**. This is
purely a test-infrastructure gap, not a CNA API gap.

## Why it was never fixed inline (precedent)

`plan_dx.md`'s own `DX-15` (closed 2026-07-13, `D3D11`) found the identical gap and explicitly
deferred it: *"a real, pre-existing, much larger portability gap ... genuinely out of scope ...
worth its own separate, explicitly-scoped task."* `DX-115` (`D3D12` docs closure) repeated the same
deferral verbatim. `D9-123` (this plan) independently hit the same wall and predicted it in
advance. All three tasks agree: this is real, cross-cutting, shared-file (`tests/`) work that
doesn't belong to any one backend's branch — fixing it inline from `feature/dx9` risks a merge
collision with `feature/graphics`, a separate, independently-active clone doing `D3D11`/`D3D12`
work on the same shared test files right now.

## The fix is mechanical — no design work needed

A portable wrapper **already exists and is already proven under this exact MinGW-w64 toolchain**:
`System::Environment::SetEnvironmentVariable(name, value)` in the sibling `sharp-runtime` repo
(`include/System/Environment.hpp:298`, impl `src/System/Environment.cpp:176-189`). It already
branches `#if defined(_WIN32)` to `_putenv_s()` and to `::setenv`/`::unsetenv` everywhere else, with
an empty-value-means-unset convention matching .NET's own `Environment.SetEnvironmentVariable`
semantics. `sharp-runtime` is already a build dependency of every CNA target, and this exact file
already compiles and links cleanly in the existing `D3D11`/`D3D12` MinGW cross-builds today — this
fix carries **zero new-toolchain risk**. MinGW-w64's own `_putenv_s` is confirmed present
(`/usr/x86_64-w64-mingw32/include/sec_api/stdlib_s.h:44`, reachable via plain `<stdlib.h>`), so
`System::Environment`'s Windows branch is not relying on anything exotic.

The actual code change is a mechanical, identical-pattern replace across all 13 files:

```cpp
// before
::setenv("SDL_AUDIODRIVER", "dummy", 1);
::unsetenv("SDL_AUDIODRIVER");

// after
System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "");  // empty value == unset
```

No new abstraction needs inventing, and no `sharp-runtime` change is needed — the wrapper this
needs is already shipped and already exercised by other code paths.

## Recommended scope for the real task

1. One task, on whichever branch/worktree the project owner assigns it — it only touches
   `tests/`, `examples/headless_coverage_gaps_test.cpp`, and
   `tools/audio/audio_no_hardware_harness.cpp`; no backend-specific code anywhere.
2. Mechanically replace all 63 `setenv` + 2 `unsetenv` call sites per the pattern above.
3. Verify no regression on every already-working Linux-native backend (`EasyGL`/`Vulkan`/`Bgfx`/
   `SDL_Renderer`) — should be behavior-preserving there too, since `SetEnvironmentVariable`'s
   POSIX branch calls the same underlying `::setenv`/`::unsetenv`.
4. Verify `CnaTests` now *builds* under `D3D9`/`D3D11`/`D3D12`'s MinGW cross-compilation. Whether
   every test then *passes* is a separate question — audio tests may hit other Windows-cross gaps
   beyond just this compile blocker (e.g. whether SDL's `dummy` audio driver is even available in
   this MinGW/Wine runtime environment at all); this task closes the build blocker, not
   necessarily every downstream test result. Report honestly if some tests newly build but then
   fail/skip for an unrelated reason.
5. **Coordinate before landing**, since `feature/graphics` (a separate, independently-active clone
   doing `D3D11`/`D3D12` work) touches the exact same shared files — land on a shared ancestor
   (e.g. `develop`) if possible, or explicitly notify so the fix isn't duplicated or merge-conflicted.

## Risk / size estimate

Small and low-risk — no logic changes, a proven existing primitive, one call-site pattern repeated
identically 65 times across 13 files. The main real risk is coordination across the two active
worktrees sharing `tests/`, not the code change itself.
