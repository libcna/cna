# Audit: examples/sdlgpu_renderstate_test.cpp

## Metadata

- Source file: `examples/sdlgpu_renderstate_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — dynamic `BlendState`/`DepthStencilState`/
  `RasterizerState` proof for the SDL_GPU backend
- File type: standalone `Game`-subclass executable, CTest-registered (`SdlGpu_RenderState`,
  `cmake/Tests/SdlGpuTests.cmake:102-104`, `TIMEOUT 60 LABELS "SdlGpu"`)
- XNA/FNA relevance: direct — `BlendState.Additive`/`AlphaBlend`, `DepthStencilState.
  StencilFunction`/`StencilPass`/`ReferenceStencil`/`StencilWriteMask`/`StencilMask`,
  `RasterizerState.CullMode`/`FillMode`/`ScissorTestEnable`, `GraphicsDevice.ScissorRectangle`.
- FNA reference: `Graphics/States/BlendState.cs`, `Graphics/States/DepthStencilState.cs`,
  `Graphics/States/RasterizerState.cs`, `Graphics/States/CullMode.cs`.
- Related production code: `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
  (`ToBlendFactor`/`ToBlendOp`/`ToCullMode` lines 82-131, `FillBlendState`/
  `FillDepthStencilState`/`FillRasterizerState` lines 198-253, `ApplyScissorForPass` lines
  1210-1231), `src/Microsoft/Xna/Framework/Graphics/BlendState.cpp` (preset definitions, lines
  6-9).

## Purpose

Eight-check (A-H), 120-frame test proving that `GraphicsDevice.BlendState`/`DepthStencilState`/
`RasterizerState`/`ScissorRectangle` genuinely reach SDL_GPU's pipeline-baked state, replacing what
the header comment (lines 2-5) says was, before this task, a hardcoded Opaque/CullNone/Solid/
no-stencil pipeline regardless of the game's requested state. Every check asserts an exact
`RenderTarget2D::GetData()` pixel value chosen so the *old* hardcoded behavior would read back a
visibly different, wrong result — not a "didn't throw" placebo.

## Executive Verdict

**Mostly healthy.** Every check's expected pixel value was independently traced against the actual
production `ToBlendFactor`/`ToCullMode`/`FillDepthStencilState` mappings and found correct
(including a full ordinal-by-ordinal re-verification of `ToBlendFactor` against the `Blend` enum
and `ToCullMode` against this project's own documented EasyGL-mirroring convention). One real
test-design gap found: Check A's chosen scene (a fully-opaque source color) cannot actually
distinguish XNA's real `BlendState.Additive` formula (`ColorSourceBlend=SourceAlpha`) from a
hypothetical, wrong `ColorSourceBlend=One` implementation, since alpha=1 collapses the two to the
same numeric result — see F1.

## Checklist Results

### API / XNA / FNA parity

Verified `BlendState::Additive`/`AlphaBlend` (`BlendState.cpp` lines 6-7) against FNA's own preset
definitions (`FNA/src/Graphics/States/BlendState.cs` lines 164-178): `Additive =
{ColorSourceBlend=SourceAlpha, AlphaSourceBlend=SourceAlpha, ColorDestinationBlend=One,
AlphaDestinationBlend=One}` and `AlphaBlend = {One, One, InverseSourceAlpha,
InverseSourceAlpha}` — **both match FNA exactly**, field-for-field (this is a real, if easy to
overlook, XNA behavior: `BlendState.Additive`'s source factor is `SourceAlpha`, not the naively
assumed `One`). Also independently re-derived `ToBlendFactor`'s full 12-case switch (lines 82-100)
against the `Blend` enum's real C++ ordinals (`Blend.hpp`: `One=0, Zero=1, SourceColor=2,
InverseSourceColor=3, SourceAlpha=4, InverseSourceAlpha=5, DestinationColor=6,
InverseDestinationColor=7, DestinationAlpha=8, InverseDestinationAlpha=9, BlendFactor=10,
InverseBlendFactor=11, SourceAlphaSaturation=12`) — every case number correctly matches its
corresponding SDL_GPU blend factor; the `default` (ordinal 0, `One`) correctly maps to
`SDL_GPU_BLENDFACTOR_ONE`.

### Behavioral correctness

- **Check A** (Additive, lines 474-476): Red bg + opaque Green(0,128,0,255) draw. Re-derived: with
  the real formula `Src·SrcAlpha + Dst·One`, `SrcAlpha=1.0` (opaque draw) makes
  `Src·SrcAlpha=Src` unchanged, so the result is `(0,128,0)+(255,0,0)=(255,128,0)` — **matches**
  `Color(255,128,0)` (line 475) exactly, and this is also what a naively-wrong `ColorSourceBlend=
  One` implementation would produce for this specific (fully opaque) scene. See F1.
- **Check B** (AlphaBlend, lines 478-480): White bg + half-alpha (128/255≈0.502) Black draw. Real
  formula: `Black·1 + White·(1-0.502)=0+255·0.498≈127`, matching `Color(127,127,127,16)`'s
  tolerance (line 479) — and here, since the source is pure black, only the destination factor
  (`InverseSourceAlpha`) actually determines the result, so this check *does* genuinely
  discriminate the correct non-degenerate formula, unlike Check A.
- **Check C/G/H** (stencil, lines 180-276): re-derived the three-stage write/read pattern
  (`StencilFunction=Always`/`Replace`/`Ref=1` write, then `Equal`/`Ref=1` reveal) against
  `FillDepthStencilState` (lines 218-243) — `compare_mask`/`write_mask` are genuinely threaded from
  `rs.stencil.readMask`/`writeMask` (lines 226-227), not hardcoded `0xFF`, confirming Check G
  (`StencilWriteMask=0x00` blocks a write) and Check H (`StencilMask=0x00` masks a deliberate
  `ReferenceStencil` mismatch away) both exercise real, non-hardcoded state.
- **Check D** (CullMode, lines 279-307): confirmed `ToCullMode`'s mapping
  (`CullClockwiseFace=1→BACK`, `CullCounterClockwiseFace=2→FRONT`, with `front_face` hardcoded
  `COUNTER_CLOCKWISE`, lines 123-131, 251-252) against this file's own comment (lines 129-136)
  documenting the CW-wound quad convention this scene relies on (no camera-induced winding flip,
  unlike `sdlgpu_3d_test.cpp`'s LookAt+Orthographic camera). Cross-checked this mapping's rationale
  against `plans/plan_sdlgpu.md`'s SDLGPU-20 row, which confirms it deliberately **mirrors this project's
  own EasyGL backend's hardware-validated cull convention**, not Vulkan's own (documented,
  Task-870) front/back-swapped convention — an intentional, cited divergence between two SdlGpu
  sibling backends' internal tables, not an inconsistency, since SDL_GPU's 3D shaders (like
  EasyGL's) need no NDC Y-flip that Vulkan's do. The observable culling *outcome* for identical
  input geometry was independently confirmed to match Vulkan's own `ToCullMode` mapping too (traced
  `VulkanGraphicsBackend.cpp` lines 3255-3265: `frontFace=CLOCKWISE`, opposite hardcode, opposite
  `BACK`/`FRONT` assignment) — the two backends' internal tables are individually "flipped" from
  each other but produce matching visible behavior for the same winding, which is the actually
  load-bearing property.
- **Check E** (FillMode, lines 311-331): triangle centroid at `(0,0)` (render-target centre)
  independent of NDC Y orientation (comment lines 310, 438) — correctly avoids coupling this check
  to the same Y-flip question Check D's comment discusses.
- **Check F** (Scissor, lines 340-364): the ordering constraint documented in the file's own
  comment (lines 333-339, 342-346) was independently traced and confirmed load-bearing, not
  defensive boilerplate — see Logic section.

### Logic

`ApplyScissorForPass` (`SdlGpuGraphicsBackend.cpp` lines 1210-1231) applies **one global, live**
`scissorEnabled_`/`scissorX/Y/W/H_` state per render pass at flush time, not a per-`DrawCommand`
snapshot. `RunScissorCheck`'s forced early flush (`(void)ReadPixel(*rtAdditive_, 0, 0);`, line 347)
is therefore not defensive noise: without it, the *first* `GetData()` call of the frame (which
would otherwise be this check's own `scissorLeft_`/`scissorRight_` read) would trigger
`EnsureFrameRendered()` while scissor is already enabled, retroactively applying Check F's own
scissor rect to every earlier check's still-pending render pass (`rtAdditive_`, `rtStencil_`, the
cull/fill-mode targets, etc. — none of which had been flushed yet, since nothing forces an eager
flush before this point). Confirmed the forced flush genuinely runs while `scissorEnabled_` is
still false (every earlier check's own `RasterizerState()` reset, e.g. line 306/330, restores
`scissorTestEnable_=false`), and confirmed the second, real flush (triggered by
`ReadPixel(*rtScissor_,...)`, lines 359-360) only has `rtScissor_` pending in
`usedRenderTargetsThisFrame_` at that point (the first flush already cleared everything else) — so
the live scissor state at that second flush correctly applies only to `rtScissor_`'s own pass. This
is a subtle, non-obvious interaction between a documented architectural simplification
(process-wide, not per-draw, scissor state) and multi-target test ordering, and it is correctly
handled.

### C++ correctness

`Check(bool, const char*)` (lines 123-127) takes a `const char*` but several call sites pass a
temporary `std::string::c_str()` (e.g. line 468, `(std::string(...) + ...).c_str()`) — the
temporary's lifetime extends to the end of the full expression containing the `Check(...)` call,
so the pointer remains valid for the duration of the call; no dangling-pointer risk despite the
raw-pointer parameter type.

### Cross-file consistency

`ToBlendFactor`/`ToCullMode` production mappings were independently verified correct (see API
section) rather than assumed from the test's own passing status — this closes out the "does this
backend's own render-state translation table actually match XNA's ordinals" question this
checklist's Testing section calls for.

## Detailed Findings

### F1 — Check A (`BlendState.Additive`) cannot distinguish the real XNA formula from a hypothetical `ColorSourceBlend=One` regression, because its scene uses a fully-opaque source color

- Severity: LOW
- Confidence: HIGH (re-derived both formulas by hand; they are provably identical whenever
  `SrcAlpha=1`, which this check's `Color::Green` (opaque, `A=255`) always is)
- Category: test-coverage
- Location/symbol: `RunBlendChecks` (lines 162-177), specifically `fullQuadGreenVb_` drawn under
  `BlendState::Additive`; Check A assertion (lines 474-476)
- Evidence: XNA's real `BlendState.Additive` is `ColorSourceBlend=SourceAlpha,
  ColorDestinationBlend=One` (confirmed against FNA's own preset, see API section above) — i.e.
  the source color is scaled by its own alpha before being added to the destination. This check's
  source draw uses `Color::Green` at full opacity (`A=255`, `SrcAlpha=1.0`), so
  `Src·SrcAlpha=Src·1=Src` regardless of whether the real `SourceAlpha` factor or a hypothetically
  wrong hardcoded `One` factor were used — both produce the identical `(255,128,0)` result this
  check asserts.
- Why it matters: a regression that silently changed this backend's `ColorSourceBlend` handling for
  `Additive` specifically (e.g. a copy-paste that hardcoded `One` instead of consulting
  `rs.blend.colorSrc`) would not be caught by this check, even though the check's own header intent
  ("proves BlendState is no longer hardcoded to Opaque") suggests it validates the full blend-state
  pipeline. The production code was independently confirmed correct in this audit (see API
  section), so this is a coverage gap, not a live defect.
- FNA/XNA comparison: N/A — the underlying `ToBlendFactor`/`FillBlendState` production logic was
  independently confirmed correct against FNA's own `Blend`/`BlendState` definitions; this finding
  is purely about the check's own discriminating power.
- Related files: none outside this test file.
- Suggested future action (not implemented by this audit): draw the Additive quad at a partial
  alpha (e.g. `Color(0,128,0,128)`) so the expected result (`Red + Green·0.5 = (255,64,0)`) would
  only be produced by a `ColorSourceBlend=SourceAlpha`-correct implementation, and a
  `ColorSourceBlend=One` regression would visibly read back `(255,128,0)` instead — genuinely
  falsifiable.

## Cross-File Observations

- `plans/plan_sdlgpu.md`'s SDLGPU-19 row (secondary context per D-3) confirms Checks G/H's own claimed
  regression-worthiness was empirically git-stash-verified: reverting `FillDepthStencilState`'s
  mask-application lines back to hardcoded `0xFF` reproduced the exact predicted G/H failures, and
  restoring the fix returned the full suite to 16/16 — the same row also documents a *second* real
  bug this task found and fixed along the way (`GraphicsDevice.ReferenceStencil` was captured but
  never reached the GPU via `SDL_SetGPUStencilReference` until this task added it), independently
  corroborating Check C's own "temporarily disabling the new call reproduced the original bug"
  claim.
- Fog and skinned-normal-transform cross-cutting bugs are **not applicable**: every draw in this
  file uses `BasicEffect`+`VertexColorEnabled` with `FogEnabled` never set (`colored3d.frag.glsl`
  implements no fog term at all), and no `SkinnedEffect` code path is exercised.
- `SDLGPU-20`'s plan-doc row explicitly documents `RasterizerState.DepthBias`/`SlopeScaleDepthBias`
  as captured-but-not-applied (a genuine SDL_gpu API-surface limitation: no per-draw dynamic
  depth-bias equivalent to Vulkan's `vkCmdSetDepthBias`) — this file does not exercise depth bias
  at all, so this limitation is out of scope for this specific report, but is worth flagging for
  whichever file (none in this batch) eventually tests `RasterizerState.DepthBias`.

## Missing or Weak Tests

- See F1 — Check A's scene cannot discriminate the specific `ColorSourceBlend=SourceAlpha` XNA
  behavior from a naive `One` implementation.
- `RasterizerState.DepthBias`/`SlopeScaleDepthBias` are captured by this backend but never applied
  (per `plans/plan_sdlgpu.md`'s own documented, deliberate scope boundary) — no test in this shard
  exercises or documents that gap at the test level; not a defect in this file, but worth noting
  since this is the file that would most naturally host such a check.

## Positive Findings

- `ToBlendFactor` was independently verified correct against all 12 `Blend` enum ordinals — a full
  parity check, not a spot-check.
- `BlendState::Additive`/`AlphaBlend` presets were independently confirmed to match FNA's real
  field values exactly, including the easy-to-miss `SourceAlpha` (not `One`) source factor for
  `Additive`.
- The scissor-check ordering constraint (documented in the file's own comment) was independently
  traced through `ApplyScissorForPass`'s actual live-state-at-flush-time semantics and confirmed
  both necessary and correctly satisfied — a genuinely subtle piece of test design that holds up
  under scrutiny.
- `ToCullMode`'s divergence from the sibling Vulkan backend's own table is explicitly documented
  in both the production code's own comment and `plans/plan_sdlgpu.md`, and was independently confirmed
  to still produce matching observable behavior across the two backends for identical input
  geometry — not an unexplained inconsistency.

## Final Assessment

A strong, wide-coverage render-state test (8 checks across blend/stencil/cull/fill/scissor) whose
production mappings were independently re-verified correct in full. One real, low-severity
test-design gap found (F1: Additive's discriminating power is accidentally defeated by using an
opaque source color) — worth a one-line fix (partial alpha) if this file is revisited, but not
indicative of any live defect in the backend itself.
