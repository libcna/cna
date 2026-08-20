# Audit: cmake/Tests/Dx3Tests.cmake

## Metadata
- Source file: `cmake/Tests/Dx3Tests.cmake` (69 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake-tests` shard
- File type: CMake module (per-backend CTest registration)
- XNA/FNA relevance: N/A (build infrastructure — registers the DX3/DirectDraw backend's CTest suite)
- Main related tests: `examples/dx3_smoke_test.cpp`, `dx3_texture_rendertarget_test.cpp`,
  `dx3_spritebatch_test.cpp`, `dx3_blend_test.cpp`, `dx3_sampling_test.cpp`,
  `dx3_spritefont_test.cpp`, `dx3_no3d_test.cpp`, `dx3_graphics_capability_test.cpp`,
  `dx3_logical_transform_test.cpp` (all 9 `examples-tests-dx3` shard files, each registered here)

## Purpose
Registers all 9 DX3 backend CTest executables, each phase-tagged against `plans/plan_dx3.md`.

## Executive Verdict
**High-value cross-check confirmed**: `Dx3_SpriteBatch` (line 30-32) IS registered as a genuine
CTest here (`cna_test_dx3_spritebatch` built from `examples/dx3_spritebatch_test.cpp`,
`TIMEOUT 30`, `LABELS "DX3"`) — meaning this project's own persistent memory record of a confirmed
real bug (`dx3_spritebatch_test.cpp` has 2/10 checks failing at runtime, one a genuine rotation-math
bug, the other likely a test-authoring premultiplied-alpha issue) is NOT hidden by any
registration-level exclusion in this file. If that failure is not currently blocking CI, the cause
lies either in the test binary's own exit-code handling or in whether `ctest -L DX3` is actually
run in CI (not verified in this build/CI shard's own scope), not in this CMake file silently
skipping the test.

## Checklist Results
- `SDL_VIDEODRIVER=dummy` is used uniformly across all 9 DX3 registrations (unlike the ASCII
  backend's `x11`+real `CNA_TEST_DISPLAY`) — `Dx3_LogicalTransform`'s own comment (lines 60-64)
  explicitly documents the consequence (the dummy driver reports a fixed 1024x768 size, so the
  letterbox-invariant check runs against that fixed size rather than a real display) and correctly
  characterizes this as still a genuine, non-trivial verification, not a degraded/meaningless one.
- The 2D-only `Dx3_GraphicsCapability` test mirrors the equivalent Canvas backend registration
  pattern (`SupportsCapability()` reporting `ThreeD` and dependents as absent).

## Detailed Findings
None in this file itself — see Executive Verdict for the cross-check confirming the known
SpriteBatch defect is genuinely wired into CTest, not silently excluded.

## Cross-File Observations
Directly relevant to this session's own prior finding
(`project_dx3_spritebatch_test_failure` context): confirms `Dx3_SpriteBatch` is a real, live CTest
registration — the bug's visibility depends on the test binary's/CI's own behavior, not this file.

## Missing or Weak Tests
N/A (build configuration).

## Positive Findings
`Dx3_LogicalTransform`'s honest disclosure of the dummy-driver's fixed-size limitation is a good
example of documenting a real test-environment constraint rather than presenting the check as
equivalent to running against a real display.

## Final Assessment
No new findings in this file; confirms (does not itself resolve) the already-known
`Dx3_SpriteBatch` failure is genuinely registered as a CTest, not silently skipped at the build
level.
