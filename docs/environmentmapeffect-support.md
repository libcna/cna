# EnvironmentMapEffect exactness support matrix

`EnvironmentMapEffect`'s properties, `OnApply()` logic and pixel formulas are verified against
FNA's `EnvironmentMapEffect.cs`/`EnvironmentMapEffect.fx`, and the above-one amount rule against
measured Microsoft XNA 4.0.

---

## 1. Properties and defaults

All 14 properties match FNA: `World`/`View`/`Projection=Identity`, `DiffuseColor=Vector3.One`,
`EmissiveColor`/`AmbientLightColor=Zero`, `Alpha=1`, `FogEnabled=false`, `FogStart=0`, `FogEnd=1`,
`FogColor=Zero`, `Texture`/`EnvironmentMap=null`, `EnvironmentMapAmount=1`,
`EnvironmentMapSpecular=Zero`, `FresnelFactor=1`. The constructor leaves Fresnel enabled
(`fresnelEnabled_=true`) even though FNA's class-field default is `false`, because the constructor
assigns `FresnelFactor`. `IEffectLights.LightingEnabled` always reads true; setting it to `false`
throws (FNA's `NotSupportedException`, mapped to `std::runtime_error`). `FillGpuDrawParams()`
forwards all three directional lights.

`modules/graphics/tests/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffectTests.cpp` covers all
14 defaults, `LightingEnabled`'s throw-on-`false`/no-throw-on-`true` behavior,
`EnableDefaultLighting()`'s ambient/light-enable effects, setter round-trips, ownership, `Clone()`
and `GetTypeName()`. `Clone()` preserves `FogColor` on all four effects that re-link
`fogColorParam_` in `CacheEffectParameters()` (`AlphaTestEffect`, `DualTextureEffect`,
`EnvironmentMapEffect`, `SkinnedEffect`); their `Clone()` tests check it.

## 2. Pixel formulas

FNA's `PSEnvMap`/`PSEnvMapSpecular` and `ComputeEnvMapVSOutput` define the contract:

```
// vertex stage (ComputeEnvMapVSOutput)
vout.Specular.rgb = useFresnel
    ? pow(max(1 - abs(dot(eyeVector, worldNormal)), 0), FresnelFactor) * EnvironmentMapAmount
    : EnvironmentMapAmount;
vout.EnvCoord = reflect(-eyeVector, worldNormal);
// pixel stage
envmap    = SAMPLE_CUBEMAP(EnvironmentMap, pin.EnvCoord) * color.a;
color.rgb = lerp(color.rgb, envmap.rgb, pin.Specular.rgb);
color.rgb += EnvironmentMapSpecular * envmap.a;          // specular variants only
```

- **Lerp, not add.** The cube sample replaces the lit/textured color in proportion to the amount.
  At `Amount=0` an additive formula coincides with the lerp; a non-saturated gray cube
  `(128,128,128)` over a nonzero lit color separates them (a white cube saturates both).
- **Both cube terms are alpha-scaled.** `envmap = sample * color.a` scales the lerp target RGB and
  the specular `envmap.a`. With a translucent cube (`alpha=128`), `Amount=0` and opaque inputs, the
  specular case gives `(151,101,76)`. The shared lerp regression uses `Alpha=0.5`, where an unscaled
  cube sample produces twice the expected RGB.
- **Fresnel is per vertex.** The amount is computed in the vertex stage and interpolated; a
  per-fragment calculation gives a different gradient. A head-on camera
  (`viewAngle≈1`) gives `pow(0,F)=0`, fully suppressing the cube map at normal incidence.
- **Reflection uses `EyePosition`.** A cube with a distinct color per face and two `CreateLookAt`
  eyes on a fixed target hit `PositiveZ` and `NegativeX` respectively.
- **Normals use `transpose(inverse(World3x3))`.** EasyGL computes the normal matrix on the CPU as
  `cofactor(World3x3)/det(World3x3)`; a raw `World` 3x3 is wrong under non-uniform scale.
- **Fog** uses FNA's view-space dot-product factor, including the `FogStart == FogEnd` case. The
  shared fog pixel contract and its SDLGPU caveat (CNA-FIX-D01) are in
  [sdl-gpu-renderer.md](sdl-gpu-renderer.md#stock-effect-fog-alpha-cna-fix-d01).

The composition test combines the lerp, alpha-scaled specular, Fresnel suppression,
`EyePosition` reflection and a non-uniform-scale `World`, and must land on `(151,101,76)` through
Fresnel suppression rather than a hardcoded `Amount=0`.

## 3. `EnvironmentMapAmount` above 1 is saturated

`EnvironmentMapAmount` has no clamp in its property setter (`EnvironmentMapEffect.cs:283`), so a
game can hand the shader a value above 1. XNA still renders it as 1, and the reason is not in the
effect at all: the factor is written to `vout.Specular.rgb` (`ComputeEnvMapVSOutput`), and
`Structures.fxh:156` declares that member `Specular : COLOR1`, so **Direct3D 9 saturates it before
interpolation** — the same `oD0`/`oD1` rule FX-123 measured for the lit programs.

**Measured on the real XNA 4.0 runtime**, not inferred from the semantic:
`spikes/xna-envmap-amount-clamp-spike/`, a quad with a `(60,60,60)` base texture and a
`(200,200,200)` environment cube, `FresnelFactor = 0`:

| `EnvironmentMapAmount` | 0.5 | 1.0 | 2.0 | 3.0 |
|---|---|---|---|---|
| real XNA | `(100,100,100)` | `(200,200,200)` | `(200,200,200)` | `(200,200,200)` |

The 0.5 column is the positive control: without it, "2.0 renders as 1.0" cannot be told apart from
"the amount does nothing on this stack".

EasyGL clamps the Fresnel/amount varying in the vertex program in
`GlStockShaderSources.hpp`. The clamp belongs in the **vertex** stage, where D3D9 applies it —
clamping after interpolation would interpolate the raw value first and give a different gradient.

Guarded by `modules/graphics/examples/environmentmapeffect_amount_clamp_test.cpp`, registered as
`SdlGpu_EnvironmentMapEffect_AmountClamp` and `EasyGL_CrossFamily_EnvironmentMapEffect_AmountClamp`.
The source is renderer-agnostic and its legs are relational, so registering it for another
renderer is one line in that family's `CMakeLists.txt`.

**The unlit sibling is the same rule one effect over** (`vout.Diffuse : COLOR0`); see
[`basiceffect-support.md`](basiceffect-support.md#d3d9-vertex-color-output-saturation).

## Support matrix

EasyGL registrations are `EasyGL_EnvironmentMapEffect_<suffix>`; SDLGPU reuses most of the same
sources as `SdlGpu_CrossFamily_EnvironmentMapEffect_<suffix>`.

| Feature | EasyGL | Test suffix |
|---|---|---|
| Property defaults (14/14) | ✅ | `EnvironmentMapEffectTests` |
| `Clone()` preserves `FogColor` | ✅ | `EnvironmentMapEffectTests` |
| `Amount=0` ignores the cube map | ✅ | `AmountZero` |
| Cube-map blend: `lerp` not additive | ✅ | `AmountOne` |
| `EnvironmentMapSpecular × envmap.a` | ✅ | `Specular` |
| Base lerp `envColor × combinedAlpha` | ✅ | `AlphaScaledLerp` |
| Fresnel edge-weighting and per-vertex interpolation | ✅ | `Fresnel`, `Fresnel_Gradient` |
| `EyePosition` → reflection vector | ✅ | `EyePosition` |
| `World` non-uniform-scale normal transform | ✅ | `WorldTransform` |
| `DirectionalLight1`/`DirectionalLight2` | ✅ | `MultiLight` |
| Fog forwarding and view-space factor | ✅ | `Fog` |
| `EnvironmentMapAmount > 1` vertex saturation | ✅ | `AmountClamp` |
| Combined composition | ✅ | `Combined` |
