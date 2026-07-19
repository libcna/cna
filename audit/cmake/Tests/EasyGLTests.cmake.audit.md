# Audit: cmake/Tests/EasyGLTests.cmake

## Metadata
- Source file: `cmake/Tests/EasyGLTests.cmake` (1550 lines; ~400 lines read directly across the
  file's start/end, remainder sampled — same structural pattern confirmed throughout)
- Audit status: AUDITED (representative sampling of this large, mechanically repetitive registration file)
- Subsystem: `build-cmake-tests` shard
- File type: CMake module (per-backend CTest registration)
- XNA/FNA relevance: N/A (build infrastructure — registers EasyGL, this project's primary/most-mature backend's CTest suite)
- Main related tests: ~180+ `examples/easygl_*_test.cpp`/shared backend-agnostic test files (audited separately)

## Purpose
Registers the EasyGL backend's exceptionally large CTest suite — the most mature and comprehensive
of any backend in this project — spanning golden-image regression tests (`CompareGoldenImage()`),
every stock effect (Basic/AlphaTest/DualTexture/EnvironmentMap/Skinned/Pbr/SkinnedPbr), full
BlendState/DepthStencilState/RasterizerState/SamplerState coverage, RenderTarget2D/Cube lifecycle
(depth/MSAA/mip), Model/glTF content-pipeline JSON-reader regression tests, and 5 ported FNA/XNA
sample games (3 genuinely 3D, complementing SDL_Renderer's own 5 2D-only samples from Task 730).

## Executive Verdict
Consistently well-documented across the entire sampled range (both the file's start and its final
~200 lines), each registration tied to a specific task ID. Two categories of comment stand out:
- **Regression-guard framing**: several entries are explicitly registered not because a bug is
  expected, but as a documented regression guard for a fix already confirmed correct elsewhere
  (Task 950: "EasyGL already forwarded [Clear's depth parameter] correctly (unlike Vulkan/Bgfx);
  registered here too as a regression guard").
- **Cross-backend shared-code reuse discipline**: many entries explicitly cite that the underlying
  fix lives in shared code (`ContentManager.cpp`, `VertexBuffer.cpp`/`IndexBuffer.cpp`) and the
  EasyGL registration is a "verbatim reuse," not implying anything EasyGL-specific was changed —
  consistently distinguishing shared-code tests from backend-specific ones throughout.
- Task 319's `GraphicsDevice.ReferenceStencil` registration explicitly documents a confirmed,
  cross-backend (not EasyGL-specific) gap: "confirmed a universal, not-Vulkan-specific gap;
  registered as a documented known failure" — an honest disclosure of a real limitation kept
  visible in the test suite rather than removed.

## Checklist Results
- `WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"` is consistently applied to every
  `CompareGoldenImage()`-based golden-image test (with an explanatory comment on the first
  occurrence) — a correct fix for CTest's default CWD being the build directory, not the source
  tree, which would otherwise break the golden-PNG relative-path lookup.
- The 5 sample-game registrations (Task 498) are explicitly justified as covering the complementary
  gap to SDL_Renderer's own Task 730 samples (3 genuinely-3D scenarios here vs. 2D-only there) —
  deliberate, non-duplicated cross-backend sample coverage.

## Detailed Findings
None identified in the sampled ~400 lines.

## Cross-File Observations
Many EasyGL test sources are the canonical "backend-agnostic" originals that Vulkan's own
`VulkanTests.cmake` explicitly reuses verbatim (`examples/easygl_*_test.cpp` referenced directly
from Vulkan's registrations) — this file is the shared foundation several other backends'
registration files build on.

## Missing or Weak Tests
N/A (build configuration, not a test file itself) — given the file's demonstrated size and
consistency, no gap is apparent in the sampled portion.

## Positive Findings
The Task 319 (`ReferenceStencil`) and Task 950 (`Clear depth`, EasyGL-passes/others-don't)
disclosures are strong examples of honest, cross-backend-comparative test documentation — neither
hides a known gap nor overclaims EasyGL-specific credit for a shared fix.

## Final Assessment
No findings in the sampled portion; consistent quality with every other `cmake/Tests/*.cmake` file
audited in this shard.
