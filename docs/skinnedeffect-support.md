# SkinnedEffect exactness support matrix

`SkinnedEffect`'s properties, bone palette, `OnApply()` logic and pixel output are verified against
FNA's `SkinnedEffect.cs`/`SkinnedEffect.fx` and, for missing textures, against measured Microsoft
XNA 4.0.

---

## 1. Properties and defaults

Every default matches FNA: `World`/`View`/`Projection=Identity`, `DiffuseColor=Vector3.One`,
`EmissiveColor`/`AmbientLightColor=Zero`, `Alpha=1`, `PreferPerPixelLighting=false`,
`FogEnabled=false`, `FogStart=0`, `FogEnd=1`, `FogColor=Zero`, `Texture=null`,
`WeightsPerVertex=4`, `MaxBones=72`, `SpecularColor=Vector3.One`, `SpecularPower=16`. The
constructor calls `SetBoneTransforms(identityBones)`, so all 72 bone slots start as
`Matrix.Identity`.

`IEffectLights.LightingEnabled` always reads true; setting it to `false` throws.
`EnableDefaultLighting()` installs the same constants as `BasicEffect` (both use FNA's
`EffectHelpers.EnableDefaultLighting()`). `Clone()` copies `SpecularColor`, `SpecularPower` and
`FogColor` into the clone's recreated `EffectParameter`s, not only into cache fields. `MaxBones` has
an out-of-line definition, so taking its address links.

`modules/graphics/tests/Microsoft/Xna/Framework/Graphics/SkinnedEffectTests.cpp` covers all
defaults, `LightingEnabled`, `EnableDefaultLighting()`, the bone bounds matrix, `WeightsPerVertex`
validation, ownership, setter round-trips, `Clone()` (fails with `SpecularColor=(0,0,0)` and
`SpecularPower=0` if the parameter copy is removed) and `GetTypeName()`.

## 2. Bone palette and bounds

- `SetBoneTransforms` throws on empty input and on more than `MaxBones` matrices;
  `GetBoneTransforms` throws for a count `<=0` or `>MaxBones`
  (`SetBoneTransformsAcceptsExactlyMaxBones`, `SetBoneTransformsThrowsWhenExceedingMaxBones`).
- `GetBoneTransforms` restores `M44=1` on every returned matrix — FNA's 4×3-to-4×4 GPU-packing
  quirk.
- `GetBoneTransforms` returns an independent copy: `EffectParameter::GetValueMatrixArray()` builds
  a new `std::vector<Matrix>` each call, as FNA's `GetValueMatrixArray(count)` allocates a fresh
  `Matrix[]`. A test mutates the first result and checks that a second call is unaffected.
- `WeightsPerVertex` accepts 1, 2 or 4 and throws otherwise. The vertex shader sums only the first
  `WeightsPerVertex` weight/index pairs, as FNA's `Skin(vin, boneCount)` does; the regression puts
  nonzero data in the unused slots.
- `BLENDINDICES0` may be declared as `Byte4` or `Vector4` (FX-127); the floating-point form selects
  the requested bone across vertex-lit, per-pixel and colored variants.

## 3. Lighting and normals

- `PreferPerPixelLighting=false` computes lighting per vertex and Gouraud-interpolates it; `true`
  evaluates it per pixel. EasyGL has distinct programs selected by the property. The focused test's
  centre pixel is about 127 on the vertex-lit path and 155 on the per-pixel path, so a decorative
  selector cannot pass.
- All three directional lights reach the renderer; the regression varies lights 1 and 2
  independently.
- `SpecularColor`/`SpecularPower` drive the stock half-vector Blinn-Phong term with World and
  `EyePosition` transport.
- Normals: bone matrices transform the normal directly, then `WorldInverseTranspose` applies the
  outer world transform (GLTF-264). The regression includes non-identity rotation and non-uniform
  scale.
- Fog uses FNA's view-space dot-product factor, including `FogStart == FogEnd`. The shared fog
  pixel contract and its SDLGPU caveat (CNA-FIX-D01) are in
  [sdl-gpu-renderer.md](sdl-gpu-renderer.md#stock-effect-fog-alpha-cna-fix-d01).
- A null `Texture` samples measured opaque black.

## 4. Pixel coverage

FNA's `Skin(vin, boneCount)` computes `skinMat = Σ Bones[Indices[i]] × Weights[i]` over the first
`boneCount` pairs. The focused programs use a discriminating bone setup:

- **Identity palette** — the default palette leaves the quad at its authored position (contrast with
  `skinned_effect_integration_test.cpp`, which moves bone 0 by `+0.5` X).
- **Single translation bone** — `CreateTranslation(+0.5,0,0)` with all weight on one bone gives
  `(174,0,0)` at the read-back point.
- **Two-bone blend** — `Bones[0]=Translate(-0.5,0,0)`, `Bones[1]=Translate(+1.5,0,0)`, weights
  `0.5`/`0.5`: net shift `+0.5`, distinct from either bone alone; weights `(1,0)` would move the quad
  to the left read-back point instead.
- **Composition** — one palette upload and one `DrawPrimitives` call cover 3 quads with identical
  geometry distinguished only by per-vertex weights/indices; all three must read `(174,0,0)`.

The renderer-neutral `EasyGL_SkinnedEffectContract` program holds the null-texture rule, material
composition, the three-light paths and 1/2/4-bone skinning together. EasyGL owns the focused
programs; SDLGPU reuses the same sources through the cross-family corpus.

## Support matrix

EasyGL registrations are `EasyGL_SkinnedEffect_<suffix>`.

| Feature | EasyGL | Test |
|---|---|---|
| Property defaults | ✅ | `SkinnedEffectTests` |
| `Clone()` preserves `SpecularColor`/`SpecularPower` | ✅ | `SkinnedEffectTests` |
| `MaxBones`/bone-count bounds-checking | ✅ | `SkinnedEffectTests` |
| `GetBoneTransforms` independent-copy semantics | ✅ | `SkinnedEffectTests` |
| Identity bone palette → zero deformation | ✅ | `IdentityBones` |
| Single-bone translation | ✅ | `TranslationBone` |
| Two-bone weighted blend | ✅ | `TwoBoneBlend` |
| Multi-quad/multi-bone composition in one draw call | ✅ | `Combined` |
| `DirectionalLight1`/`DirectionalLight2` | ✅ | `MultiLight` |
| `SpecularColor`/`SpecularPower` GPU implementation | ✅ | `Specular` |
| `WeightsPerVertex` GPU enforcement | ✅ | `WeightsPerVertex` |
| `PreferPerPixelLighting` vertex/pixel dispatch | ✅ | `PreferPerPixelLighting` |
| Fog forwarding and view-space factor | ✅ | `Fog` |
| Bone-space then inverse-transpose World normal | ✅ | `WorldNormal` |
| `Byte4` and `Vector4` bone-index declarations | ✅ | `EasyGL_SkinnedEffectVector4BoneIndices` |
| Null texture samples measured opaque black | ✅ | `EasyGL_SkinnedEffectContract` |
