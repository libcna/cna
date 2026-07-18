# Audit: examples/bgfx_dualtextureeffect_doubling_test.cpp

## Metadata

- Source file: `examples/bgfx_dualtextureeffect_doubling_test.cpp` (162 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `DualTextureEffect` two-texture-blend / `color.rgb *= 2`
  doubling-factor pixel test, first-ever Bgfx pixel-integration test for `DualTextureEffect`
  (per the file's own header comment).
- CTest registration: `cna_bgfx_test(cna_test_bgfx_dualtextureeffect_doubling …)` /
  `cna_register_backend_test(NAME Bgfx_DualTextureEffect_Doubling …)`
  (`cmake/Tests/BgfxTests.cmake:390-392`).
- XNA/FNA relevance: direct — `DualTextureEffect.Texture`/`Texture2`/`DiffuseColor`.
- FNA reference: `HLSL/DualTextureEffect.fx` `PSDualTexture`: `color.rgb *= 2; color *= overlay *
  pin.Diffuse;`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/DualTextureEffect.cpp`
  (`FillGpuDrawParams()` lines 248-275), `src/CNA/Internal/Backends/Bgfx/shaders/fs_dual_texture3d.sc`
  (`base.rgb *= 2.0; color = base * texture2D(s_texColor2,...) * v_color0;`),
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (dual-texture dispatch branch,
  lines 2411-2479).

## Purpose

Two-check pixel test isolating FNA's `color.rgb *= 2` RGB-doubling factor on `Texture` (slot 0)
before it is multiplied by `Texture2` (slot 1) and `DiffuseColor`: (a) `Texture`=gray(100),
`Texture2`=white, `DiffuseColor`=white — isolates the doubling factor alone (100×2≈200); (b) both
textures white, `DiffuseColor`=red — the original task-title regression case (a missing `*2`
would still clamp to the same saturated result for pure 0/1 textures, which is exactly why the
comment explains this bug was invisible to every earlier `DualTextureEffect` test).

## Executive Verdict

**Healthy** — both expected constants were independently re-derived by this audit against the
current production shader and match to within rounding; the doubling factor and multiply order
are verified correct against FNA's `PSDualTexture`.

## Checklist Results

### API / XNA / FNA parity
`setTextureProperty`/`setTexture2Property`/`setDiffuseColorProperty` (lines 89-91) map directly to
FNA's `DualTextureEffect.Texture`/`Texture2`/`DiffuseColor`. `DualTextureEffect::FillGpuDrawParams()`
(`DualTextureEffect.cpp:260-263`) premultiplies `diffuseColor_` by `alpha_` exactly as FNA's own
`DualTextureEffect.cs` setter pattern does; `alpha_` defaults to `1.0f`
(`DualTextureEffect.hpp:270`) so this is a no-op for this test.

### Behavioral correctness
Re-derived both checks by hand against the *current* shader
(`fs_dual_texture3d.sc:11-15`: `base=texture0; base.rgb*=2.0; color=base*texture1*diffuse`):
- Check (a): tex0=gray(100/255=0.3922), tex1=white(1), diffuse=white(1). `base.rgb = 0.3922*2 =
  0.7843`; `color = 0.7843*1*1 = 0.7843 → 255*0.7843 ≈ 200.0`. Matches
  `kExpectedGray200 = Color(200,200,200,255)` asserted at line 135-136 exactly.
- Check (b): tex0=white(1), tex1=white(1), diffuse=red(1,0,0). `base.rgb = 1*2 = 2` (unclamped
  intermediate value — GPUs commonly carry fp32 through the fragment shader and clamp only at the
  final 8-bit write); `color = 2*1*(1,0,0) = (2,0,0)`, clamped on write to `(255,0,0)`. Matches
  `Color(255,0,0,255)` asserted at line 140-141 exactly.
Both values check out against the real current shader, not a stale or superseded formula.

### Logic
`drawAndRead()` (lines 85-108) retries the draw/readback loop up to 20 times, breaking on the
first non-black pixel — a defensive pattern against a first-frame-blank artifact documented
elsewhere in this shard (`GetBackBufferData` only reliably reflecting the first read per rendered
frame, per the fog test's own comment); reasonable given the alternative (asserting on a
transient black frame) would be a flaky false failure, not a false pass.

### C++ correctness
`colourMatch(..., tol=20)` (lines 47-52) only compares R/G/B, not alpha — acceptable here since
both expected outputs use full alpha (255) and `BlendState::Opaque` is set every iteration
(line 98), so alpha divergence would not silently mask an RGB-channel defect.

### Robustness
The `RasterizerState::CullNone` workaround (line 101, referencing Task 364/884) is independently
justified: the quad's vertex order (`TL→BL→BR`, `TL→BR→TR`, `Vector3(-1,1)…Vector3(1,1)`) has a
positive-signed-area (CCW, by the shoelace formula) winding in this Y-up NDC scheme, and the
production `RasterizerState` default is confirmed to be `CullCounterClockwiseFace`
(`RasterizerState.cpp:11`), which culls CCW-wound faces — so without the explicit `CullNone` this
quad genuinely would not render on a backend whose default cull state matches FNA's real default
(Bgfx, per this shard's own cross-file note), as opposed to EasyGL/Vulkan, which are separately
flagged (Task 884, out of this batch's scope) as not yet matching that FNA default.

### Testing
Both checks are precise, evidence-backed pixel assertions rather than "compiles and doesn't
crash" checks; each isolates a distinct part of the formula (doubling factor in isolation vs. the
full pipeline with a non-white diffuse tint).

## Detailed Findings

None. Both expected constants were independently re-derived from the current production shader
source and match to sub-1-unit rounding; the test genuinely discriminates the bug it was written
to catch (a missing `*2` would make check (a) read ≈100 instead of ≈200, clearly outside the
default `tol=20`).

## Cross-File Observations

- This file, `bgfx_dualtextureeffect_fog_test.cpp`, `bgfx_dualtextureeffect_null_texture0_test.cpp`,
  and `bgfx_dualtextureeffect_null_texture2_test.cpp` all exercise the same
  `dualTexture3DProgram_`/`fs_dual_texture3d.sc` pair; this file is the one that specifically pins
  down the doubling factor that the other three take as a given (their own expected constants,
  e.g. `(160,80,240)` for a white-texture fallback, already bake the `*2` in).
- Git history (`1ed85909 fix(Task 383): DualTextureEffect missing FNA's color.rgb*=2 doubling
  factor (all 3 backends)`) confirms the header comment's account of the fix that this test locks
  in.

## Missing or Weak Tests

None specific to doubling — `Alpha` premultiplication and fog are covered by sibling files in this
shard (`Task 385`/`Task 888` per git log), so this file's narrow scope is appropriately
complemented rather than duplicated.

## Positive Findings

- The test's own header comment is unusually precise about *why* this bug was invisible to every
  prior `DualTextureEffect` test (Tasks 133/135/191/293/294/296/297 all used saturated 0/1 texture
  values where a missing `*2` clamps back to the same result) — this is corroborated by this
  audit's own manual check that check (b) alone (both textures white) cannot discriminate the bug,
  which is exactly why check (a)'s non-saturated gray(100) input is the one that matters.
- Reuses the exact same `RasterizerState::CullNone` workaround and retry-loop pattern consistently
  with the rest of this shard's Bgfx pixel tests, reducing the chance of a one-off authoring
  mistake in this particular file.

## Final Assessment

A precise, well-targeted regression test for a specific, previously-invisible RGB-doubling bug;
both expected constants were independently re-verified against the current shader and FNA
reference and are exactly correct.
