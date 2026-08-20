# EnvironmentMapEffect Exactness Support Matrix

Phase 45 (`plans/plan_graphics.md` Tasks 391–400) audited and pixel-verified `EnvironmentMapEffect`
conformance against FNA across all three graphics renderers (EasyGL, Vulkan, Bgfx). This document
summarizes the findings and closes the phase.

---

## 1. Property/default audit (Tasks 391–392)

Task 391 audited all 14 `EnvironmentMapEffect` properties and `OnApply()`'s material-color/
lighting-matrix/shader-index logic line-by-line against FNA's `EnvironmentMapEffect.cs` —
**verify-only, zero bugs found**, mirroring Task 361/371/381's opener precedent. Every default
matched exactly (`World`/`View`/`Projection=Identity`, `DiffuseColor=Vector3.One`,
`EmissiveColor`/`AmbientLightColor=Zero`, `Alpha=1`, `FogEnabled=false`, `FogStart=0`, `FogEnd=1`,
`FogColor=Zero`, `Texture`/`EnvironmentMap=null`, `EnvironmentMapAmount=1`,
`EnvironmentMapSpecular=Zero`, `FresnelFactor=1`), including a subtle detail easy to get wrong
(`FresnelFactor`'s constructor-driven `fresnelEnabled_=true` post-construction state, correctly
encoded despite FNA's own class-field default being `false`). `IEffectLights.LightingEnabled`'s
always-true getter and throw-on-`false` setter matched FNA's `NotSupportedException` semantics
(mapped to `std::runtime_error`). The audit found `FillGpuDrawParams()` only forwards
`DirectionalLight0` — the same shape as `BasicEffect`'s already-tracked Task 885 gap — opened as
new Task 890 rather than folded into Task 885, since it's a distinct effect with its own dispatch.

Task 391 found **zero existing test coverage** for `EnvironmentMapEffect`. Task 392 wrote
`tests/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffectTests.cpp` from scratch (mirroring
Task 372/382's precedent): 42 tests covering all 14 property defaults, `LightingEnabled`'s
throw-on-`false`/no-throw-on-`true` behavior, `EnableDefaultLighting()`'s ambient/light-enable
effects, setter round-trips, and `Clone()`. **The `Clone()` test caught a real, previously-
undiscovered bug affecting 4 stock effects**: `Clone()` never preserved `FogColor` on
`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect` —
`CacheEffectParameters()` re-links `fogColorParam_` to a fresh, zero-valued parameter in the
clone, and nothing copied the source's actual value across. Fixed all 4 with a one-line
`fogColorParam_->SetValue(src.getFogColorProperty())` addition each; extended Task 372/382's own
`Clone()` tests to close the gap that let it go undetected.

## 2. Cube-map blend formula (Tasks 393–394)

Task 393 verified `EnvironmentMapAmount=0` correctly ignores the cube map on all 3 renderers —
**zero bugs in its own scope** — but surfaced a real formula-level discrepancy: FNA's real
`PSEnvMap` pixel shader **lerps** between the lit/textured color and the cube map
(`color.rgb = lerp(color.rgb, envmap.rgb, EnvironmentMapAmount)`), while CNA's actual shader
formula on all 3 renderers **added** the cube map on top instead
(`rgb = litRGB×texColor.rgb + envColor×Amount + specular`). Both formulas coincide exactly at
`Amount=0` (why Task 393's own scope couldn't catch it) but diverge sharply at `Amount=1`.

**Task 394 confirmed and FIXED this real formula bug on all 3 renderers.** A literal "white
cubemap" sub-case (matching the task's own title) couldn't discriminate the two formulas — both
saturate to `(255,255,255)` identically, the same 0/1-saturation blind spot Task 383 already
taught this project to watch for. A second, deliberately non-saturated **gray cubemap**
`(128,128,128)` sub-case, combined with a genuinely nonzero lit/textured contribution, cleanly
discriminated them: FNA's lerp predicts `(128,128,128)` (fully replaced); CNA's additive formula
predicted `(228,178,153)` (added on top) — empirically confirmed as CNA's exact pre-fix output.
**Fixed** on all 3 renderers by changing the additive term to a proper `mix()`
(`vec3 baseColor=litRGB*texColor.rgb; vec3 rgb=mix(baseColor,envColor,Amount)+specular;`).

## 3. `EnvironmentMapSpecular` alpha scaling (Task 395)

**Real, confirmed formula bug found and fixed on all 3 renderers.** FNA's real `PSEnvMapSpecular`
pixel shader scales the specular term by the cube map's own **alpha** channel, further scaled by
the combined texture×diffuse alpha (`envmap = SAMPLE_CUBEMAP(...) * color.a;
color.rgb += EnvironmentMapSpecular * envmap.a`). CNA's actual shader formula added
`EnvironmentMapSpecular` as a **flat, unscaled constant** on all 3 renderers, never reading the
cube map's alpha channel at all. Isolated the test with `EnvironmentMapAmount=0` (removing the
base-lerp term from the picture) and opaque texture/diffuse (`combinedAlpha=1`, isolating purely
to the cube map's own alpha channel): an opaque cube map (`alpha=255`) is not discriminating
(scaling by 1.0 changes nothing); a translucent cube map (`alpha=128`) is — FNA predicts
`(151,101,76)`, CNA's flat-additive formula predicts `(202,152,127)`, empirically confirmed as the
exact pre-fix output. **Fixed** on all 3 renderers by sampling the cube map's full `vec4` (not just
`.rgb`/`.xyz`) and scaling the specular term by `envSample.a × combinedAlpha`. Deliberately left
unfixed and opened as **Task 891**: the base lerp's `envColor` term is still unscaled by combined
texture×diffuse alpha (FNA's `envmap = SAMPLE_CUBEMAP(...) * color.a` scales both `envmap.rgb` and
`envmap.a`; this task only fixed the `.a` half used by the specular term) — only visible when
texture/diffuse alpha is strictly less than 1, unexercised by any test to date.

## 4. Fresnel edge-weighting (Task 396)

**Real, confirmed missing-feature gap found and fixed on all 3 renderers.** Tasks 393/394 already
confirmed CNA implemented **no Fresnel uniform at all** — the env-map blend factor was always the
flat `EnvironmentMapAmount` regardless of view angle, instead of FNA's real per-vertex
`pow(max(1-abs(dot(eyeVector,worldNormal)),0),FresnelFactor)*EnvironmentMapAmount` term (the
default, since `FresnelFactor=1` by construction). A grazing/coplanar camera setup (reused from
Tasks 393–395) reduces the Fresnel formula to exactly the flat `Amount` — a non-discriminating
sanity check. A genuinely new **head-on perspective camera** (eye looking straight at the surface)
makes `viewAngle≈1`, collapsing the Fresnel term to `pow(0,F)=0` — the cube map should be **fully
suppressed** at normal incidence (the physically-correct behavior). CNA's pre-fix flat-`Amount`
formula would instead still fully apply the cube map here: FNA predicts `(100,50,25)` (suppressed),
CNA's pre-fix formula predicted `(128,128,128)` (fully applied), empirically confirmed. **Fixed**
by adding `fresnelEnabled`/`fresnelFactor` to `GpuDrawParams` (forwarded from
`FillGpuDrawParams()`) and threading a per-pixel `viewAngle`/`pow()` term into all 3 renderers'
fragment shaders — Vulkan/Bgfx repurposed previously-unused UBO/uniform padding rather than
growing the layout. Computed per-pixel rather than FNA's per-vertex (then rasterizer-interpolated)
computation — an acceptable, strictly-more-accurate CNA-vs-FNA deviation on coarse tessellation,
not a regression.

## 5. `EyePosition` and reflection-vector correctness (Task 397)

**Verify-only, zero bugs found, no code changed.** Every prior env-map test in this phase used
solid-color cube maps, unable to detect a wrong reflection vector at all (every face samples
identically regardless of which face is actually hit). Built a cube map with a **distinct color
per face** and 2 camera positions via `Matrix::CreateLookAt` with a fixed origin target (so the
quad's centre always projects to screen centre regardless of how oblique the eye is): a
straight-on eye hits the `PositiveZ` face exactly; an off-axis eye hits the `NegativeX` face by a
~10× dominant-component margin — 2 clearly different, exactly-predicted faces, proof by
construction that `EyePosition` wiring works correctly end-to-end on all 3 renderers.

## 6. `World` transform / normal-matrix correctness (Task 398)

**Real, confirmed formula bug found and fixed on 2 of 3 renderers.** XNA content ships
`WorldInverseTranspose` for exactly this reason: transforming a normal by `World` directly is only
correct for pure rotation/uniform-scale/translation; under **non-uniform scale** the correct
transform is `transpose(inverse(World3x3))`. Auditing found: **EasyGL** computed the "normal
matrix" as `World`'s raw upper-left 3x3 on the CPU side (shared infra also used by `BasicEffect`'s
lit-textured pipeline) — WRONG. **Vulkan** already computes `transpose(inverse(mat3(world)))`
directly in-shader — already CORRECT, no fix needed. **Bgfx** transformed the normal via
`mul(u_world, vec4(a_normal,0.0))` directly — WRONG, same shape as EasyGL. Notably,
`EnvironmentMapEffect::OnApply()` already computes the correct `WorldInverseTranspose` for its own
`EffectParameter` (API/content-pipeline fidelity) — but this value was never forwarded into the
actual GPU dispatch on either buggy renderer. A synthetic, non-axis-aligned test normal combined
with a large non-uniform `World` scale produced reflection vectors with opposite dominant-face
results between the correct and buggy formulas, empirically confirmed as the exact pre-fix output
on both. **Fixed** via the standard cofactor/determinant shortcut
(`normalMatrix = cofactor(World3x3)/det(World3x3)`) computed on the CPU side on both renderers.
**Deliberately out-of-scope, opened as Task 892**: Bgfx's sibling `BasicEffect` lit-textured
shader has a **worse** bug — it transforms the normal by the full `World×View×Projection` matrix,
not even `World` alone, invisible in every existing `BasicEffect` Bgfx test since they all use
`Identity` `View`/`Projection`.

## 7. Cross-renderer consistency (Task 399)

Closed the pixel-verification work with a capstone test combining everything Tasks 394–398
verified individually — the lerp blend, alpha-scaled specular, Fresnel suppression,
`EyePosition`-driven reflection, and a non-uniform-scale `World` — to prove the fixes compose
correctly together, not just in isolation. Deliberately chose parameters so Fresnel-suppression
(not a hardcoded `Amount=0`) isolates the env-map contribution to almost exactly the specular
term, landing on the same easily-verified numeric target Task 395 derived (`(151,101,76)`) via a
genuinely different code path.

**Result: all 3 renderers produced the exact predicted `(151,101,76)` on the first attempt** —
EasyGL, Vulkan, and Bgfx alike. No new bugs found — pure integration verification; a clean pass on
the first attempt is itself a meaningful confirmation that Tasks 394–398's fixes don't just work
in narrow isolation but compose correctly when combined in one scene.

## Support matrix

| Feature | EasyGL | Vulkan | Bgfx |
|---|---|---|---|
| Property defaults (14/14) | ✅ Task 391/392 | ✅ (shared C++) | ✅ (shared C++) |
| `Clone()` preserves `FogColor` | ✅ fixed Task 392 | ✅ fixed Task 392 | ✅ fixed Task 392 |
| Cube-map blend: `lerp` not additive | ✅ fixed Task 394 | ✅ fixed Task 394 | ✅ fixed Task 394 |
| `EnvironmentMapSpecular × envmap.a` | ✅ fixed Task 395 | ✅ fixed Task 395 | ✅ fixed Task 395 |
| Base lerp `envColor × combinedAlpha` | ✅ fixed Task 891 | ✅ fixed Task 891 | ✅ fixed Task 891 |
| Fresnel edge-weighting | ✅ fixed Task 396 | ✅ fixed Task 396 | ✅ fixed Task 396 |
| `EyePosition` → reflection vector | ✅ Task 397 | ✅ Task 397 | ✅ Task 397 |
| `World` non-uniform-scale normal transform | ✅ fixed Task 398 | ✅ Task 398 (already correct) | ✅ fixed Task 398 |
| `DirectionalLight1`/`DirectionalLight2` | ✅ fixed Task 890 (2026-07-11) | ✅ fixed Task 890 | ✅ fixed Task 890 |
| Cross-renderer pixel consistency | ✅ Task 399 | ✅ Task 399 | ✅ Task 399 |

Legend: ✅ verified working · ❌ confirmed not implemented.

## Open, tracked follow-up work

Phase 45 opened 3 new tracked tasks:

- ~~**Task 890**~~ (opened by Task 391) — **fixed, 2026-07-11**: `DirectionalLight1`/
  `DirectionalLight2` now forward on `EnvironmentMapEffect` on all 3 renderers, mirroring
  `BasicEffect`'s own fix (Task 885/886). See `NEXT.md` §3 for the full write-up.
- ~~**Task 891**~~ (opened by Task 395) — **fixed**: the base cube-map lerp target (`envColor`) is
  now scaled by combined texture×diffuse alpha on all 3 renderers, the other half of the
  `envmap = ... * color.a` formula Task 395's specular-only fix didn't cover.
- **Task 892** (opened by Task 398) — fix `BasicEffect`'s Bgfx lit-textured normal transform,
  which transforms normals by the full `World×View×Projection` matrix — a worse sibling bug found
  while auditing `EnvironmentMapEffect`'s own normal-matrix bug, invisible in every existing
  `BasicEffect` Bgfx test since they all use `Identity` `View`/`Projection`.

This closes Phase 45 (`plans/plan_graphics.md` Tasks 391–400) in full.
