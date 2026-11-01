# EnvironmentMapEffect exactness support matrix

Phase 45 (graphics Tasks 391–400) audited and pixel-verified `EnvironmentMapEffect`
conformance against FNA. This document records those findings and the later fixes that closed
the phase's follow-up work. Current-state reconciliation: 2026-10-31.

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
new Task 890 rather than folded into Task 885, since it is a distinct effect with its own
dispatch. Task 890 subsequently added all three lights and a discriminating regression.

Task 391 found **zero existing test coverage** for `EnvironmentMapEffect`. Task 392 wrote
`modules/graphics/tests/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffectTests.cpp` from
scratch (mirroring Task 372/382's precedent). The current suite covers all 14 property defaults,
`LightingEnabled`'s throw-on-`false`/no-throw-on-`true` behavior, `EnableDefaultLighting()`'s
ambient/light-enable effects, setter round-trips, ownership, `Clone()` and `GetTypeName()`.
**The original `Clone()` test caught a real, previously-undiscovered bug affecting 4 stock
effects**: `Clone()` never preserved `FogColor` on
`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect` —
`CacheEffectParameters()` re-links `fogColorParam_` to a fresh, zero-valued parameter in the
clone, and nothing copied the source's actual value across. Fixed all 4 with a one-line
`fogColorParam_->SetValue(src.getFogColorProperty())` addition each; extended Task 372/382's own
`Clone()` tests to close the gap that let it go undetected.

## 2. Cube-map blend formula (Tasks 393–394)

Task 393 verified `EnvironmentMapAmount=0` correctly ignores the cube map —
**zero bugs in its own scope** — but surfaced a real formula-level discrepancy: FNA's real
`PSEnvMap` pixel shader **lerps** between the lit/textured color and the cube map
(`color.rgb = lerp(color.rgb, envmap.rgb, EnvironmentMapAmount)`), while CNA's actual shader
formula **added** the cube map on top instead
(`rgb = litRGB×texColor.rgb + envColor×Amount + specular`). Both formulas coincide exactly at
`Amount=0` (why Task 393's own scope couldn't catch it) but diverge sharply at `Amount=1`.

**Task 394 confirmed and FIXED this real formula bug.** A literal "white
cubemap" sub-case (matching the task's own title) couldn't discriminate the two formulas — both
saturate to `(255,255,255)` identically, the same 0/1-saturation blind spot Task 383 already
taught this project to watch for. A second, deliberately non-saturated **gray cubemap**
`(128,128,128)` sub-case, combined with a genuinely nonzero lit/textured contribution, cleanly
discriminated them: FNA's lerp predicts `(128,128,128)` (fully replaced); CNA's additive formula
predicted `(228,178,153)` (added on top) — empirically confirmed as CNA's exact pre-fix output.
**Fixed** by changing the additive term to a proper `mix()`
(`vec3 baseColor=litRGB*texColor.rgb; vec3 rgb=mix(baseColor,envColor,Amount)+specular;`).

## 3. `EnvironmentMapSpecular` alpha scaling (Task 395)

**Real, confirmed formula bug found and fixed.** FNA's real `PSEnvMapSpecular`
pixel shader scales the specular term by the cube map's own **alpha** channel, further scaled by
the combined texture×diffuse alpha (`envmap = SAMPLE_CUBEMAP(...) * color.a;
color.rgb += EnvironmentMapSpecular * envmap.a`). CNA's actual shader formula added
`EnvironmentMapSpecular` as a **flat, unscaled constant**, never reading the
cube map's alpha channel at all. Isolated the test with `EnvironmentMapAmount=0` (removing the
base-lerp term from the picture) and opaque texture/diffuse (`combinedAlpha=1`, isolating purely
to the cube map's own alpha channel): an opaque cube map (`alpha=255`) is not discriminating
(scaling by 1.0 changes nothing); a translucent cube map (`alpha=128`) is — FNA predicts
`(151,101,76)`, CNA's flat-additive formula predicts `(202,152,127)`, empirically confirmed as the
exact pre-fix output. **Fixed** by sampling the cube map's full `vec4` (not just
`.rgb`/`.xyz`) and scaling the specular term by `envSample.a × combinedAlpha`. Task 395
deliberately deferred the other half of the same formula to **Task 891**: the base lerp's
`envColor` target also needs scaling by combined texture×diffuse alpha because FNA's
`envmap = SAMPLE_CUBEMAP(...) * color.a` scales both `envmap.rgb` and `envmap.a`. Task 891 later
fixed that half too. The shared EasyGL/SDLGPU regression uses `Alpha=0.5`, where an unscaled cube
sample produces twice the expected RGB and cannot pass accidentally.

## 4. Fresnel edge-weighting (Task 396)

**Real, confirmed missing-feature gap found and fixed.** Tasks 393/394 already
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
`FillGpuDrawParams()`) and threading a per-pixel `viewAngle`/`pow()` term into the
fragment shaders. That initial per-fragment placement was not exact: XNA/FNA computes the scalar
per vertex and the rasterizer interpolates the result. Task 1112 subsequently moved the Fresnel
calculation to the vertex stage. Its varying-normal gradient regression distinguishes the two
orders and runs on both EasyGL and SDLGPU.

## 5. `EyePosition` and reflection-vector correctness (Task 397)

**Verify-only, zero bugs found, no code changed.** Every prior env-map test in this phase used
solid-color cube maps, unable to detect a wrong reflection vector at all (every face samples
identically regardless of which face is actually hit). Built a cube map with a **distinct color
per face** and 2 camera positions via `Matrix::CreateLookAt` with a fixed origin target (so the
quad's centre always projects to screen centre regardless of how oblique the eye is): a
straight-on eye hits the `PositiveZ` face exactly; an off-axis eye hits the `NegativeX` face by a
~10× dominant-component margin — 2 clearly different, exactly-predicted faces, proof by
construction that `EyePosition` wiring works correctly end-to-end.

## 6. `World` transform / normal-matrix correctness (Task 398)

**Real, confirmed formula bug found and fixed on EasyGL.** XNA content ships
`WorldInverseTranspose` for exactly this reason: transforming a normal by `World` directly is only
correct for pure rotation/uniform-scale/translation; under **non-uniform scale** the correct
transform is `transpose(inverse(World3x3))`. Auditing found: **EasyGL** computed the "normal
matrix" as `World`'s raw upper-left 3x3 on the CPU side (shared infra also used by `BasicEffect`'s
lit-textured pipeline) — WRONG. Notably,
`EnvironmentMapEffect::OnApply()` already computes the correct `WorldInverseTranspose` for its own
`EffectParameter` (API/content-pipeline fidelity) — but this value was never forwarded into the
actual GPU dispatch on the buggy renderer. A synthetic, non-axis-aligned test normal combined
with a large non-uniform `World` scale produced reflection vectors with opposite dominant-face
results between the correct and buggy formulas, empirically confirmed as the exact pre-fix output.
**Fixed** via the standard cofactor/determinant shortcut
(`normalMatrix = cofactor(World3x3)/det(World3x3)`) computed on the CPU side.

## 7. Integration capstone (Task 399)

Closed the pixel-verification work with a capstone test combining everything Tasks 394–398
verified individually — the lerp blend, alpha-scaled specular, Fresnel suppression,
`EyePosition`-driven reflection, and a non-uniform-scale `World` — to prove the fixes compose
correctly together, not just in isolation. Deliberately chose parameters so Fresnel-suppression
(not a hardcoded `Amount=0`) isolates the env-map contribution to almost exactly the specular
term, landing on the same easily-verified numeric target Task 395 derived (`(151,101,76)`) via a
genuinely different code path.

**Result: the exact predicted `(151,101,76)` on the first attempt.** No new bugs found — pure integration verification; a clean pass on
the first attempt is itself a meaningful confirmation that Tasks 394–398's fixes don't just work
in narrow isolation but compose correctly when combined in one scene.

## 8. `EnvironmentMapAmount` above 1 is saturated (2026-09-07)

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
`SdlGpu_EnvironmentMapEffect_AmountClamp`. The source is renderer-agnostic and its legs are
relational, so registering it for another renderer is one line in that family's `CMakeLists.txt`.

**The unlit sibling is the same rule one effect over** (`vout.Diffuse : COLOR0`). See §6 of
`docs/basiceffect-support.md` — including the colour-gradient case, where the two orders are 77
levels apart. Both clamp programs also run on EasyGL (`EasyGL_CrossFamily_*`) and pass there.

## Support matrix

| Feature | EasyGL |
|---|---|
| Property defaults (14/14) | ✅ Task 391/392 |
| `Clone()` preserves `FogColor` | ✅ fixed Task 392 |
| Cube-map blend: `lerp` not additive | ✅ fixed Task 394 |
| `EnvironmentMapSpecular × envmap.a` | ✅ fixed Task 395 |
| Base lerp `envColor × combinedAlpha` | ✅ fixed Task 891 |
| Fresnel edge-weighting and per-vertex interpolation | ✅ fixed Tasks 396/1112 |
| `EyePosition` → reflection vector | ✅ Task 397 |
| `World` non-uniform-scale normal transform | ✅ fixed Task 398 |
| `DirectionalLight1`/`DirectionalLight2` | ✅ fixed Task 890 (2026-07-11) |
| Fog forwarding and view-space factor | ✅ fixed Tasks 900/1111 |
| `EnvironmentMapAmount > 1` vertex saturation | ✅ measured and regressed |
| Combined integration capstone | ✅ Task 399 |

Legend: ✅ verified working.

## Subsequent fixes

- **Task 890** (opened by Task 391, fixed 2026-07-11) — `DirectionalLight1` and
  `DirectionalLight2` now reach the renderer alongside light 0. The EasyGL regression varies
  each light independently; SDLGPU reuses the same program through the cross-family corpus.
- **Task 891** (opened by Task 395, fixed 2026-07-11) — the base cube-map lerp target is now
  `envSample.rgb × combinedAlpha`, completing the other half of FNA's
  `envmap = SAMPLE_CUBEMAP(...) * color.a` rule. The shared regression is
  `modules/graphics/examples/environmentmapeffect_alphascaledlerp_test.cpp`.
- **Tasks 900/1111** — fog fields now reach the renderer and use FNA's view-space dot-product
  factor, including the `FogStart == FogEnd` case. EasyGL and SDLGPU run the same focused fog
  program; the renderer-wide fog contract has separate shared coverage.
- **Task 1112** — replaced the original per-fragment Fresnel calculation with XNA/FNA's
  per-vertex calculation and interpolated scalar. The gradient test fails on the old ordering.

The dated fog forwarding/factor checks do not qualify every alpha combination. SDLGPU's current
stock fragment mixes toward unscaled FogColor instead of FogColor times the completed fragment
alpha. That source-verified defect is documented in
[sdl-gpu-renderer.md](sdl-gpu-renderer.md#parity-with-easygl) and `handoff.md` §5.1.

Phase 45 and all follow-up work named on this page are closed. Current renderer-wide capability
status belongs in `graphics-renderer-feature-matrix.md`; unresolved defects belong in
`handoff.md` section 5.
