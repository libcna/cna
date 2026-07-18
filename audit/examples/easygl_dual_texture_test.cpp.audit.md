# Audit: examples/easygl_dual_texture_test.cpp

## Metadata

- Source file: `examples/easygl_dual_texture_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration/pixel-readback test
- File type: C++ executable test (`Game` subclass, no gtest)
- Lines: 125
- XNA/FNA relevance: exercises `Microsoft::Xna::Framework::Graphics::DualTextureEffect`, a real XNA 4.0 stock
  effect
- FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/DualTextureEffect.cs`
  and its vendored HLSL, `src/CNA/Internal/Backends/D3D9/shaders/xna/DualTextureEffect.fx` (EXEMPT
  `vendored-verbatim-stock-effect` per `AUDIT_DECISIONS.md` D-5, consulted here as the authoritative behavior
  reference, not itself audited)
- Production code under test: `src/Microsoft/Xna/Framework/Graphics/DualTextureEffect.cpp`,
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (`EnsureDualTextured3DProgram`, lines 3009-3070)

## Purpose

Task 133 integration test: renders a full-screen quad textured with two solid 1×1 textures (magenta on unit 0,
yellow on unit 1) through `DualTextureEffect`, and reads back the centre pixel expecting red — the test's own
header comment states the expected math as "magenta × yellow × white = (1, 0, 0, 1) = red," i.e. a plain
three-way component-wise multiply.

## Executive Verdict

**Needs attention — the test's own documented model of the blend math is incomplete, and its chosen colors
happen to mask the gap rather than expose it.** The production EasyGL shader correctly replicates FNA's real
`DualTextureEffect.fx` formula, which is not a plain multiply but "modulate 2×" (`color.rgb *= 2` before the
second multiply) — confirmed against the actual vendored HLSL. The test's magenta/yellow color choice cannot
distinguish "the ×2 step ran" from "the ×2 step was silently removed," because both give an identical clamped
result for these specific inputs. See F1.

## Checklist Results

### API / XNA / FNA parity
`DualTextureEffect` construction and property usage (`setTextureProperty`, `setTexture2Property`,
`setWorldProperty`/`setViewProperty`/`setProjectionProperty`, `Apply()`) match
`include/Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp`'s public surface, which in turn matches FNA's
`DualTextureEffect.cs` property set (`Texture`, `Texture2`, `World`/`View`/`Projection` via `IEffectMatrices`).
`VertexPositionTexture` (stride 20: 3 floats position + 2 floats UV) is the correct FNA vertex type for this
effect's un-lit, non-vertex-colored path (`VSInputTx2`/`VSInputTx` family in the vendored `.fx`).

### Behavioral correctness
Traced `DualTextureEffect::FillGpuDrawParams` (`DualTextureEffect.cpp:248-275`): sets `p.dualTexture = true`,
`p.texture0 = &texture_->GetBackend()`, `p.texture1 = &texture2_->GetBackend()`, `p.diffuseColor =
diffuseColor_*alpha_` (defaults `{1,1,1}`/`1.0f` per `DualTextureEffect.hpp:269-270`, matching the test's "white"
assumption since neither is set explicitly by the test). Traced `EnsureDualTextured3DProgram`'s fragment shader
(`EasyGLGraphicsBackend.cpp:3038-3056`):
```
vec4 base=texture(uTexture,vUV);
base.rgb*=2.0;
FragColor=base*texture(uTexture2,vUV)*uDiffuseColor;
```
Cross-checked against the actual vendored FNA HLSL (`DualTextureEffect.fx:95-106`, `PSDualTexture`):
```
color.rgb *= 2;
color *= overlay * pin.Diffuse;
```
**Confirmed identical formula** — CNA's EasyGL implementation is a faithful, correct port of FNA's real
"modulate 2×" blend, not a plain multiply. This is the correct production behavior; the issue (F1) is entirely in
what this test's chosen inputs can prove.
Computed both formulas for this test's actual inputs — magenta `(1,0,1)` on unit 0, yellow `(1,1,0)` on unit 1,
diffuse `(1,1,1)`:
- **With** the real ×2 step: `(1×2, 0×2, 1×2) × (1,1,0) × (1,1,1) = (2,0,0)`, clamped on write to an 8-bit UNORM
  backbuffer → `(1,0,0)` = red.
- **Without** the ×2 step (a plain multiply, i.e. a regressed/incorrect implementation): `(1,0,1) × (1,1,0) ×
  (1,1,1) = (1,0,0)` = red — **identical after clamping.**
Because the G and B channels are already zero from the yellow texture's own components regardless of the ×2
scale, and the R channel saturates to 1.0 either way (2.0 or 1.0, both clamp to the same 8-bit value), this
test's specific color choice cannot discriminate correct-vs-regressed blend math. A hypothetical regression that
deleted `base.rgb*=2.0;` entirely would still make this exact test print `[PASS]`.

### Logic
The pass/fail check (`getRProperty() >= 200 && getGProperty() <= 50 && getBProperty() <= 50`, lines 97-99) is the
same tolerant-threshold pattern used across this shard's other readback tests — reasonable in isolation, but here
it's checking a result that both the correct and an incorrect implementation would produce identically (see
above).

### Memory/resource lifetime
`tex0_`/`tex1_` are `Texture2D` member fields (not locals), created once in `Initialize()` via
`Texture2D::CreateFromPixels` and used in the single `Draw()` call — no dangling-pointer concern (contrast with
the function-local-`BasicEffect` pattern seen in the other files in this batch; this file avoids that entirely
since `DualTextureEffect fx(device)` is itself local to `Draw()` but `Draw()` runs to completion, including
`Exit()`, before returning, so no cross-call dangling window exists here).

### Robustness
Same `RasterizerState::CullNone` workaround (line 88), with a comment explicitly cross-referencing "the Bgfx
sibling's Task 364/884 fix" — independently re-verified the winding for this file's own quad-corner order
(`(-1,1)→(-1,-1)→(1,-1)` for the first triangle) and confirmed the same CCW-under-default-`CullCounterClockwiseFace`
conclusion as every other file in this batch; correctly necessary and correctly attributed.

### Testing
This is the sole test in the manifest's stated scope exercising `DualTextureEffect`'s two-texture blend formula.
Given the confirmed formula match (real ×2 in both FNA and CNA) but the color-choice weakness (F1), this test
currently proves "both textures are sampled and their result reaches the screen roughly right," not "the specific
modulate-2× blend equation is correct" — a materially weaker claim than the test's own header comment implies.

## Detailed Findings

### F1 — Test's color choice (magenta × yellow) cannot distinguish the real "modulate 2×" blend from a plain single multiply

- Severity: MEDIUM
- Confidence: HIGH (both formulas computed explicitly against this test's actual RGB inputs, and the real FNA
  formula independently confirmed byte-for-byte in the vendored `.fx` source)
- Category: test-coverage
- Location/symbol: `DualTextureTest::Draw` (lines 49-52, magenta/yellow texture data); production formula at
  `EasyGLGraphicsBackend.cpp:3051-3052` (`base.rgb*=2.0; FragColor=base*texture(uTexture2,vUV)*uDiffuseColor;`)
- Evidence: see Behavioral correctness above — both the "correct" and "×2 silently removed" formulas yield an
  identical clamped `(1,0,0)` for this test's specific `(1,0,1)`×`(1,1,0)` inputs.
- Why it matters: this test's own header comment presents "magenta × yellow × white = red" as *the* expected
  math, without mentioning the ×2 factor at all — suggesting the test's own author's model of the formula was
  incomplete when the color choice was picked, not a deliberate "this input happens not to distinguish the cases"
  decision. A future regression that dropped or altered the ×2 scale (e.g. during a shader refactor) would pass
  this test silently, with no other test in this batch or (as far as this audit's scope covers) exercising the
  same code path with different color values.
- FNA/XNA comparison: FNA's real formula (confirmed above) is "modulate 2×," a specific, named XNA blend
  behavior (the classic `D3DTOP_MODULATE2X`-style effect XNA's `DualTextureEffect` is built for, e.g. lightmap
  compositing) — distinct from a plain multiply, and worth testing as its own named behavior rather than folding
  it into a test whose inputs can't observe it.
- Related files: none of this batch's other 7 files touch `DualTextureEffect`; this is an isolated gap specific
  to this one test.
- Suggested action (not implemented by this audit): use a pair of mid-tone gray textures (e.g. `(128,128,128)` on
  both units) where the ×2 scale is the *only* thing standing between "washed-out gray" (correct, since
  0.5×2×0.5×1=0.5 mid-gray survives) and "quarter-brightness dark gray" (regressed, 0.5×0.5×1=0.25) — a choice
  that would actually discriminate the two cases instead of coincidentally aliasing them.

## Cross-File Observations

- This is the only file in this 8-file batch whose production formula was confirmed correct but whose *test
  input choice* independently reduces its discriminating power — a different failure mode from
  `easygl_draw_user_primitives_custom_test.cpp`'s F1 (where the production code itself has a confirmed gap). Both
  are genuine "the test doesn't validate what it claims to" findings, arrived at only by reading the actual
  shader/production math rather than trusting the test's own comment.

## Missing or Weak Tests

- No test in this repository (as far as this batch's scope reveals) uses inputs capable of isolating
  `DualTextureEffect`'s "modulate 2×" scale factor from a plain single-multiply implementation — see F1's
  suggested fix.
- No test exercises `DualTextureEffect`'s vertex-color-enabled path (`VertexColorEnabled` / `EnsureDualTexturedColored3DProgram`,
  `EasyGLGraphicsBackend.cpp:3072-3142`), fog, or alpha-test behavior — all real, documented features of this
  effect with dedicated shader code paths in the EasyGL backend that this single test never reaches.

## Positive Findings

- The production EasyGL implementation was independently verified, byte-for-byte, against the real vendored FNA
  HLSL shader — a genuinely faithful, correct port, not merely "close enough."
- Correctly and specifically justifies its `CullNone` requirement with a cross-reference to a related backend's
  prior fix, and that justification was independently re-derived and confirmed here, not just trusted.
- Uses `Texture2D::CreateFromPixels` and a real `GetBackBufferData` readback rather than any kind of mock — a
  genuine end-to-end rendering test in mechanism, even though its specific color choice under-tests the effect's
  actual math.

## Final Assessment

The production `DualTextureEffect`/EasyGL "modulate 2×" implementation is correct and verified against FNA's real
shader. The test itself, however, is weaker than its own header comment implies: its color choice cannot detect
the removal of the very blend-scale factor that distinguishes this effect from a plain texture multiply, so a
real regression there would go unnoticed. This is exactly the class of "test exercises the code but doesn't prove
the claimed semantics" gap the audit brief asks to surface.
