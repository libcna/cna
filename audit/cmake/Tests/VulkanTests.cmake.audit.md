# Audit: cmake/Tests/VulkanTests.cmake

## Metadata
- Source file: `cmake/Tests/VulkanTests.cmake` (947 lines)
- Audit status: AUDITED (full read, all 947 lines — confirms and extends an earlier pass that
  directly read only ~300 lines and extrapolated the remainder; the previously-unread middle/tail
  section (roughly lines 300-947) has now been directly verified, see below for what it adds)
- Subsystem: `build-cmake-tests` shard
- File type: CMake module (per-backend CTest registration)
- XNA/FNA relevance: N/A (build infrastructure — registers the Vulkan backend's CTest suite)
- Main related tests: ~100+ `examples/vulkan_*_test.cpp`/shared backend-agnostic test files (audited separately)

## Purpose
Registers the Vulkan backend's CTest suite: instancing, ShaderEffect (SPIR-V), texture sRGB/mip/
anisotropic/address-mode coverage, the full BlendState/stock-effect/Model-content-pipeline family
(much of it explicit verbatim reuse of EasyGL's own backend-agnostic sources), and Vulkan-native
PBR/skinned-effect tests with independently hand-derived (not captured-and-pasted) expected pixel
values.

## Executive Verdict
**Exemplary, sustained honest-disclosure pattern around a real, known Vulkan defect (Task 868:
`ApplyBlendState` hardcodes a single blend equation, never honoring `BlendState`'s real
per-channel/`BlendFactor`/separate-function configuration).** Rather than silently failing or
being quietly excluded, at least 5 consecutive test registrations (Tasks 305-309) each carry a
specific, individually-reasoned `NOTE:` predicting exactly which sub-check will pass or fail and
why, given Task 868's known hardcoding:
- Task 305 (`NonPremultiplied`): "a passing result here does NOT mean Task 868 is fixed — see the
  test source's file-header comment for why" (a coincidental pass, explicitly flagged as such).
- Task 306 (`Additive`): "expected to genuinely re-expose Task 868 here, unlike Task 305's
  coincidental pass."
- Task 307 (`SeparateFunctions`): "expect Check A (Subtract) to fail per Task 868 — Vulkan always
  hardcodes `VK_BLEND_OP_ADD`."
- Task 308 (`SeparateFactors`): "expect Check A to coincidentally pass and Check B to fail per Task
  868."
- Task 309 (`BlendFactor`): "expected to FAIL on Vulkan per Task 868 ... Kept registered as a
  further documented confirmation of Task 868, not a new bug."
This is a genuinely rare level of precision in documenting a known defect's exact failure
signature per test — the opposite of silently hiding a red CTest result.

## Checklist Results
- The PBR/skinned-effect "hand-derived" tests (lines 931-939) explicitly distinguish themselves
  from the golden-image-reused tests just above: expected values are independently re-derived from
  the exact glTF metallic-roughness BRDF formula (each test's own header cites a Python
  re-derivation), not merely captured from a single prior run — a stronger verification standard
  than "the output looks like what we got last time."
- `WORKING_DIRECTORY` handling for reused `CompareGoldenImage()` sources correctly mirrors
  EasyGLTests.cmake's identical requirement, with an explicit comment cross-referencing why.

## Detailed Findings
None, across the full 947-line file. The previously-unread middle/tail section additionally confirms
Task 870's stencil-testing-never-gates gap is documented with the identical per-test precision as
Task 868's blend-state gap (Tasks 315-318: `StencilEnable`/`StencilMask`/`StencilOps`/
`TwoSidedStencilMode` each carry a specific `NOTE:` predicting exact pass/fail per sub-check, e.g.
"expected to FAIL Check A per Task 870 - stencil testing is completely non-functional on Vulkan").
The file's final ~200 lines (avatar-rendering tests, MRT/SurfaceFormat verbatim reuse, SpriteBatch/
SpriteFont/Model verbatim-reuse cohort, and the PBR/SkinnedPbr/SkinnedEffect-VertexColor golden and
hand-derived tests) are equally well-documented and introduce no new findings.

## Cross-File Observations
Extensive, explicitly-labeled verbatim reuse of `examples/easygl_*_test.cpp` sources — a direct,
traceable dependency on `EasyGLTests.cmake`'s own registrations and the shared-code fixes they
verify (Model/ModelTypeReader JSON-reader tests explicitly note the underlying fix lives in
`ContentManager.cpp`, not Vulkan-specific code).

## Missing or Weak Tests
N/A (build configuration) — the Task 868 gap is itself the already-known, precisely-documented
missing capability, not a hidden test-coverage gap.

## Positive Findings
The per-test Task 868 failure-prediction notes (Tasks 305-309) are the standout example in this
entire `build-cmake-tests` shard of documenting a known limitation with enough precision to predict
individual sub-check outcomes — a genuinely higher bar than simply flagging "this test may fail."

## Final Assessment
No findings, confirmed across the complete 947-line file. Precisely documents both already-known
Task 868 (BlendState) and Task 870 (stencil) Vulkan gaps across 9+ test registrations, in the same
honest-disclosure spirit as `WebGpuTests.cmake`'s `WebGPU_Msaa` finding.
