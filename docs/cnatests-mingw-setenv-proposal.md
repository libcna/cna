# Proposal: fix `CnaTests`' POSIX `::setenv()`/`::unsetenv()` blocker under MinGW-w64

**Status: FULLY IMPLEMENTED 2026-07-15 on `feature/dx9`, project-owner go-ahead given.** Originally
written as a proposal-only document; the table below is kept as-written (62 call sites total, not
64 — this table slightly overcounted `SoundEffectTests.cpp`, see the correction note at the bottom)
for the historical record of what was scoped before implementation. See `plans/plan_dx9.md`'s `D9-123`
row for the full implementation/verification result: `CnaTests` now compiles cleanly under
`CNA_GRAPHICS_RENDERER=D3D9` (first time ever for any Windows-cross renderer), AND the follow-up
`gtest_discover_tests` CTest-registration blocker this fix surfaced (`ctest -L D3D9` couldn't
execute the cross-compiled `.exe` to enumerate test names) is also fixed and verified
end-to-end — `ctest -L D3D9` runs clean, 4383 tests are registered, and spot-checked individual
tests genuinely execute through Wine and pass.

Originally written from `feature/dx9` (`D9-123`) because that is where this session hit the
blocker, but the problem and the fix are **not D3D9-specific** — it already independently blocked
`D3D11` (`plans/plan_dx.md` `DX-15`) and `D3D12` (`DX-115`), both of which deferred it with near-identical
wording rather than fixing it inline. This fix has not been independently re-verified on D3D11/
D3D12's own build directories (not configured in this worktree) — no regression is expected since
only shared `tests/`/`examples/`/`tools/` files changed and `sharp-runtime`'s wrapper was already
proven compiling under those same toolchains, but this specific commit's effect there is unconfirmed.

## The problem

`CnaTests` (the GoogleTest suite) fails to build under every Windows-cross-compiled renderer
(`D3D9`/`D3D11`/`D3D12`, via MinGW-w64) because **13 test/tool files call POSIX-only
`::setenv()`/`::unsetenv()` directly**, which MinGW-w64's `<cstdlib>` does not declare. This is a
pure build-time blocker (compilation fails, nothing runs) — it has nothing to do with any one
renderer's rendering correctness.

## Exact scope — 62 `setenv`+`unsetenv` call sites, 13 files, 100% test setup/teardown

*(Correction, post-implementation: `SoundEffectTests.cpp` is 17 `setenv` sites, not 18 as
originally counted below — 60 `setenv` + 2 `unsetenv` = 62 total, not 65. The per-file breakdown
below is otherwise accurate.)*

| File | Call sites | What it's setting |
|---|---|---|
| `tests/Microsoft/Xna/Framework/Audio/SoundEffectTests.cpp` | 17 `setenv` | `SDL_AUDIODRIVER=dummy` |
| `tests/Microsoft/Xna/Framework/Audio/CueTests.cpp` | 11 `setenv` | `SDL_AUDIODRIVER=dummy` |
| `tests/Microsoft/Xna/Framework/Audio/AudioCategoryTests.cpp` | 10 `setenv` | `SDL_AUDIODRIVER=dummy` |
| `tests/Microsoft/Xna/Framework/Audio/WaveBankTests.cpp` | 7 `setenv` | `SDL_AUDIODRIVER=dummy` |
| `tests/Microsoft/Xna/Framework/Audio/AudioEngineTests.cpp` | 3 `setenv` | `SDL_AUDIODRIVER=dummy` |
| `modules/renderers/headless/examples/headless_coverage_gaps_test.cpp` | 4 `setenv`, 1 `unsetenv` | `CNA_HEADLESS_MODE` (Fast/TRACE/validation/bogus) |
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

`plans/plan_dx.md`'s own `DX-15` (closed 2026-07-13, `D3D11`) found the identical gap and explicitly
deferred it: *"a real, pre-existing, much larger portability gap ... genuinely out of scope ...
worth its own separate, explicitly-scoped task."* `DX-115` (`D3D12` docs closure) repeated the same
deferral verbatim. `D9-123` (this plan) independently hit the same wall and predicted it in
advance. All three tasks agree: this is real, cross-cutting, shared-file (`tests/`) work that
doesn't belong to any one renderer's branch — fixing it inline from `feature/dx9` risks a merge
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

## Implementation result (2026-07-15)

1. All 62 call sites replaced per the pattern above, across all 13 files; `#include
   "System/Environment.hpp"` added wherever missing. `tools/audio/audio_no_hardware_harness.cpp`
   turned out to already have a working `#if defined(_WIN32)` → `_putenv_s()` branch (so it was
   never actually part of the compile blocker) — simplified to the shared wrapper anyway, removing
   the now-redundant local `#ifdef`.
2. **EasyGL regression-checked**: `cmake --build cmake-build-debug --target CnaTests` succeeds;
   the 11 affected gtest suites (`AudioEngineTest`, `FrameworkDispatcherTest`,
   `DynamicSoundEffectInstanceTest`, `SoundEffectInstanceTest`, `MicrophoneTest`, `Texture2DTest`
   + its 2 sibling suites, `SoundBankTest`, `SoundEffectTest`, `WaveBankTest`, `AudioCategoryTest`,
   `CueTest`) — **491/491 pass**, full output grepped for `FAILED` (not tail-truncated).
3. **D3D9 verified**: `cmake --build cmake-build-d3d9 --target CnaTests` — **`CnaTests.exe` now
   compiles and links with zero errors and zero remaining `setenv`/`unsetenv`** (grepped the full
   build log, not just the tail). This is the first time this binary has ever successfully built
   for any Windows-cross-compiled renderer.
4. **New, previously-unreachable finding, fixed the same day**: `ctest -L D3D9` initially failed at
   a *different*, later step — `gtest_discover_tests(CnaTests DISCOVERY_MODE PRE_TEST)`
   (`CMakeLists.txt:7117`, unconditional, no `MINGW` guard) tried to directly execute
   `CnaTests.exe` to enumerate test names at build/ctest time. It's a real PE32+ Windows binary
   (confirmed via `file`), so it cannot run natively on this Linux host (`run-detectors: unable to
   find an interpreter`) — no `CMAKE_CROSSCOMPILING_EMULATOR` routed it through Wine. This was
   invisible before because the binary never compiled far enough to reach this step under any
   Windows-cross renderer. **Fixed**: measured the naive fix's real cost first (a single Wine
   process spawn costs ~1.2s; the 4367 individually-discovered test cases would cost ~87 minutes
   of pure process overhead if each became its own separately-spawned CTest entry) before choosing
   an approach. Set `CROSSCOMPILING_EMULATOR` on the `CnaTests` target — the same CMake-native
   mechanism `plans/plan_dx.md` `DX-80`'s own `cna_directx11_ctest_command` macro already uses for D3D11/
   D3D12's own CTests — selecting the correct per-renderer Wine wrapper
   (`run-wine-dxvk9.sh`/`run-wine-dxvk.sh`/`run-wine-vkd3d.sh`) with that wrapper's own
   authenticity gate deliberately disabled inline (`env CNA_D3D9_SKIP_DXVK_GATE=1 <wrapper>`, no
   new script needed) — `CnaTests` spans non-Graphics namespaces that never open a device, so the
   gate would otherwise misreport every one of those as a fake fallback. This also automatically
   fixed `CnaInputTests`'s own separate `add_test`, no extra change needed. Deliberately did NOT
   redesign test granularity: the project's real workflow (`ctest -L D3D9`) label-filters, and none
   of the 4367 discovered cases carry that label, so they're registered but never actually invoked
   by the normal command — the 87-minute concern only applies to a hypothetical unfiltered full
   run. **Verified end-to-end**: `ctest -L D3D9` 14/14 pass (no more crash); `ctest -N` shows 4383
   total registered tests; explicitly ran 2 individual discovered cases plus `CnaInputTests` via
   `ctest -R` — genuinely execute through Wine and pass, not just register. EasyGL
   regression-checked (reconfigured + rebuilt, same spot-checks pass natively).
5. **Not independently re-verified on `D3D11`/`D3D12`'s own build directories** (not configured in
   this worktree) — `sharp-runtime`'s `Environment.cpp` was already confirmed compiling there
   before this change, and the `CROSSCOMPILING_EMULATOR` fix mirrors D3D11/D3D12's own already-
   proven `cna_directx11_ctest_command` pattern exactly, so no regression is expected, but this
   specific commit's effect there is unconfirmed. Coordinate with whoever is working
   `feature/graphics` before assuming this is also resolved there.

## Risk / size estimate

Was low-risk as predicted — no logic changes, a proven existing primitive, one call-site pattern
repeated across 13 files. The actual, only real surprise was the second `gtest_discover_tests`
blocker above, invisible until the compile blocker was actually cleared.
