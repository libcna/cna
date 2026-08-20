# Audit: cmake/Tests/WebGpuTests.cmake

## Metadata
- Source file: `cmake/Tests/WebGpuTests.cmake` (189 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake-tests` shard
- File type: CMake module (per-backend CTest registration)
- XNA/FNA relevance: N/A (build infrastructure — registers the experimental WEBGPU backend's CTest suite)
- Main related tests: ~19 `examples/webgpu_*_test.cpp` files (audited separately) plus the native smoke wrapper (`cmake/WebGPUNativeSmokeTest.cmake`)

## Purpose
Registers the WEBGPU backend's CTest suite: the native-smoke wrapper test, 2D/3D pipeline variants
(colored/textured/lit-textured/alpha-test/dual-texture/env-map/instanced/PBR/skinned/skinned-PBR),
render-target/render-target-cube/MSAA/mip-generation, and graphics-state (blend/rasterizer/
scissor/viewport) wiring.

## Executive Verdict
**Exemplary honesty, directly consistent with CLAUDE.md's own explicit instruction not to
overclaim WebGPU parity**: the `WebGPU_Msaa` registration (lines 135-148) is left registered and
**failing on purpose** — its own comment states plainly "KNOWN OPEN ISSUE, intentionally left
registered and failing rather than hidden: 3 of 6 checks in this test currently FAIL because
genuine multisample-resolved rendering does not yet work end-to-end through the real
GraphicsDevice/BasicEffect draw path, despite the infrastructure above being real and individually
verified." This is a rare and valuable choice — most projects would either delete a failing test or
quietly exclude it from the default run; here it stays a visible, honest red light in `ctest`
output until genuinely fixed.

## Checklist Results
- `WebGPU_Native2D_Smoke`'s `SKIP_REGULAR_EXPRESSION "\\[SKIP\\] CNA WebGPU native smoke"` correctly
  wires into `cmake/WebGPUNativeSmokeTest.cmake`'s own skip-vs-fail output convention (audited
  separately) — the two files' skip-detection contracts are mutually consistent.
- The `WEBGPU-52` mip-generation test's comment explicitly documents a genuine, deliberate
  divergence from FNA and every sibling CNA backend (auto-mip-regen for a plain texture, not just a
  render target being unbound) — a disclosed intentional deviation, not a silently-introduced one.
- `cna_webgpu_test()`'s runtime-library copy step correctly sets `BUILD_RPATH "$ORIGIN"` on
  UNIX-non-Apple only, matching the platform scope of the dynamic-library-relative-path convention.

## Detailed Findings
None — the one "failing" test (`WebGPU_Msaa`) is a disclosed, intentional, in-progress-tracked
state, not an undocumented defect.

## Cross-File Observations
Directly builds on `cmake/ThirdPartyWebGPU.cmake` (`CNA_WEBGPU_RUNTIME_LIBRARY`) and
`cmake/WebGPUNativeSmokeTest.cmake` (the skip-detection wrapper) — all three files' documented
contracts are mutually consistent when read together.

## Missing or Weak Tests
The `WebGPU_Msaa` 3/6-failing state is itself the disclosed gap — already tracked, not hidden.

## Positive Findings
The deliberate "registered and failing, not hidden" choice for `WebGPU_Msaa` is the standout
positive finding in this entire `build-cmake-tests` shard — a genuine model for how to handle a
known, real, in-progress limitation without either overclaiming completeness or silently deleting
the evidence.

## Final Assessment
No findings requiring action — the one visibly-failing test is intentional, disclosed, and
tracked (`plans/plan_webgpu.md`'s WEBGPU-58 row), consistent with CLAUDE.md's explicit instruction to not
describe the WebGPU backend as further along than it is.
