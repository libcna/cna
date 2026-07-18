# Audit: examples/bgfx_alphatest_vertexcolor_test.cpp

## Metadata

- Source file: `examples/bgfx_alphatest_vertexcolor_test.cpp` (162 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `AlphaTestEffect.VertexColorEnabled` fix verification
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_alphatest_vertexcolor …)` /
  `cna_register_backend_test(NAME Bgfx_AlphaTest_VertexColor …)`, `cmake/Tests/BgfxTests.cmake:329-332`).
- XNA/FNA relevance: direct — `AlphaTestEffect.VertexColorEnabled`, and the FNA-specific rule that the
  alpha test gates on the *combined* pixel alpha (texture × vertex-color × material), not the material alpha
  alone.
- FNA reference: `src/CNA/Internal/Backends/D3D9/shaders/xna/AlphaTestEffect.fx`
  (`VSAlphaTestVc`: `vout.Diffuse *= vin.Color;`, then `PSAlphaTestLtGt`/`PSAlphaTestEqNe` gate on
  `color.a` after `SAMPLE_TEXTURE(...) * pin.Diffuse`).
- Related production code:
  `src/CNA/Internal/Backends/Bgfx/shaders/vs_alpha_test_colored3d.sc` (the stride-24
  `VertexPositionColorTexture` variant added for this fix), `fs_alpha_test3d.sc` (shared fragment shader).

## Purpose

Regression test for `Task 887`: Bgfx's (and Vulkan's) alpha-test pipeline previously declared only
position+texcoord vertex inputs, never a color attribute, so `AlphaTestEffect.VertexColorEnabled` had zero
effect on those two backends. Fixed by adding a dedicated stride-24 vertex shader variant
(`vs_alpha_test_colored3d.sc`) that reads `a_color0` and gates its multiply by `VertexColorEnabled`,
dispatched whenever `VertexColorEnabled=true`.

## Executive Verdict

**Healthy** — I independently re-derived both the RGB combination formula and the combined-alpha gating
logic directly against FNA's real vendored `AlphaTestEffect.fx` (`VSAlphaTestVc`), not just this file's own
comment, and both check out exactly.

## Checklist Results

### API / XNA / FNA parity

Directly compared this test's premise against the real (vendored, byte-for-byte-from-FNA)
`AlphaTestEffect.fx`: `VSAlphaTestVc` computes `vout.Diffuse *= vin.Color;` (vertex color multiplies into the
diffuse *including its alpha channel*), and `PSAlphaTestLtGt`/`PSAlphaTestEqNe` both gate on
`SAMPLE_TEXTURE(Texture, pin.TexCoord) * pin.Diffuse`'s alpha — i.e. **texture alpha × vertex alpha ×
material alpha**, exactly as this test's own comment claims and exactly what
`vs_alpha_test_colored3d.sc` implements (`vec4 vc = (u_vertexColorEnabled3D.x>0.5) ? a_color0 :
vec4(1,1,1,1); v_color0 = vc * u_diffuseColor;`, then `fs_alpha_test3d.sc`'s
`texture2D(...) * v_color0`). This is a genuine FNA-behavior match, independently confirmed against the
vendored HLSL, not merely restated from the test's own header.

### Behavioral correctness

Re-derived both expected values by hand:
- RGB (`kExpectedRgb`): `TextureColor(1,1,1) × VertexColor(200,100,50)/255 × DiffuseColor(0.6,0.4,0.8) ×
  EffectAlpha(0.8)` → `R: (200/255)×0.6×0.8×255 = 96.0`, `G: (100/255)×0.4×0.8×255 = 32.0`,
  `B: (50/255)×0.8×0.8×255 = 32.0` → `(96, 32, 32)` — matches `kExpectedRgb` exactly.
- Combined alpha: `TextureAlpha(1) × VertexAlpha(200/255) × EffectAlpha(0.8) = 0.6275 ≈ 160/255`. At
  `ReferenceAlpha=100` (case A), `160 > 100` passes `CompareFunction::Greater` → drawn, confirming the RGB
  formula. At `ReferenceAlpha=180` (case B), `160 < 180` fails → discarded — and critically, the *diffuse
  alpha alone* (`EffectAlpha × 255 = 204`) would incorrectly pass `180` if vertex alpha were ignored, which
  is exactly the bug this test is designed to catch. Both branches independently verified arithmetically
  correct.

### Robustness

The two-case design (A: passes and reveals RGB; B: fails specifically because of the *vertex* alpha
contribution, not just any alpha) is a well-targeted pair — case B specifically distinguishes "combined
alpha gates the test" from "only material alpha gates the test," which is precisely the axis Task 887's bug
lived on (a shader that silently dropped the vertex-color attribute entirely).

### Testing

Uses the shared retry-loop readback pattern (`renderAndRead()` lines 96-110) consistent with the rest of
this batch except `bgfx_alphatest_comparefunction_sweep_test.cpp` (see that file's own F1).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

## Cross-File Observations

- Same `RasterizerState::CullNone`/Task 364/884 note as every other file in this batch, accurately
  described.
- Directly complements `bgfx_alphatest_null_texture_test.cpp` (this batch): together they cover the two
  vertex/pixel-input axes (`VertexColorEnabled`, `Texture=null`) that Bgfx's alpha-test pipeline previously
  mishandled independently (Tasks 887 and 379 respectively).

## Missing or Weak Tests

- No case exercises `VertexColorEnabled=true` together with `AlphaFunction::Equal`/`NotEqual` (the
  `isEqNe_`-gated shader-index branch) — only `Greater` is used here. Given `AlphaTestEffect`'s shader-index
  selection folds `vertexColorEnabled` and `isEqNe` into the same integer (`AlphaTestEffect.cpp` lines
  301-309), a defect specific to the `VertexColorEnabled + Equal/NotEqual` combination would not be caught by
  this file or by `bgfx_alphatest_comparefunction_sweep_test.cpp` (which never enables vertex color) or by
  this file (which never varies `AlphaFunction`).

## Positive Findings

- Both the RGB and combined-alpha formulas were independently re-derived and confirmed exactly correct
  against FNA's real vendored `AlphaTestEffect.fx` shader logic, not just this test's own narrative.
- Correctly isolates the specific defect class (vertex-alpha contribution to the alpha-test gate) with a
  two-case design that would fail clearly and specifically if the fix regressed.

## Final Assessment

A precise, FNA-verified regression test with no material issues. The formula and gating logic were
independently confirmed against the real vendored XNA shader source, not merely trusted from the file's own
comments.
