# Audit: cmake/BackendSelection.cmake

## Metadata
- Source file: `cmake/BackendSelection.cmake` (267 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake` shard
- File type: CMake module
- XNA/FNA relevance: N/A (build infrastructure — selects and gates `CNA_GRAPHICS_BACKEND`)
- Main related tests: N/A (build configuration, not a test)

## Purpose
Resolves which of the 13 graphics backends (`SDL_RENDERER`, `EASYGL`, `BGFX`, `VULKAN`, `WEBGPU`,
`HEADLESS`, `SOFTWARE`, `D3D11`, `D3D12`, `CANVAS`, `ASCII`, `DX3`, `D3D9`, `SDL_GPU`) to build,
supporting both the direct `CNA_GRAPHICS_BACKEND` cache variable and a legacy
`CNA_BACKEND_<NAME>` boolean-option style (with a hard error if more than one boolean is set), then
sets `BACKEND_DIR`/`BACKEND_TARGET`/`CNA_BACKEND_DEFINE` per selection with platform hard-gates for
Windows-only (D3D9/11/12) and Emscripten-only (CANVAS) backends.

## Executive Verdict
Mostly correct and clearly structured, with one real reproducibility concern. The
`_cna_enabled_backends_count EQUAL 1` guard is a sound safety check against ambiguous configuration
when the legacy boolean-option style is used. Platform gates (D3D9/11/12 requiring Windows, CANVAS
requiring Emscripten) are hard `FATAL_ERROR`s with clear, actionable remediation instructions (e.g.
pointing at the mingw-w64 toolchain file) rather than silently producing an unbuildable
configuration. However, the BGFX backend's own `FetchContent_Declare(bgfx_cmake ... GIT_TAG
master ...)` pins a floating branch rather than a fixed commit/tag — see Detailed Findings.

## Checklist Results
- Sibling-repository existence checks (`easy-gl`, `free-direct`) fail loudly with the exact `git
  clone` command needed, rather than a generic CMake "directory does not exist" error.
- `BGFX`'s non-Linux platform check is correctly a soft `WARNING` (not `FATAL_ERROR`), consistent
  with it being "primarily tested on" rather than "only buildable on" Linux — a meaningfully
  different (and correct) severity choice from the D3D9/11/12/CANVAS hard gates.
- `FetchContent_Declare(bgfx_cmake GIT_REPOSITORY ... GIT_TAG master ...)` (in the BGFX branch of
  the backend-configuration `if/elseif` chain) pins the floating `master` branch rather than a fixed
  commit or release tag. This means two BGFX-backend configures performed at different times can
  silently resolve to different upstream `bgfx.cmake`/bgfx commits, with no recorded version to
  bisect against if a regression appears — a real build-reproducibility gap, in direct contrast to
  this same shard's `cmake/ThirdPartyWebGPU.cmake`, which pins an exact, cached
  `CNA_WEBGPU_VERSION` ("v29.0.1.1") for the equivalent "fetch a third-party graphics dependency"
  problem.

## Detailed Findings
- **MEDIUM** — BGFX's `FetchContent_Declare` uses `GIT_TAG master` (a floating branch), not a
  pinned commit/tag. Confirmed by direct read of the BGFX configuration block in this file.

## Cross-File Observations
Feeds `BACKEND_DIR`/`BACKEND_TARGET` into `cmake/BackendLibraries.cmake`'s per-backend link-setup
logic. The `GIT_TAG master` finding above contrasts directly with `cmake/ThirdPartyWebGPU.cmake`'s
pinned-version design (see that file's own audit report) — worth flagging together since both files
solve the same class of problem with different reproducibility guarantees.

## Missing or Weak Tests
N/A (build configuration).

## Positive Findings
Consistently actionable `FATAL_ERROR` messages (exact remediation command included) rather than
bare failure descriptions.

## Final Assessment
1 MEDIUM finding: BGFX's `FetchContent_Declare` uses `GIT_TAG master` (a floating branch), not a
pinned commit/tag — a real build-reproducibility gap relative to this project's own more disciplined
pattern used for WebGPU and the vendored submodules.
