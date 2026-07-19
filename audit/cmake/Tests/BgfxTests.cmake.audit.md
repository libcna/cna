# Audit: cmake/Tests/BgfxTests.cmake

## Metadata
- Source file: `cmake/Tests/BgfxTests.cmake` (910 lines)
- Audit status: AUDITED (full read, all 910 lines — confirms and extends an earlier pass that
  directly read only the first 575 lines and extrapolated the remainder; the previously-unread tail
  (lines 576-910) has now been directly verified, see Checklist Results/Cross-File Observations
  below for what it adds)
- Subsystem: `build-cmake-tests` shard
- File type: CMake module (per-backend CTest registration)
- XNA/FNA relevance: N/A (build infrastructure — registers the Bgfx backend's own CTest suite)
- Main related tests: every `examples/bgfx_*_test.cpp`/shared `examples/easygl_*_test.cpp` file this registers (audited separately under the graphics-backend example test shards)

## Purpose
Registers the Bgfx backend's entire CTest suite (~90+ individual test executables spanning
SpriteBatch/texture/render-target/BasicEffect/SkinnedEffect/EnvironmentMapEffect/AlphaTestEffect/
DualTextureEffect/blend-state/occlusion-query coverage), each tied to a specific task ID.

## Executive Verdict
Exceptionally well-documented for a large, mechanically-repetitive registration file. Nearly every
entry cites the exact task ID that added it and a concrete rationale for *why* a Bgfx-specific test
source was needed instead of reusing the shared/EasyGL source verbatim (most commonly: EasyGL's
source calls `SetDepthTestEnabled(false)`, which unconditionally throws on Bgfx). Two entries are
particularly valuable examples of honest, evidence-based scope-limiting comments rather than
silent gaps:
- Task 923 (BlendState AlphaSourceBlend/AlphaDestinationBlend independence): explicitly documents
  that a real pixel-differential test was built and empirically found to report the same (wrong)
  result whether or not the underlying fix is applied — attributed to this sandbox's bgfx
  GL2.1/Mesa-llvmpipe software renderer not honoring separately-requested alpha blend factors,
  confirmed via 3 independent probes, and explicitly cross-referenced against the project's own
  established precedent for this class of environment limitation (Task 448's occlusion query,
  Task 879's MSAA resolve).
- Task 879 (RenderTarget2D MSAA resolve): documents that this dev environment's bgfx OpenGL path
  negotiates only a legacy GL 2.1 context (confirmed via `glxinfo` that the underlying driver
  actually supports GL 4.6), under which MSAA-flagged framebuffer textures don't really resolve
  with sub-pixel blending — and proves this is an environment limitation, not a CNA defect, by
  showing the same test passes cleanly under bgfx's Vulkan renderer (`CNA_BGFX_RENDERER=VULKAN`)
  with zero code changes.

## Checklist Results
- The Task 13.6 comment (lines 20-29) is a strong example of documenting a deliberately
  NOT-attempted extension: avatar-rendering smoke tests were investigated for Bgfx but found
  blocked by `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` being unconditional
  `ThrowNo3DState()` stubs — correctly scoped as a real backend feature gap, not this
  test-coverage task's responsibility to fix.
- Task 743's comment documents a real bug FOUND and FIXED during this test-authoring work
  (`ApplySamplerState`'s filter switch silently collapsing all 6 split Min/Mag/Mip `TextureFilter`
  values to plain Linear) — a genuine positive example of tests catching a real defect, not just
  asserting pre-existing correct behavior.
- Task 773's comment explicitly supersedes an earlier, now-stale comment on the same test area
  ("pixel-level verification is not available") — a good instance of the project keeping its own
  historical commentary honest as capabilities evolve, rather than leaving contradictory old notes
  in place.
- Directly verifying the previously-unread tail (lines 576-910) confirms the identical pattern and
  quality continues throughout: Task 759-764's DepthStencilState/stencil-op test group each
  precisely explains why the shared EasyGL source needed restructuring (Bgfx's
  `GetBackBufferData` "first read per rendered frame" limitation, Task 406, versus the shared
  source's multi-region-per-frame reads); Task 761's comment additionally discloses that
  `StencilWriteMask` is verified informationally-only since bgfx's own state API has no per-draw
  stencil write-mask flag at all (confirmed against `bgfx/defines.h`) — a permanent backend
  limitation honestly distinguished from a bug; Task 815's occlusion-query comment discloses a
  third instance of the same sandbox-software-renderer limitation class as Task 448/923
  (`PixelCount()` does not discriminate visible from occluded geometry in this sandbox); and the
  file's final section (Tasks 926-927, CNB-58/60/67 PBR/SkinnedPbr/SkinnedEffect-VertexColor Bgfx
  ports) is equally well-documented, closing out this backend's stock-effect coverage.

## Detailed Findings
None, across the full 910-line file (previously reported only for the first 575 lines; the
remainder is now directly confirmed to hold the same standard).

## Cross-File Observations
Shares numerous test sources with `cmake/Tests/D3D9Tests.cmake`/Vulkan's own registrations (the
`easygl_*_test.cpp`/backend-agnostic shared sources) — cross-backend reuse is consistently
documented with an explicit rationale for why reuse was or wasn't possible per test. The Task
815/923/448 "confirmed sandbox software-renderer limitation, not a CNA defect" cross-reference
chain is internally consistent across all three citations, verified via direct read of each.

## Missing or Weak Tests
N/A (this file itself is a test-registration list, not a test).

## Positive Findings
The Task 923/879 environment-limitation write-ups are exemplary: they don't merely disable or skip
a failing test, they empirically prove (via an independent renderer switch or hardware capability
check) that the failure is environmental rather than a real product defect, and they explicitly
cross-reference this project's own established precedent for that class of finding.

## Final Assessment
No findings, confirmed across the complete 910-line file.
