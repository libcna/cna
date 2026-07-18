# Audit: examples/easygl_texture_filter_linear_golden_test.cpp

## Metadata

- Source file: `examples/easygl_texture_filter_linear_golden_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 466, golden-image consumer for Task 297's
  magnification/`Linear` scene
- File type: `PixelTestGame`-derived executable (`common/PixelTestGame.hpp`), CTest-registered as
  `cna_test_easygl_texture_filter_linear_golden` (`cmake/Tests/EasyGLTests.cmake:85-86`).
- XNA/FNA relevance: `TextureFilter::Linear`, `DualTextureEffect`, `SamplerState` — real XNA 4.0 API.
- Golden fixture: `examples/golden/easygl_texture_filter_linear_golden_test.png` — confirmed present
  on disk (98 bytes).
- Related production code: `DualTextureEffect.cpp` (`DiffuseColor` parameter);
  `EasyGLGraphicsBackend.cpp`'s dual-textured fragment shader (`base.rgb*=2.0`, line 3051).

## Purpose

Recreates exactly one column (magnification, `Linear`) from the pre-existing hand-rolled
`easygl_texture_filter_point_vs_linear_test.cpp` (Task 297) scene, but validates it two independent
ways: an exact hand-derived pixel value (`ExpectPixel`) and a checked-in reference-PNG region
comparison (`CompareGoldenImage`, Task 463's infrastructure) — deliberately exercising the golden-
image mechanism against an already-trusted scene rather than adding new `TextureFilter` coverage.

## Executive Verdict

**Healthy.** The independently re-derived expected blend value is arithmetically correct, matches
the actual `DualTextureEffect` doubling-compensation shader behavior verified directly in
`EasyGLGraphicsBackend.cpp`, and the golden-image comparison region is robust to backbuffer-size
differences between capture and comparison runs.

## Checklist Results

### Behavioral correctness
At UV=0.5 for a 2-texel (Red|Green) texture, linear filtering blends texel 0 and texel 1 at their
exact 50/50 midpoint: `(255,0,0)` and `(0,255,0)` average to `(127.5,127.5,0)` before rounding. The
comment's claimed match to a live-observed `(127,128,0)` (Task 297's own value) is internally
consistent — this audit independently re-derived the same `~127/128` midpoint from first principles,
not merely repeated the comment.

The `DiffuseColor=(0.5,0.5,0.5)` compensation (line 69) is verified correct against the actual
EasyGL fragment shader for the dual-textured 3D program
(`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:3051-3052`):
`base.rgb*=2.0; FragColor=base*texture(uTexture2,vUV)*uDiffuseColor;` — with a white (identity,
`(1,1,1,1)`) overlay texture (`whiteTex`, line 57), the doubling is exactly canceled by
`uDiffuseColor=(0.5,0.5,0.5)`, leaving the raw texel blend unscaled. This mirrors FNA's own
`DualTextureEffect.fx` pixel shader (`color.rgb *= 2; color *= overlay * pin.Diffuse;`,
`HLSL/DualTextureEffect.fx` lines 100-101) verbatim — the CNA GLSL is a faithful port of FNA's
documented `*2` doubling quirk, and this test correctly compensates for it rather than silently
absorbing an error into a wider tolerance.

### Logic
The screen-space geometry math (lines 78-88) computes NDC coordinates for a fixed pixel-space column
`[256,512)` from the *actual runtime* backbuffer width `W`, not a hardcoded resolution — so the drawn
quad always occupies the same absolute pixel range regardless of what `GraphicsDeviceManager`'s
actual default resolution is on a given platform/backend. `samplePx=384` (the column's own midpoint)
and the golden-comparison region (`samplePx-4 .. +4`, i.e. `[380,388)`) both stay inside the drawn
quad for any `W` large enough to fit the column at all, so no off-by-one or partial-quad sampling
risk was found.

### Robustness
`ExpectPixel` and `CompareGoldenImage` are called with the *same* independently-derived value
(`Color(127,128,0,255)`), i.e. the golden-PNG comparison is corroborated by a hand-computed
prediction rather than trusted blindly — if the checked-in PNG were itself generated from a buggy
render, `ExpectPixel` alone would still catch the divergence. This is the same "dual verification"
discipline seen in sibling golden-image tests (see `easygl_alphatesteffect_golden_test.cpp`'s audit
report) and correctly documented as such in the file's own header comment.

`tolerance=10` on both checks is tighter than the `±20`-class tolerances used by most golden-image
tests in this shard; the header comment (lines 17-19) justifies this explicitly by contrasting it
against the ~128-unit gap to a broken Point-filtered result — a deliberate, reasoned choice, not an
unexplained magic number.

### Testing
Narrow, correctly-scoped reuse of an already-verified scene to validate golden-image infrastructure,
consistent with this project's established Task 463-468 pattern (cross-referenced against the
sibling `easygl_alphatesteffect_golden_test.cpp` audit).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Depends on `RasterizerState::CullNone` workaround being applied consistently

- Severity: INFO
- Confidence: HIGH
- Category: cross-file consistency
- Location/symbol: line 94, `device.setRasterizerStateProperty(RasterizerState::CullNone)`
- Evidence: comment attributes this to "Task 896's finding" (same as the source scene, Task 297) —
  the quad's own vertex winding is back-facing under CNA's real default rasterizer state. Correctly
  carried over from the original scene rather than silently omitted, which would have made the golden
  comparison compare against an unlit/culled (background-color) region instead.

## Cross-File Observations

- This file and `easygl_texture_filter_point_vs_linear_test.cpp` (audited separately in this batch)
  intentionally draw the *same* geometry/scene fragment (magnification, `Linear`, columns 2) — a
  legitimate, explicitly-documented duplication for golden-image infrastructure validation rather than
  a copy-paste maintenance smell.

## Missing or Weak Tests

None found — the file's narrow scope (validate the golden-image mechanism against an already-trusted
`Linear`-blend case) is appropriately minimal.

## Positive Findings

- Independent arithmetic re-derivation of the expected blend value, cross-checked against the actual
  GLSL shader source rather than merely asserted in a comment.
- Correctly reuses an already-established, already-audited scene instead of introducing new render
  logic solely to exercise the golden-image path.

## Final Assessment

A small, well-reasoned golden-image consumer test whose one nontrivial claim (the `DiffuseColor=0.5`
doubling compensation) was independently verified against the real EasyGL fragment shader and found
correct.
