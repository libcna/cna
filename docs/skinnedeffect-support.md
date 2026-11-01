# SkinnedEffect exactness support matrix

Phase 46 (graphics Tasks 401–410) audited and pixel-verified `SkinnedEffect` conformance
against FNA. This document records those findings and the later fixes that closed the phase's
follow-up work. Current-state reconciliation: 2026-10-31.

---

## 1. Property/default audit, `Clone()` bug (Task 401)

Task 401 audited all `SkinnedEffect` properties, `MaxBones`, `SetBoneTransforms`/
`GetBoneTransforms`'s bounds-checking, and `OnApply()`'s material-color/lighting-matrix/
shader-index logic line-by-line against FNA's `SkinnedEffect.cs` — mirroring Task 361/371/381/391's
opener precedent. Every default matched exactly (`World`/`View`/`Projection=Identity`,
`DiffuseColor=Vector3.One`, `EmissiveColor`/`AmbientLightColor=Zero`, `Alpha=1`,
`PreferPerPixelLighting=false`, `FogEnabled=false`, `FogStart=0`, `FogEnd=1`, `FogColor=Zero`,
`Texture=null`, `WeightsPerVertex=4`, `MaxBones=72`, `SpecularColor=Vector3.One`,
`SpecularPower=16`), including the constructor's `SetBoneTransforms(identityBones)` call
initializing all 72 bone slots to `Matrix.Identity`. `SetBoneTransforms`/`GetBoneTransforms`'s
bounds-checking matched FNA exactly (throws on empty/`>MaxBones`/`<=0` input; `GetBoneTransforms`
restores `M44=1` on every returned matrix, FNA's own 4×3-to-4×4 GPU-packing quirk).

**Real, confirmed, FIXED bug found**: `Clone()`'s copy constructor set the private
`specularColor_`/`specularPower_` cache fields directly but never updated the freshly-recreated
`specularColorParam_`/`specularPowerParam_` `EffectParameter`s — since the getters always read
from the parameter when non-null (always true post-`CacheEffectParameters()`), every clone's
`SpecularColor`/`SpecularPower` silently reset to `(0,0,0)`/`0` regardless of the source's actual
values. **The identical architectural bug shape Task 392 already fixed for `FogColor`** across 4
stock effects (`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`),
undetected here since `SkinnedEffect` had zero existing test coverage until this task. Fixed with
the same one-line-per-field pattern used for `FogColor`.

Opened 3 follow-up tasks: **Task 893** (`DirectionalLight1`/`DirectionalLight2` unforwarded,
same shape as `BasicEffect`'s Task 885 and `EnvironmentMapEffect`'s Task 890 — `SkinnedEffect.fx`
shares the same `Lighting.fxh`/`ComputeLights` mechanism), **Task 894** (`SpecularColor`/
`SpecularPower` have zero GPU implementation — `GpuDrawParams` has no
generic specular fields at all, same shape as `BasicEffect`'s Task 886), and **Task 895**
(`WeightsPerVertex` is a complete GPU no-op — FNA's real `Skin(vin, boneCount)`
HLSL function only sums the first `boneCount` weight/index pairs, but CNA's skinning shader
unconditionally sums all 4 regardless of the property's value; only visible when unused weight
slots hold nonzero data). Tasks 893–895 were all fixed on 2026-07-11.

The audit also noted that `PreferPerPixelLighting=false` was effectively a no-op because EasyGL
always evaluated lighting in the fragment shader. The phase originally called this an acceptable,
more-accurate deviation. Task 1102b later corrected that classification: XNA's default `false`
path computes lighting per vertex and Gouraud-interpolates it, which is observably different.
EasyGL now has distinct vertex-lit and per-pixel programs selected by the property.

## 2. Default-value unit tests, `MaxBones` linker gap (Task 402)

Task 401 found **zero existing test coverage** for `SkinnedEffect`. Task 402 wrote
`modules/graphics/tests/Microsoft/Xna/Framework/Graphics/SkinnedEffectTests.cpp` from scratch
(mirroring Task 372/382/392's precedent). The current 57-case fixture covers all property defaults,
`LightingEnabled`'s throw-on-`false`/no-throw-on-`true` behavior,
`EnableDefaultLighting()`'s effects (byte-for-byte identical constants to Task 363's own
`BasicEffect` confirmation, since both share FNA's identical
`EffectHelpers.EnableDefaultLighting()` helper), `SetBoneTransforms`/`GetBoneTransforms`'s full
bounds-checking matrix, `WeightsPerVertex`'s accept-1/2/4/throw-otherwise validation, ownership,
setter round-trips, `Clone()` and `GetTypeName()`. **The `Clone()` test is the regression guard for
Task 401's own fix** — it fails with `SpecularColor=(0,0,0)` and `SpecularPower=0` if the parameter
copy is removed.

Also fixed a genuinely new, unrelated build-breaking discovery: `SkinnedEffect::MaxBones` (an
in-class-initialized `static const int`) had no out-of-line definition, causing a linker error the
moment any code (here, GTest's comparison machinery) took its address. It now follows the
project's static-member definition convention.

## 3. Bone-count bounds-checking (Tasks 403–405)

Tasks 403 ("verify `SetBoneTransforms` accepts exactly supported bone count") and 405 ("verify too
many bones throws correct exception") were **both already fully satisfied by Task 402's own test
coverage** — `SetBoneTransformsAcceptsExactlyMaxBones`/`SetBoneTransformsThrowsWhenExceedingMaxBones`
already exercised precisely this scope. Marked done with a documentation-only note, no new code.

Task 404 verified `GetBoneTransforms` returns a genuinely independent copy, not an alias into
internal storage — **zero bugs found**. `EffectParameter::GetValueMatrixArray()` builds a
brand-new `std::vector<Matrix>` from scratch every call, matching FNA's own array-allocating
semantics (`bonesParam.GetValueMatrixArray(count)` likewise allocates a fresh `Matrix[]` every
call). Added a test that mutates the first call's returned vector and confirms a second call is
unaffected — discriminating power confirmed by construction (an alias-based implementation would
have let the mutation corrupt the underlying storage).

## 4. Identity bone palette (Task 406)

**First real pixel/rendering test for `SkinnedEffect` in this phase** — Tasks 401–405 were all
unit tests/audits, no GPU rendering exercised yet. **Verify-only, zero bugs found.** Confirmed the
skinning math correctly degenerates to a no-op when every bone transform is `Identity`: for a
vertex bound 100% to a single bone, `skinMat = 1 × Identity = Identity`, so the mesh renders at
exactly its authored position. Deliberately structured as a direct contrast with the pre-existing
(Task 123) `modules/graphics/examples/skinned_effect_integration_test.cpp`, which uses identical geometry but sets
bone 0 to a `+0.5` X translation — this test leaves the default identity palette untouched and
confirms the quad does **not** move.

## 5. Single translation bone (Task 407)

**Verify-only, zero bugs found.** Formalized the pre-existing (Task 123)
`skinned_effect_integration_test.cpp` translation-bone scenario into Phase 46's own per-renderer
naming/registration convention. All vertices bound 100% to a single bone set to a real,
non-identity `Matrix.CreateTranslation(+0.5,0,0)`; EasyGL produced the exact predicted output
`(174,0,0)` on the first attempt.

## 6. Two-bone weighted blend (Task 408)

**Verify-only, zero bugs found.** Confirmed FNA's real `Skin(vin, boneCount)` formula for
`boneCount=2`: `skinMat = Bones[Indices[0]]×Weights[0] + Bones[Indices[1]]×Weights[1]`.
Deliberately chose a discriminating, non-trivial bone pair rather than a saturated 0/1 split:
`Bones[0]=Translate(-0.5,0,0)`, `Bones[1]=Translate(+1.5,0,0)`, weights split `0.5`/`0.5` → net
blended shift `= 0.5×(-0.5)+0.5×(1.5) = +0.5`, a value distinct from either individual bone's own
shift — so a bug that picked one bone alone (ignoring the other's weight) would produce a clearly
different, wrong result rather than accidentally matching. The exact predicted output appeared on
the first attempt, byte-identical to Task 407's own value.

**Discriminating power independently verified**: temporarily changed the EasyGL test's weights to
`(1,0)` (bone 0 alone) and reran — predicted shift becomes `-0.5` instead of `+0.5`; observed
exactly the predicted swap (quad moved to the *left* read-back point instead of centre) before
restoring the real `0.5`/`0.5` test.

## 7. Integration capstone (Task 409)

**Capstone, zero new bugs found.** Combined Tasks 406–408's individually-verified pieces (identity
no-op, single-bone translation, 2-bone weighted blend) into **one scene, one bone-palette upload,
one `DrawPrimitives` call covering 3 quads** — proving the pieces compose correctly *together
within a single draw*, not just when each is exercised in its own dedicated draw call. All 3 quads
share identical authored geometry and are distinguished only by their per-vertex weight/index
data, directly testing that the vertex shader reads genuinely per-vertex skinning attributes.

**Result: the exact predicted output on the first attempt**, byte-identical across all 3 quads and
matching the Task 406–408 single-quad values exactly (all 3 = `(174,0,0)` on EasyGL) — strong
evidence the 3 independent bone/weight combinations apply correctly and simultaneously within one
draw call.

## 8. Later exactness fixes

CNA-FIX-D01 additionally corrects SDLGPU's fog alpha composition. The shared pixel contract
verifies combined effect/texture alpha and an untextured effect-alpha case on SDLGPU, EasyGL and
Software. See [sdl-gpu-renderer.md](sdl-gpu-renderer.md#parity-with-easygl) for the evidence boundary.

- **Tasks 900/1111** added the missing fog forwarding and then corrected EasyGL's initial
  depth shortcut to FNA's real view-space dot-product formula, including the
  `FogStart == FogEnd` case.
- **Task 1102b** implemented XNA's real `PreferPerPixelLighting=false` vertex-lit/Gouraud path.
  The focused test uses a scene whose centre pixel is approximately 127 on the vertex-lit path
  and 155 on the per-pixel path, so a decorative selector cannot pass.
- **GLTF-264** restored the classic stock-effect normal contract: bone matrices transform the
  normal directly, then `WorldInverseTranspose` applies the outer world transform. The regression
  includes non-identity rotation and non-uniform scale cases that the original identity-World
  phase tests could not detect.
- **FX-127** accepts `BLENDINDICES0` declared as either `Byte4` or `Vector4`. Its tests prove the
  floating-point form selects the requested bone across vertex-lit, per-pixel and colored
  variants rather than merely permitting the declaration.
- The renderer-neutral `SkinnedEffect` contract additionally holds the measured opaque-black
  null-texture rule, material composition, the three-light paths and 1/2/4-bone skinning together.

## Support matrix

| Feature | EasyGL |
|---|---|
| Property defaults | ✅ Task 401/402 |
| `Clone()` preserves `SpecularColor`/`SpecularPower` | ✅ fixed Task 401 |
| `MaxBones`/bone-count bounds-checking | ✅ Task 402/403/405 |
| `GetBoneTransforms` independent-copy semantics | ✅ Task 404 |
| Identity bone palette → zero deformation | ✅ Task 406 |
| Single-bone translation | ✅ Task 407 |
| Two-bone weighted blend | ✅ Task 408 |
| Multi-quad/multi-bone composition in one draw call | ✅ Task 409 |
| `DirectionalLight1`/`DirectionalLight2` | ✅ fixed Task 893 (2026-07-11) |
| `SpecularColor`/`SpecularPower` GPU implementation | ✅ fixed Task 894 (2026-07-11) |
| `WeightsPerVertex` GPU enforcement | ✅ fixed Task 895 (2026-07-11) |
| `PreferPerPixelLighting` vertex/pixel dispatch | ✅ fixed Task 1102b |
| Fog forwarding and view-space factor | ✅ fixed Tasks 900/1111 |
| Bone-space then inverse-transpose World normal | ✅ fixed GLTF-264 |
| `Byte4` and `Vector4` bone-index declarations | ✅ fixed FX-127 |
| Null texture samples measured opaque black | ✅ shared contract |

Legend: ✅ verified working.

## Subsequent fixes

- **Task 893** (opened by Task 401, fixed 2026-07-11) — all three directional lights now reach
  the renderer. The regression varies lights 1 and 2 independently.
- **Task 894** (opened by Task 401, fixed 2026-07-11) — implemented the stock effect's
  half-vector Blinn-Phong specular term and the required World/EyePosition transport.
- **Task 895** (opened by Task 401, fixed 2026-07-11) — the vertex shader now limits accumulation
  to the first 1, 2 or 4 weight/index pairs according to `WeightsPerVertex`; unused nonzero slots
  make the regression discriminating.

EasyGL owns the original focused programs; SDLGPU reuses the same Phase 46 and follow-up sources
through the cross-family corpus. Phase 46 and all follow-up work named on this page are closed.
Current renderer-wide capability status belongs in `graphics-renderer-feature-matrix.md`;
unresolved defects belong in `handoff.md` section 5.
