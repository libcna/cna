# Audit: cmake/WebGPUNativeSmokeTest.cmake

## Metadata
- Source file: `cmake/WebGPUNativeSmokeTest.cmake` (35 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake` shard
- File type: CMake script (CTest `COMMAND ${CMAKE_COMMAND} -P` wrapper)
- XNA/FNA relevance: N/A (build infrastructure — runtime skip-vs-fail wrapper for the WebGPU native smoke demo)
- Main related tests: the native WebGPU smoke demo executable (path passed via `CNA_WEBGPU_DEMO`)

## Purpose
A CTest `-P` script wrapper that runs the WebGPU native smoke demo with a 60s timeout, correctly
distinguishing "no display available" (skip) from "ran on an available desktop but failed"
(genuine failure) by pattern-matching the combined stdout/stderr against known
adapter/device-unavailable error strings.

## Executive Verdict
Correct and appropriately narrow in scope — the file's own opening comment states the core problem
precisely (CTest itself cannot know whether the executing host has a desktop display/GPU) and the
implementation matches that stated goal exactly.

## Checklist Results
- Checks both `WAYLAND_DISPLAY` and `DISPLAY` env vars before even attempting to run — avoids
  wasting the 60s timeout on a host with obviously no display server at all.
- The failure-pattern regex (`SDL_InitSubSystem.*failed|SDL_CreateWindow.*failed|unsupported SDL
  Linux video driver|adapter request failed|device request failed`) is reasonably specific to
  genuine environment-unavailability messages rather than a broad catch-all that might mask real
  application bugs as "skips."

## Detailed Findings
None. One minor observation (not a defect): if the demo's own error message wording ever changes,
this regex would need a matching update — an inherent, low-risk coupling for this kind of
output-pattern-matching skip detection, not unique to this file.

## Cross-File Observations
None directly, though this shares the same "skip vs. fail based on environment availability"
philosophy as `examples/common/PixelTestGame.hpp`'s `RunPixelTest()` sentinel-exit-code convention
(`cmake/UnitTests.cmake`'s `SKIP_RETURN_CODE 77`), just implemented via output pattern-matching
instead of a sentinel exit code (since this wraps an external native demo binary rather than a
GTest-based test).

## Missing or Weak Tests
N/A (this file IS the test wrapper).

## Positive Findings
Correctly narrow, well-targeted skip-detection logic that avoids the common failure mode of either
never skipping (false failures on headless CI) or too-broadly skipping (masking real regressions).

## Final Assessment
No findings.
