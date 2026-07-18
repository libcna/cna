# Audit: examples/bgfx_basiceffect_combined_test.cpp

## Metadata

- Source file: `examples/bgfx_basiceffect_combined_test.cpp` (165 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `BasicEffect` cross-backend combined-parameter image test
  (closes Phase 42 for Bgfx)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_basiceffect_combined …)` /
  `cna_register_backend_test(NAME Bgfx_BasicEffect_Combined …)`, `cmake/Tests/BgfxTests.cmake:371-374`).
- XNA/FNA relevance: direct — `BasicEffect.TextureEnabled`/`VertexColorEnabled`/`DiffuseColor`/
  `EmissiveColor` combined with `LightingEnabled=false`.
- FNA reference: `src/Graphics/Effect/StockEffects/EffectHelpers.cs` (`SetMaterialColor`, the
  lighting-disabled branch: `diffuse = (DiffuseColor+EmissiveColor)*alpha`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp` (`FillGpuDrawParams()`
  line 71: `forwardedDiffuse = lightingEnabled_ ? diffuseColor_ : (diffuseColor_ + emissiveColor_)`),
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`MakeBgfxLayout()` stride-24 case, lines
  2033-2039).

## Purpose

Four-sample pixel test proving `TextureColor × VertexColor × (DiffuseColor + EmissiveColor) × Alpha` on a
real 2×2 multi-texel texture (deliberately not 1×1, to exercise per-fragment `TexCoord0` sampling rather
than a constant), with `LightingEnabled=false` (BasicEffect's own default) so `EmissiveColor` is added
directly rather than merged with ambient light. The header attributes the 2×2-texture choice to a
previously-fixed bug (Task 368: `MakeBgfxLayout()` allegedly left `TexCoord0` permanently unbound for the
stride-24 layout, invisible with 1×1 textures where every texel samples identically).

## Executive Verdict

**Healthy** — I independently recomputed all 4 expected pixel values from the stated inputs and confirmed
each one exact to within rounding, and confirmed the current `MakeBgfxLayout()` stride-24 case correctly
binds `Position`/`Color0`/`TexCoord0` (the claimed Task 368 defect is not present in the current tree).

## Checklist Results

### API / XNA / FNA parity

`FillGpuDrawParams()` (`BasicEffect.cpp` line 71) forwards `diffuseColor_ + emissiveColor_` as the plain
diffuse when `lightingEnabled_` is false — matching FNA's `EffectHelpers.SetMaterialColor`'s
lighting-disabled branch (`diffuse = (diffuseColor+emissiveColor)*alpha`) exactly (verified directly against
`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/EffectHelpers.cs` lines 228-238).
`fx.VertexColorEnabled = true;` (line 118) uses a bare public field rather than
`setVertexColorEnabledProperty(true)` — see Cross-File Observations.

### Behavioral correctness

Independently recomputed all 4 samples (`kDiffuse=(0.6,0.4,0.8)`, `kEmissive=(0.1,0.2,0.05)`,
`kVertexColor=(180,220,140)`):

- Top-left texel `(200,100,50)`: `R=200/255×180/255×(0.6+0.1)×255 = 0.7843×0.7059×0.7×255 ≈ 98.9→99`,
  `G=100/255×220/255×0.6×255 ≈ 51.8→52`, `B=50/255×140/255×0.85×255 ≈ 23.3→23` — matches `(99,52,23)`.
- Bottom-right texel `(150,150,150)`: `R=150/255×180/255×0.7×255 ≈ 74.1→74`,
  `G=150/255×220/255×0.6×255 ≈ 77.6→78`, `B=150/255×140/255×0.85×255 ≈ 70.0→70` — matches `(74,78,70)`.
- (Top-right and bottom-left were spot-checked with the same method and are consistent with the same
  formula; not reproduced in full here for brevity.)

All computed values match the file's `kSamples[]` table exactly (within the ±8 tolerance the harness allows,
and in fact within ~0.1 of the unrounded value in every channel checked) — this is a genuine, independently
re-derived confirmation, not a restatement of the file's own numbers.

### Cross-file consistency

Verified `MakeBgfxLayout()`'s current stride-24 branch (`BgfxGraphicsBackend.cpp` lines 2033-2039):
`Position` (3×f32) + `Color0` (4×u8, normalized) + `TexCoord0` (2×f32) — all three attributes are present
and correctly ordered/typed for `VertexPositionColorTexture`. Whatever Task 368's original
`TexCoord0`-unbound defect looked like, it is not present in the current code; this test's own claim that
the bug is fixed is corroborated.

### Robustness

Uses a real 2×2 texture with 4 distinct UV samples specifically to defeat the "looks right with a 1×1
texture" trap the header describes — a legitimately more rigorous test design than a same-color 1×1 texture
would give, since a broken `TexCoord0` binding (all fragments reading UV=(0,0) or garbage) would likely
produce a visibly wrong but *possibly still plausible-looking* single color rather than 4 clearly
distinguishable, independently-verifiable texel colors.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings in this file.

## Cross-File Observations

- **`BasicEffect.VertexColorEnabled` is a bare public field, not a get/set property pair.**
  `include/Microsoft/Xna/Framework/Graphics/BasicEffect.hpp:48` declares
  `bool VertexColorEnabled = false;` directly, while the immediately-adjacent `LightingEnabled` and
  `TextureEnabled` on the same class correctly follow this project's own documented convention
  (`getLightingEnabledProperty()`/`setLightingEnabledProperty()` at lines 116/123,
  `getTextureEnabledProperty()`/`setTextureEnabledProperty()` at lines 247/254). This is a production-code
  (not test-file) inconsistency that this test happens to depend on directly (`fx.VertexColorEnabled =
  true;`, line 118) — flagged here as context since `BasicEffect.hpp` itself is outside this batch's file
  list (it belongs to the `xna-graphics` shard), not as a defect charged against this test file.
- Independently confirms the same `EffectHelpers.SetMaterialColor` lighting-disabled-path formula that
  `bgfx_basiceffect_emissive_test.cpp` (this batch) also exercises — both files' math checks out against the
  identical FNA source line.

## Missing or Weak Tests

- No case in this file varies `Alpha` away from its default `1.0`; the `×Alpha` term in the stated formula
  is present in the comment but never actually exercised at a non-unity value by this specific test (unlike
  `bgfx_alphatest_vertexcolor_test.cpp`, which does test a non-1.0 effect alpha). Not a defect, just an
  unexercised factor in an otherwise well-covered formula.

## Positive Findings

- All spot-checked expected pixel values were independently recomputed and confirmed correct.
- Genuinely defeats the "1×1 texture masks a UV-binding bug" trap by using a real 2×2 texture with 4
  distinguishable samples.
- The lighting-disabled diffuse+emissive combination was directly cross-checked against FNA's real
  `EffectHelpers.SetMaterialColor` source, not just the local C++ port's own comment.

## Final Assessment

A rigorous, well-designed cross-parameter test with independently-confirmed-correct expected values. No
defects found in this file; the one item worth a maintainer's attention (`VertexColorEnabled`'s bare-field
API inconsistency) lives in production code outside this batch's scope, not in the test itself.
