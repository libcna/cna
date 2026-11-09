# DualTextureEffect exactness support matrix

`DualTextureEffect`'s properties, `OnApply()` logic and pixel output are verified against FNA's
`DualTextureEffect.cs`/`DualTextureEffect.fx` and, for missing textures, against measured
Microsoft XNA 4.0.

---

## 1. Properties and defaults

All 9 properties match FNA: `FogEnabled`/`VertexColorEnabled=false`,
`World`/`View`/`Projection=Identity`, `DiffuseColor=Vector3.One`, `Alpha=1`, `FogStart=0`,
`FogEnd=1`, `FogColor=Zero`, `Texture`/`Texture2=null`. The `EffectDirtyFlags` usage,
property-setter dirty-flag side effects, `OnApply()`'s `SetWorldViewProjAndFog`/`SetFogVector`
logic, and `Clone()`'s field-copy list match FNA literal-for-literal.

`modules/graphics/tests/Microsoft/Xna/Framework/Graphics/DualTextureEffectTests.cpp` covers all 9
defaults, setter round-trips (including `Texture`/`Texture2`), ownership, `Clone()`,
`GetTypeName()`, forwarded alpha and the measured null-sampler contract.

## 2. Two-texture blend formula

FNA's `PSDualTexture` computes:

```
color = SAMPLE(Texture); color.rgb *= 2; color *= overlay * pin.Diffuse;
```

The first texture's RGB is doubled (alpha untouched) before the multiply with the second texture
and the diffuse color. CNA's dual-texture fragment shaders apply the same `base.rgb *= 2.0`.
Tests that use only 0/1-saturated texels cannot detect a missing doubling factor, because the
result clamps back to the same value; `EasyGL_DualTextureEffect_Doubling` checks the factor
directly, and the `TextureFilter::Point vs Linear` test uses `DiffuseColor=0.5` so its thresholds
hold with the factor applied.

The two samplers read independent texture coordinates: usage indices `TEXCOORD0` and `TEXCOORD1`
feed `Texture` and `Texture2` respectively. The real-GL `EasyGL_DualTextureEffect_IndependentUV`
regression (SAMPLE-073) uses a 40-byte position/normal/two-UV declaration and fails black if the
second sampler reuses the first UV.

## 3. Alpha premultiplication

`FillGpuDrawParams()` forwards `Vector4(diffuseColor * alpha, alpha)`, matching FNA. Three
GPU-independent unit tests check that output directly, and `BlendState.AlphaBlend` pixel tests on
EasyGL prove the premultiplied RGB blends correctly against a real background.

## 4. Null textures

`DualTextureEffect` has no `TextureEnabled` flag. Measured Microsoft XNA 4.0 (SOFTWARE-302/303):
either null slot samples **opaque black**, including alpha. CNA has no white fallback and no
null-bypass.

The shared `DualTextureEffectNullSamplerTest` holds every active rasterizing renderer — EasyGL,
Software, WebGPU, SDLGPU, FNA3D and DirectX9 — to this rule in both indexed and non-indexed draws.
Each case first installs non-black textures, then nulls one slot, so an invented white fallback or
a stale binding cannot pass. HEADLESS and STUB have no raster path and are outside this pixel
contract.

## 5. Fog

`FillGpuDrawParams()` forwards `FogEnabled`, `FogColor`, `FogStart` and `FogEnd`; the dedicated
dual-texture program (`EnsureDualTextured3DProgram()` on EasyGL) carries
`uFogEnabled`/`uFogColor`/`uFogStart`/`uFogEnd` and the standard `vFogFactor`/`mix()` blend. The
shared fog pixel contract checks the product of effect alpha and both texture alphas; its SDLGPU
alpha-composition caveat (CNA-FIX-D01) is in
[sdl-gpu-renderer.md](sdl-gpu-renderer.md#stock-effect-fog-alpha-cna-fix-d01).

## 6. Vertex color and composition

With `VertexColorEnabled`, the vertex color multiplies the result, as in FNA's
`VSDualTextureVc`/`VSDualTextureVcNoFog` paths. The shared regression uses
`VertexPositionColorTexture`, half-gray × white textures and non-degenerate diffuse/alpha values,
and requires different exact outputs when `VertexColorEnabled` changes. It registers as
`EasyGL_DualTextureEffect_VertexColor` and `SdlGpu_CrossFamily_DualTextureEffect_VertexColor`;
WebGPU's `WebGPU_DualTexture3D` independently requires a green vertex tint to survive
dual-texture sampling.

A composition test samples a real 2×2 multi-texel texture at all 4 texel centers and requires
`Texture × 2 × Texture2 × DiffuseColor` at each point.

## Support matrix

| Feature | EasyGL | Test |
|---|---|---|
| Property defaults (9/9) | ✅ | `DualTextureEffectTests` |
| `color.rgb *= 2` doubling factor | ✅ | `EasyGL_DualTextureEffect_Doubling` |
| Basic two-texture multiply (magenta×yellow) | ✅ | `EasyGL_DualTextureEffect_Blend` |
| `Alpha` premultiplication | ✅ | `EasyGL_DualTextureEffect_Alpha` |
| First texture (`Texture`) null → opaque black | ✅ | `EasyGL_DualTextureEffect_NullTexture0` |
| Second texture (`Texture2`) null → opaque black | ✅ | `EasyGL_DualTextureEffect_NullTexture2` |
| Fog (`FogEnabled`/`FogColor`/`FogStart`/`FogEnd`) | ✅ | `EasyGL_DualTextureEffect_Fog` |
| Independent `TextureCoordinate0` / `TextureCoordinate1` | ✅ | `EasyGL_DualTextureEffect_IndependentUV` |
| `VertexColorEnabled` | ✅ | `EasyGL_DualTextureEffect_VertexColor` |
| Combined 2×2 composition | ✅ | `EasyGL_DualTextureEffect_Combined` |
