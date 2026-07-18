# Audit: examples/bgfx_render_target_cube_sample_test.cpp

## Metadata

- Source file: `examples/bgfx_render_target_cube_sample_test.cpp` (138 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — RenderTargetCube-sampled-as-TextureCube-after-unbinding smoke test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_test_bgfx_render_target_cube_sample` / `Bgfx_RenderTargetCube_SampleAfterUnbind`,
  `cmake/Tests/BgfxTests.cmake:275-278`)
- XNA/FNA relevance: direct — sampling a `RenderTargetCube` as a `TextureCube` via
  `EnvironmentMapEffect.EnvironmentMap` after `SetRenderTarget(null)` is standard, load-bearing XNA usage.
- Related production code: `include/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp`
  (`IBgfxCubeSamplable`, `BgfxTextureCubeBackend`, `BgfxRenderTargetCubeBackend`),
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`DrawPrimitivesEx`'s `envMapping` branch, lines ~2636-2693 and ~3144-3201).
- Authoring commit: `6c834671` ("verify(Task P39-334): RenderTargetCube-as-TextureCube sampling, find 3
  real bugs", 2026-07-05).

## Purpose

Task 334: mirrors `bgfx_render_target_sample_test.cpp`'s Task 333 finding (2D case) one level up for
cube maps. The file's header states, as its central claim: `DrawPrimitivesEx`'s `envMapping` branch does
`static_cast<const BgfxTextureCubeBackend&>(*params.envMap)`, which is claimed to be unsafe because
`BgfxRenderTargetCubeBackend` (a `RenderTargetCube`'s real backend) is an unrelated sibling class whose
first data member (`fbo`, a `bgfx::FrameBufferHandle`) would be misread as `BgfxTextureCubeBackend`'s
first member (`handle`, a `bgfx::TextureHandle`) — the header explicitly states *"tracked as Task 874,
not fixed here."* Exit code 0 = did not crash (no pixel-correctness assertion attempted, same rationale
as the 2D sibling file).

## Executive Verdict

**Needs attention (stale claim, not a live defect)** — exactly the same shape of issue as
`bgfx_render_target_sample_test.cpp`'s F1: the bug this file explicitly says is "not fixed here" **was
in fact fixed**, by a later commit than this file's own authoring commit. The current production code no
longer performs the described `static_cast`.

## Checklist Results

### API / XNA / FNA parity
The scenario (bind each of 6 cube faces, clear, unbind, sample the whole cube via
`EnvironmentMapEffect.EnvironmentMap`) is correct, standard XNA usage, and `RenderTargetCube : TextureCube`
(confirmed `include/Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp:20`) matches FNA's inheritance
shape, so passing a `RenderTargetCube*` to `setEnvironmentMapProperty(TextureCube*)` is legitimate XNA
usage this test is right to exercise.

### Behavioral correctness / Logic — independent re-verification
Traced `DrawPrimitivesEx`'s `envMapping` branch as it exists **today** (lines 2684-2692 and the
duplicate at 3192-3200):
```cpp
if (params.envMap && bgfx::isValid(envMapSampler_))
{
    // params.envMap may be a BgfxRenderTargetCubeBackend (a sampled RenderTargetCube), ...
    if (const auto* samplable = dynamic_cast<const IBgfxCubeSamplable*>(params.envMap))
        bgfx::setTexture(1, envMapSampler_, samplable->GetBgfxCubeTextureHandle());
}
```
This is **not** the unsafe `static_cast<const BgfxTextureCubeBackend&>` the file's header describes. Both
`BgfxTextureCubeBackend` and `BgfxRenderTargetCubeBackend` implement `IBgfxCubeSamplable`
(`BgfxGraphicsBackend.hpp:178,250`), each reporting their own real handle (`handle` vs. `cubeTex`) via
`GetBgfxCubeTextureHandle()`. The interface's own doc comment (`BgfxGraphicsBackend.hpp:142-150`) states:
*"IBgfxCubeSamplable — the cube-map sibling of IBgfxSamplable (Task 907, closes Task 874):
`BgfxGraphicsBackend::DrawPrimitivesEx`'s `EnvironmentMapEffect` dispatch previously did an unsafe
`static_cast<const BgfxTextureCubeBackend&>`... the identical bug shape Task 873 already fixed for
RenderTarget2D via `IBgfxSamplable`."*

### Cross-file / git-history verification (this audit's own check)
Confirmed via `git log` that the fix (commit `5ac03715`, "feat(Task 907): implement RenderTargetCube mip
chains on Vulkan and Bgfx") post-dates this test file's own authoring commit (`6c834671`, both
2026-07-05, same day — `5ac03715` lands after both `6c834671` and the 2D fix `bda07bac`). The file's
explicit "not fixed here" statement was accurate for this exact bug at the time it was written, but Task
907 closed it shortly after, and this test's header was never updated.

### Robustness
The same "no pixel-correctness assertion is possible" framing appears here as in the 2D sibling file,
and is subject to the identical rebuttal: `bgfx_rendertarget2d_mip_test.cpp` and `bgfx_pbreffect_test.cpp`
in this same shard demonstrate real, working `GetBackBufferData` pixel readback after 3D draws on this
backend. A cube-map-specific pixel assertion (e.g. sampling the quad and checking it picked up the blue
clear color reflected via `EnvironmentMap`) is very plausibly achievable today, though this audit did not
attempt to construct one (out of scope for an audit-only task).

### Testing
The test does correctly apply the `RasterizerState::CullNone` workaround the file's own comment
identifies (Task 896 finding: the quad's CCW winding is back-facing under CNA's real default
`RasterizerState`) — this is a genuinely useful, still-accurate piece of test-authoring diligence, unlike
the stale Task 874 claim.

## Detailed Findings

### F1 — Header comment claims Task 874 ("unsafe static_cast reading RenderTargetCube's fbo as a texture
handle") is unfixed; current production code no longer contains that cast

- Severity: MEDIUM
- Confidence: HIGH (confirmed by reading the current `DrawPrimitivesEx` implementation, its
  `IBgfxCubeSamplable` doc comment explicitly cross-referencing "Task 907... closes Task 874", and
  commit-date ordering via `git log`)
- Category: test-coverage / stale-comment
- Location/symbol: file header comment (lines 6-16), specifically *"Same bug shape and severity as
  Task 873, tracked as Task 874, not fixed here."*
- Evidence: `dynamic_cast<const IBgfxCubeSamplable*>(params.envMap)` replaces the described
  `static_cast<const BgfxTextureCubeBackend&>` at both `envMapping` call sites in the current source.
- Why it matters: identical to the 2D sibling file's F1 — a reader trusting this comment would believe
  RenderTargetCube-as-TextureCube sampling is still an open, unconfirmed UB risk on Bgfx, and that the
  test is deliberately incapable of stronger verification. Both are now false. The test still provides
  residual crash/UB-regression value, but understates both the current correctness state and the
  pixel-verification capability now available in this shard.
- FNA/XNA comparison: N/A (test-authoring/documentation issue).
- Related files: `bgfx_render_target_sample_test.cpp` (identical pattern, 2D case, Task 873/878-879) and
  `bgfx_render_target_usage_test.cpp` (related pattern, Task 179) — see their own reports.
- Suggested future action (not implemented by this audit): update the header comment to reflect the
  Task 907 fix, and consider upgrading to a real pixel assertion now that the readback path is proven
  viable elsewhere in this shard.

## Cross-File Observations

- This is the third file in this exact 8-file batch (alongside `bgfx_render_target_sample_test.cpp` and
  `bgfx_render_target_usage_test.cpp`) whose header comment describes a Bgfx render-target-related bug or
  limitation that a `git log`-dated fix commit has since closed. All three predate their respective fixes
  by days to weeks — this looks like a systemic pattern of test files not being revisited after the bug
  they were written to characterize gets fixed elsewhere, rather than three independent coincidences.
- The `IBgfxSamplable`/`IBgfxCubeSamplable` pair is a clean, consistent fix shape (same interface pattern
  applied to both the 2D and cube-map cases, roughly two days apart) — good architectural consistency
  even though the tests that originally found the bugs were never updated to acknowledge the fix.

## Missing or Weak Tests

See F1 — no pixel-correctness assertion is attempted despite the underlying capability existing
elsewhere in this shard.

## Positive Findings

- The `RasterizerState::CullNone` workaround (Task 896) is correctly and currently-accurately applied —
  not every claim in this file's header is stale, only the central "not fixed" one.
- The underlying `IBgfxCubeSamplable` fix this audit verified is a clean, minimal, correctly-targeted
  interface addition (mirrors the 2D `IBgfxSamplable` fix exactly), not a hacky patch.

## Final Assessment

Same conclusion as the 2D sibling file: the feature under test is correct today, fixed by a commit that
postdates this file's authoring. The file's own "not fixed here" claim is stale and should be corrected;
this is a documentation/test-rationale issue, not a live production defect.
