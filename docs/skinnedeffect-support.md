# SkinnedEffect Exactness Support Matrix

Phase 46 (`plans/plan_graphics.md` Tasks 401–410) audited and pixel-verified `SkinnedEffect` conformance
against FNA across all three graphics renderers (EasyGL, Vulkan, Bgfx). This document summarizes
the findings and closes the phase.

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

Opened 3 new backlog tasks: **Task 893** (`DirectionalLight1`/`DirectionalLight2` unforwarded,
same shape as `BasicEffect`'s Task 885 and `EnvironmentMapEffect`'s Task 890 — `SkinnedEffect.fx`
shares the same `Lighting.fxh`/`ComputeLights` mechanism), **Task 894** (`SpecularColor`/
`SpecularPower` have zero GPU implementation on any of the 3 renderers — `GpuDrawParams` has no
generic specular fields at all, same shape as `BasicEffect`'s Task 886), and **Task 895**
(`WeightsPerVertex` is a complete GPU no-op on all 3 renderers — FNA's real `Skin(vin, boneCount)`
HLSL function only sums the first `boneCount` weight/index pairs, but CNA's skinning shader
unconditionally sums all 4 regardless of the property's value; only visible when unused weight
slots hold nonzero data). Also noted, **not** opened as a bug (an acceptable, strictly-more-
accurate deviation, same class as Task 396's per-pixel-Fresnel note): `PreferPerPixelLighting=false`
is effectively a no-op since `NdotL` is always computed in the fragment shader on every renderer.

## 2. Default-value unit tests, `MaxBones` linker gap (Task 402)

Task 401 found **zero existing test coverage** for `SkinnedEffect`. Task 402 wrote
`tests/Microsoft/Xna/Framework/Graphics/SkinnedEffectTests.cpp` from scratch (mirroring Task
372/382/392's precedent): 52 tests covering all property defaults, `LightingEnabled`'s
throw-on-`false`/no-throw-on-`true` behavior, `EnableDefaultLighting()`'s effects (byte-for-byte
identical constants to Task 363's own `BasicEffect` confirmation, since both share FNA's identical
`EffectHelpers.EnableDefaultLighting()` helper), `SetBoneTransforms`/`GetBoneTransforms`'s full
bounds-checking matrix, `WeightsPerVertex`'s accept-1/2/4/throw-otherwise validation, setter
round-trips, and a `Clone()` test. **The `Clone()` test is the regression guard for Task 401's own
fix** — `git stash`-confirmed (via direct edit+rebuild) it fails exactly as predicted with the fix
reverted (`SpecularColor` read back `(0,0,0)` instead of the test's actual value, `SpecularPower`
read back `0` instead of `64`).

Also fixed a genuinely new, unrelated build-breaking discovery: `SkinnedEffect::MaxBones` (an
in-class-initialized `static const int`) had no out-of-line definition, causing a linker error the
moment any code (here, GTest's comparison machinery) took its address. Fixed per CLAUDE.md's own
"Static Members and Named Constants" convention.

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

**Found a genuinely new Bgfx-specific test-harness pitfall** (not a `SkinnedEffect`/renderer bug):
reading 3 distinct screen rectangles from a single rendered Bgfx frame within one retry-loop
iteration only reliably reflects the *first* read — every prior multi-region Bgfx pixel test in
this project (audited all existing `bgfx_*_test.cpp` files) already reads exactly one rectangle
per draw+retry pass. Fixed the test (not production code) by refactoring to a `renderAndRead()`
helper performing its own full clear+draw+retry-loop+single-read pass per checkpoint — now the
established pattern reused by every subsequent multi-point Bgfx test in this phase (Tasks 407–409).

## 5. Single translation bone (Task 407)

**Verify-only, zero bugs found.** Formalized the pre-existing (Task 123) EasyGL-only
`skinned_effect_integration_test.cpp` translation-bone scenario into Phase 46's own per-renderer
naming/registration convention, extending it to Vulkan and Bgfx for the first time. All vertices
bound 100% to a single bone set to a real, non-identity `Matrix.CreateTranslation(+0.5,0,0)`. All
3 renderers produced the exact predicted output on the first attempt (EasyGL `(174,0,0)`, Vulkan/
Bgfx `(160,0,0)` — the small EasyGL-vs-Vulkan/Bgfx RGB difference is the same pre-existing
per-renderer lighting-precision variance seen throughout this project, not a bug).

## 6. Two-bone weighted blend (Task 408)

**Verify-only, zero bugs found.** Confirmed FNA's real `Skin(vin, boneCount)` formula for
`boneCount=2`: `skinMat = Bones[Indices[0]]×Weights[0] + Bones[Indices[1]]×Weights[1]`.
Deliberately chose a discriminating, non-trivial bone pair rather than a saturated 0/1 split:
`Bones[0]=Translate(-0.5,0,0)`, `Bones[1]=Translate(+1.5,0,0)`, weights split `0.5`/`0.5` → net
blended shift `= 0.5×(-0.5)+0.5×(1.5) = +0.5`, a value distinct from either individual bone's own
shift — so a bug that picked one bone alone (ignoring the other's weight) would produce a clearly
different, wrong result rather than accidentally matching. All 3 renderers produced the exact
predicted output on the first attempt, byte-identical to Task 407's own per-renderer values.

**Discriminating power independently verified**: temporarily changed the EasyGL test's weights to
`(1,0)` (bone 0 alone) and reran — predicted shift becomes `-0.5` instead of `+0.5`; observed
exactly the predicted swap (quad moved to the *left* read-back point instead of centre) before
restoring the real `0.5`/`0.5` test.

## 7. Cross-renderer consistency (Task 409)

**Capstone, zero new bugs found.** Combined Tasks 406–408's individually-verified pieces (identity
no-op, single-bone translation, 2-bone weighted blend) into **one scene, one bone-palette upload,
one `DrawPrimitives` call covering 3 quads** — proving the pieces compose correctly *together
within a single draw*, not just when each is exercised in its own dedicated draw call. All 3 quads
share identical authored geometry and are distinguished only by their per-vertex weight/index
data, directly testing that the vertex shader reads genuinely per-vertex skinning attributes.

**Result: all 3 renderers produced the exact predicted output on the first attempt**, each
byte-identical across all 3 quads within itself and matching each renderer's own Task 406–408
single-quad values exactly (EasyGL all 3 = `(174,0,0)`; Vulkan/Bgfx all 3 = `(160,0,0)`) — strong
evidence the 3 independent bone/weight combinations apply correctly and simultaneously within one
draw call.

## Support matrix

| Feature | EasyGL | Vulkan | Bgfx |
|---|---|---|---|
| Property defaults | ✅ Task 401/402 | ✅ (shared C++) | ✅ (shared C++) |
| `Clone()` preserves `SpecularColor`/`SpecularPower` | ✅ fixed Task 401 | ✅ fixed Task 401 | ✅ fixed Task 401 |
| `MaxBones`/bone-count bounds-checking | ✅ Task 402/403/405 | ✅ (shared C++) | ✅ (shared C++) |
| `GetBoneTransforms` independent-copy semantics | ✅ Task 404 | ✅ (shared C++) | ✅ (shared C++) |
| Identity bone palette → zero deformation | ✅ Task 406 | ✅ Task 406 | ✅ Task 406 |
| Single-bone translation | ✅ Task 407 | ✅ Task 407 | ✅ Task 407 |
| Two-bone weighted blend | ✅ Task 408 | ✅ Task 408 | ✅ Task 408 |
| Multi-quad/multi-bone composition in one draw call | ✅ Task 409 | ✅ Task 409 | ✅ Task 409 |
| `DirectionalLight1`/`DirectionalLight2` | ✅ fixed Task 893 (2026-07-11) | ✅ fixed Task 893 | ✅ fixed Task 893 |
| `SpecularColor`/`SpecularPower` GPU implementation | ✅ fixed Task 894 (2026-07-11) | ✅ fixed Task 894 | ✅ fixed Task 894 |
| `WeightsPerVertex` GPU enforcement | ✅ fixed Task 895 (2026-07-11) | ✅ fixed Task 895 | ✅ fixed Task 895 |

Legend: ✅ verified working · ❌ confirmed not implemented.

## Open, tracked follow-up work

Phase 46 opened 3 new tracked tasks (all from Task 401's opener audit):

- ~~**Task 893**~~ — **fixed, 2026-07-11**: `DirectionalLight1`/`DirectionalLight2` now forward on
  `SkinnedEffect` on all 3 renderers, mirroring `BasicEffect`'s (Task 885/886) and
  `EnvironmentMapEffect`'s (Task 890) own fixes. See `NEXT.md` §3 for the full write-up.
- ~~**Task 894**~~ — **fixed, 2026-07-11**: real GPU specular highlights (half-vector Blinn-Phong,
  matching `BasicEffect`'s Task 886 formula exactly) now implemented for `SkinnedEffect`'s
  `SpecularColor`/`SpecularPower` on all 3 renderers, including new World-matrix/EyePosition
  plumbing each renderer's skinned shader previously had zero infrastructure for. See `NEXT.md` §3.
- ~~**Task 895**~~ — **fixed, 2026-07-11**: `WeightsPerVertex` is now a real GPU-enforced constraint
  on all 3 renderers — each renderer's skinning vertex shader gates bone-weight accumulation with
  `>=2`/`>=4` conditionals, matching FNA's real `Skin(vin, boneCount)` behavior of only summing the
  first `boneCount` weight/index pairs. See `NEXT.md` §3.

This closes Phase 46 (`plans/plan_graphics.md` Tasks 401–410) in full.
