# DualTextureEffect Exactness Support Matrix

Phase 44 (graphics Tasks 381–390) audited and pixel-verified `DualTextureEffect`
conformance against FNA. This document summarizes the findings and closes the phase.

---

## 1. Property/default audit (Tasks 381–382)

Task 381 audited all 9 `DualTextureEffect` properties and `OnApply()`'s material-color/shader-index
logic line-by-line against FNA's `DualTextureEffect.cs` — **verify-only, zero bugs found**,
mirroring Task 371's `AlphaTestEffect` opener. Every default matched exactly (`FogEnabled`/
`VertexColorEnabled=false`, `World`/`View`/`Projection=Identity`, `DiffuseColor=Vector3.One`,
`Alpha=1`, `FogStart=0`, `FogEnd=1`, `FogColor=Zero`, `Texture`/`Texture2=null`), the
`EffectDirtyFlags` usage, property-setter dirty-flag side effects, `OnApply()`'s
`SetWorldViewProjAndFog`/`SetFogVector` logic, and `Clone()`'s field-copy list all matched FNA
literal-for-literal. The audit found `FillGpuDrawParams()` never forwards any fog fields at all —
the identical bug shape Task 378 found and fixed for `AlphaTestEffect` — deliberately deferred to
Task 388.

Task 381 found **zero existing default-value unit test coverage**. Task 382 wrote
`tests/Microsoft/Xna/Framework/Graphics/DualTextureEffectTests.cpp` from scratch (mirroring Task
372's `AlphaTestEffectTests.cpp` precedent): 26 tests covering all 9 property defaults, setter
round-trips (including `Texture`/`Texture2`), `Clone()`, and `GetTypeName()`. No bugs found — pure
test-authoring.

## 2. Basic two-texture blend formula (Tasks 383–384)

**Task 383 found and fixed a real bug.** FNA's `DualTextureEffect.fx`
`PSDualTexture` computes `color = SAMPLE(Texture); color.rgb *= 2; color *= overlay *
pin.Diffuse;` — a `*2` doubling factor on the first texture's RGB channels (alpha untouched).
CNA's dual-texture fragment shaders all computed
`texture0 * texture1 * diffuseColor` directly, silently missing this factor — invisible to every
prior test (Tasks 133/135/191/293/294/296/297) since they all used pure 0/1-saturated texture
values, where a missing `*2` clamps right back to the same result. **Fixed** by adding
`base.rgb *= 2.0` to the dual-texture fragment shaders. Fixing it also required
updating a pre-existing test (`TextureFilter::Point vs Linear`, Task 297) whose blend-detection thresholds only worked *because of* the missing factor —
compensated with `DiffuseColor=0.5`.

## 3. Alpha premultiplication (Task 385)

**Verify-only, zero bugs** — Task 381's audit had already confirmed
`FillGpuDrawParams()`'s `Vector4(diffuseColor*alpha, alpha)` formula matches FNA exactly. Task 385
added the missing verification layers: 3 new GPU-independent unit tests directly checking
`FillGpuDrawParams()`'s output, plus real `BlendState.AlphaBlend` pixel tests on EasyGL
proving the premultiplied RGB blends correctly against a real background.

## 4. Texture null-fallback behavior (Tasks 386–387)

`DualTextureEffect` has no `TextureEnabled` flag (like `AlphaTestEffect`). Task 379 originally
invented an opaque-white fallback without measuring XNA. SOFTWARE-302 later measured Microsoft
XNA 4.0 and corrected the Software/EasyGL contract: either null slot samples opaque black.

Task 386's original first-slot white assertion was therefore not a conformance proof. The corrected
Software/EasyGL test now draws a real texture first and requires exact opaque black after nulling
`Texture`, including alpha.

SOFTWARE-302 closes both slots for the Software/EasyGL parity target; the other renderer
families require their own Microsoft-XNA reconciliation.

## 5. Fog behavior (Task 388)

**Real bug found and fixed on EasyGL.** Task 381's audit had already found
`FillGpuDrawParams()` never forwards any fog fields — the same bug shape Task 378 fixed for
`AlphaTestEffect`. **Larger than Task 378's fix**: unlike `AlphaTestEffect` (which reuses a shared
per-stride shader that already had fog infra), `DualTextureEffect` uses its own dedicated
`EnsureDualTextured3DProgram()` shader on EasyGL, which had **no fog uniforms at all**. Fixed on
both sides: added `uFogEnabled`/`uFogColor`/`uFogStart`/`uFogEnd` uniforms plus the standard
`vFogFactor`/`mix()` blend to the shader (mirroring `EnsureTextured3DProgram()`'s pattern exactly),
then forwarded the 4 fog fields in `FillGpuDrawParams()` (mirroring `AlphaTestEffect`'s Task 378
C++ pattern).

## 6. Integration capstone (Task 389)

Closed the pixel-verification work with a capstone test combining everything Tasks 383–388
verified individually — the doubling factor, two-texture multiply, and `DiffuseColor` — to prove
the fixes compose correctly together, not just in isolation. Used, for the first time in any
`DualTextureEffect` pixel test, a **real 2×2 multi-texel texture** (every prior task used a 1×1
solid color) sampled at all 4 texel centers via 4 separate draws.

**Result: the pixel output matched the FNA-derived expected formula (`Texture × 2 × Texture2 × DiffuseColor`) at all 4 sample points. No new bugs found in the
combined assertions — but writing this test surfaced a real, previously-unaudited gap: **found and
opened Task 889** (`VertexColorEnabled` is a total no-op — the dedicated dual-texture
shader/pipeline declares only position+texcoord inputs, no color attribute at all). Phase 44 never had a dedicated audit task for this property, unlike `AlphaTestEffect`'s
Task 377.

## Support matrix

| Feature | EasyGL |
|---|---|
| Property defaults (9/9) | ✅ Task 381/382 |
| `color.rgb *= 2` doubling factor | ✅ fixed Task 383 |
| Basic two-texture multiply (magenta×yellow) | ✅ Task 133/384 |
| `Alpha` premultiplication | ✅ Task 385 |
| First texture (`Texture`) null-fallback | ✅ Task 386 |
| Second texture (`Texture2`) null-fallback | ✅ Task 387 |
| Fog (`FogEnabled`/`FogColor`/`FogStart`/`FogEnd`) | ✅ fixed Task 388 |
| Independent `TextureCoordinate0` / `TextureCoordinate1` | ✅ fixed SAMPLE-073 |
| `VertexColorEnabled` | ❌ Task 889 |
| Combined integration capstone | ✅ Task 389 |

Legend: ✅ verified working · ❌ confirmed not implemented.

## Open, tracked follow-up work

- **SAMPLE-073 follow-up (2026-08-31)** — the earlier matrix never exercised FNA's distinct
  `TEXCOORD0`/`TEXCOORD1` inputs. EasyGL incorrectly forwarded one UV to both samplers. Its stock
  declaration mapper and shaders now consume usage indices 0 and 1 independently; the real-GL
  `EasyGL_DualTextureEffect_IndependentUV` regression uses the original SoccerPitch 40-byte
  position/normal/two-UV declaration and fails black if the second sampler reuses the first UV.
- **Task 889** (opened by Task 389) — `DualTextureEffect.VertexColorEnabled` is a total no-op on
  EasyGL. Needs a new vertex-color-carrying stride variant and `vin.Color` multiplication
  into the forwarded diffuse on the dual-texture shader (mirroring FNA's
  `VSDualTextureVc`/`VSDualTextureVcNoFog` shader variants) — a genuinely new multi-shader-file
  feature addition, not fixed here.

This closes Phase 44 (graphics Tasks 381–390) in full.
