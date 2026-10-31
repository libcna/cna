# DualTextureEffect exactness support matrix

Phase 44 (graphics Tasks 381–390) audited and pixel-verified `DualTextureEffect`
conformance against FNA. This document records the resulting contract and the later fixes that
closed its follow-up work. Current-state reconciliation: 2026-10-31.

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
`modules/graphics/tests/Microsoft/Xna/Framework/Graphics/DualTextureEffectTests.cpp` (mirroring
Task 372's `AlphaTestEffectTests.cpp` precedent): the suite covers all 9 property defaults, setter
round-trips (including `Texture`/`Texture2`), ownership, `Clone()`, `GetTypeName()`, forwarded alpha
and the measured null-sampler contract.

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

SOFTWARE-302 first closed both slots for the Software/EasyGL parity target. The shared
`DualTextureEffectNullSamplerTest` now holds every active rasterizing renderer — EasyGL, Software,
WebGPU, SDLGPU, FNA3D and DirectX9 — to the same measured rule in both indexed and non-indexed
draws. Each test first installs non-black textures, then nulls one slot, so an invented white
fallback or a stale binding cannot pass accidentally. HEADLESS and STUB have no raster path and
are outside this pixel contract.

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

**Result:** the pixel output matched the FNA-derived expected formula
(`Texture × 2 × Texture2 × DiffuseColor`) at all 4 sample points. Writing the test also surfaced a
real, previously unaudited gap: the dedicated dual-texture shader path did not consume vertex
color. That became Task 889 and was subsequently fixed; it is no longer an open limitation.

Task 889's shared regression uses `VertexPositionColorTexture`, half-gray × white textures and
non-degenerate diffuse/alpha values. It requires different exact outputs when
`VertexColorEnabled` changes, so a shader that ignores the property cannot pass. EasyGL registers
it as `EasyGL_DualTextureEffect_VertexColor` and SDLGPU reuses the same source as
`SdlGpu_CrossFamily_DualTextureEffect_VertexColor`. WebGPU's `WebGPU_DualTexture3D` independently
covers the colored stride and requires a green vertex tint to survive dual-texture sampling.

## Support matrix

| Feature | EasyGL |
|---|---|
| Property defaults (9/9) | ✅ Task 381/382 |
| `color.rgb *= 2` doubling factor | ✅ fixed Task 383 |
| Basic two-texture multiply (magenta×yellow) | ✅ Task 133/384 |
| `Alpha` premultiplication | ✅ Task 385 |
| First texture (`Texture`) null-fallback | ✅ Task 386 / SOFTWARE-302 |
| Second texture (`Texture2`) null-fallback | ✅ Task 387 / SOFTWARE-302 |
| Fog (`FogEnabled`/`FogColor`/`FogStart`/`FogEnd`) | ✅ fixed Task 388 |
| Independent `TextureCoordinate0` / `TextureCoordinate1` | ✅ fixed SAMPLE-073 |
| `VertexColorEnabled` | ✅ fixed Task 889 |
| Combined integration capstone | ✅ Task 389 |

Legend: ✅ verified working.

## Subsequent fixes

- **SAMPLE-073 follow-up (2026-08-31)** — the earlier matrix never exercised FNA's distinct
  `TEXCOORD0`/`TEXCOORD1` inputs. EasyGL incorrectly forwarded one UV to both samplers. Its stock
  declaration mapper and shaders now consume usage indices 0 and 1 independently; the real-GL
  `EasyGL_DualTextureEffect_IndependentUV` regression uses the original SoccerPitch 40-byte
  position/normal/two-UV declaration and fails black if the second sampler reuses the first UV.
- **Task 889** (opened by Task 389, fixed by `ceacce7a9`) — added the vertex-color-carrying
  dual-texture shader variant and `vin.Color` multiplication required by FNA's
  `VSDualTextureVc`/`VSDualTextureVcNoFog` paths. The current EasyGL implementation retains the
  colored program and the shared regression; newer renderers implement and test the same public
  contract through their own pipelines.
- **SOFTWARE-302/303** — replaced CNA's invented white/null-bypass behavior with Microsoft XNA's
  measured opaque-black sample for either missing classic stock-effect slot. The shared indexed
  and non-indexed matrix now owns this contract across the active rasterizers.

Phase 44 and all follow-up work named on this page are closed. Current renderer-wide capability
status belongs in `graphics-renderer-feature-matrix.md`; unresolved defects belong in
`handoff.md` section 5.
