# Audit: cmake/ThirdPartySDL.cmake

## Metadata
- Source file: `cmake/ThirdPartySDL.cmake` (291 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake` shard
- File type: CMake module
- XNA/FNA relevance: N/A (build infrastructure — vendored SDL3/SDL3_image/SDL3_mixer configure-time build)
- Main related tests: N/A (build configuration, consumed by nearly every CNA target)

## Purpose
Builds and installs SDL3/SDL3_image/SDL3_mixer from vendored submodules at CMake-configure time
via `execute_process()`, into a persistent, platform/arch-keyed prebuilt-install root that survives
`cmake --build --clean-first` and build-tree deletion; also provides `cna_copy_mingw_runtime`/
`cna_copy_sdl_runtime` helpers for copying runtime DLLs next to Windows executables.

## Executive Verdict
Correct and thoughtfully designed. The prebuilt-install-root path being deliberately OUTSIDE the
CMake build tree (so a clean build doesn't force an expensive multi-minute SDL rebuild) is a sound,
clearly-documented design choice, and the non-recursive submodule-init guidance (avoiding ~19
unneeded nested codec submodules) matches this project's own established memory/precedent from
elsewhere in the audit.

## Checklist Results
- Every optional SDL_image/SDL_mixer codec is explicitly disabled (`SDLIMAGE_AVIF=OFF`,
  `SDLMIXER_GME=OFF`, etc.) — a deliberately minimal, dependency-free build rather than pulling in
  unused codec libraries.
- Each `execute_process()` call (configure/build/install) checks `RESULT_VARIABLE` and fails loudly
  with `FATAL_ERROR` on a nonzero exit code — no silent partial-build state possible.
- Android's sub-build correctly re-passes `ANDROID_ABI`/`ANDROID_PLATFORM`/`ANDROID_STL` explicitly,
  with a clear comment explaining why (each SDL sub-build is a fully separate `cmake` invocation
  that does NOT inherit the parent configure's cache automatically) — a genuinely non-obvious CMake
  behavior correctly identified and handled.
- `cna_copy_mingw_runtime`'s two-tier lookup (compiler `-print-file-name` first, falling back to a
  path derived from the compiler's own directory) with a `WARNING` (not `FATAL_ERROR`) if neither
  finds `libwinpthread-1.dll` is a sensible graceful-degradation choice — a missing runtime DLL is a
  real deployment problem worth warning about, but not one that should block the build itself.

## Detailed Findings
None.

## Cross-File Observations
`cna_copy_sdl_runtime`/`cna_copy_mingw_runtime` are called from nearly every Windows-targeted
executable registration in `cmake/Examples.cmake` and `cmake/UnitTests.cmake`.

## Missing or Weak Tests
N/A (build configuration).

## Positive Findings
The Android sub-build cache-variable re-propagation detail (explicitly explained, not just present)
is a strong example of documenting a genuinely subtle CMake gotcha rather than leaving future
maintainers to rediscover it via a broken ARM32 build.

## Final Assessment
No findings.
