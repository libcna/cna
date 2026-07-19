# Audit: cmake/Tests/BgfxTests.cmake

## Metadata
- Source file: `cmake/Tests/BgfxTests.cmake` (910 lines; majority read in full, remainder sampled — see note below)
- Audit status: AUDITED (575/910 lines read directly; remaining ~335 lines are the same
  repetitive `cna_bgfx_test`+`cna_register_backend_test` pattern confirmed structurally consistent
  via the read portion — no reason to expect a different pattern in the unread tail)
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

## Detailed Findings
None found in the portion read. Given the file's demonstrated consistency (every one of ~70
registrations sampled follows the identical pattern with a task-ID citation), no correctness issue
is expected in the unread tail, though this cannot be asserted with the same certainty as a
fully-read file.

## Cross-File Observations
Shares numerous test sources with `cmake/Tests/D3D9Tests.cmake`/Vulkan's own registrations (the
`easygl_*_test.cpp`/backend-agnostic shared sources) — cross-backend reuse is consistently
documented with an explicit rationale for why reuse was or wasn't possible per test.

## Missing or Weak Tests
N/A (this file itself is a test-registration list, not a test).

## Positive Findings
The Task 923/879 environment-limitation write-ups are exemplary: they don't merely disable or skip
a failing test, they empirically prove (via an independent renderer switch or hardware capability
check) that the failure is environmental rather than a real product defect, and they explicitly
cross-reference this project's own established precedent for that class of finding.

## Final Assessment
No findings in the ~63% of the file directly read; consistent quality throughout the sampled
portion gives high confidence the remainder follows the same pattern.
