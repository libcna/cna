# Audit: examples/bgfx_environmentmapeffect_fresnel_test.cpp

## Metadata

- Source file: `examples/bgfx_environmentmapeffect_fresnel_test.cpp` (184 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `EnvironmentMapEffect.FresnelFactor` edge-weighting,
  Bgfx backend, Task 396.
- CTest registration: `cna_bgfx_test(cna_test_bgfx_environmentmapeffect_fresnel …)` /
  `cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_Fresnel …)`
  (`cmake/Tests/BgfxTests.cmake:213-215`).
- XNA/FNA relevance: direct — `EnvironmentMapEffect.FresnelFactor`.
- FNA reference: `HLSL/EnvironmentMapEffect.fx`'s `ComputeFresnelFactor`: `pow(max(1 -
  abs(dot(eyeVector, worldNormal)), 0), FresnelFactor) * EnvironmentMapAmount`.
- Related production code: `src/CNA/Internal/Backends/Bgfx/shaders/fs_env_map3d.sc` (`blendFactor
  = (u_envMapAmount.y > 0.5) ? pow(max(1-abs(viewAngle),0), u_envMapSpecular.w) *
  u_envMapAmount.x : u_envMapAmount.x`), `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`params.fresnelEnabled ? 1.0f : 0.0f` packed into `u_envMapAmount.y`, `params.fresnelFactor`
  packed into `u_envMapSpecular.w`, line 2665-2669).

## Purpose

Two-check pixel test proving the Fresnel edge-weighting term is genuinely
view-angle-dependent, matching FNA's real per-vertex `pow(max(1-abs(dot(eyeVector,worldNormal)),0),
FresnelFactor)*EnvironmentMapAmount` formula. Per the header comment (lines 2-8), the real gap this
task found and fixed was that CNA previously implemented **no** Fresnel uniform at all in any of
the 3 backends — the blend factor was always the flat `EnvironmentMapAmount` regardless of view
angle. Check (a) uses a degenerate (grazing, `viewAngle≈0`) camera where Fresnel's own value
coincides with the flat-`Amount` case (self-labelled "not discriminating"); check (b) uses a
head-on camera (`viewAngle≈1`) where Fresnel suppresses the cube map almost entirely — the
actually-discriminating check.

## Executive Verdict

**Healthy** — both expected constants were independently re-derived from the current production
formula and match exactly (check (b)'s values are exact integers, not tolerance-assisted).

## Checklist Results

### Behavioral correctness — full independent re-derivation
`World=Identity` (both checks); `emissiveColor_=(0.5,0.5,0.5)` (line 105), `DiffuseColor` left at
its default `Vector3::One`, `DirectionalLight0`'s default zero diffuse (`DirectionalLight.cpp:7`):
`litRGB = (0.5,0.5,0.5)*(1,1,1) = (0.5,0.5,0.5)`. `texColor = (200,100,50,255)/255 ≈
(0.7843,0.3922,0.1961,1)` (line 134, 137-138). `baseColor = litRGB*texColor ≈
(0.3922,0.1961,0.0980)` → exactly `(100,50,25)` in bytes (`200*0.5=100`, `100*0.5=50`, `50*0.5=25`
— exact integer arithmetic, not rounded).
- Check (a): `View=Projection=Identity` (line 152) → same "eye embedded in the quad's own plane"
  geometry independently re-derived in this batch's `eyeposition`/`amount_one` sibling reports:
  `eyeDir.z ≡ 0` exactly (both `eyePos.z` and `worldPos.z` are `0` under Identity `View`/`World`),
  so `dot(E,N)=0` at the (non-degenerate, off-true-center) sampled pixel. `blendFactor =
  pow(max(1-|0|,0), FresnelFactor=1.0-default) * EnvironmentMapAmount(1) = pow(1,1)*1 = 1` — i.e.
  Fresnel's own value is *maximal* here, numerically identical to the flat `Amount=1` case, matching
  the comment's own "reduces to flat Amount (not discriminating)" label (line 154) precisely.
  `rgb = mix(baseColor,(0.502,0.502,0.502),1) = grayCube = (128,128,128)`. Matches
  `Color(128,128,128,255)` (line 153-155) exactly.
- Check (b): `View=CreateLookAt((0,0,3),Zero,(0,1,0))`, `Projection=CreatePerspectiveFieldOfView(...)`
  (lines 157-158) — a head-on camera looking straight down `-Z` at the quad, whose normal is
  `(0,0,1)`: `E≈N≈(0,0,1)`, `viewAngle=dot(E,N)≈1`. `blendFactor = pow(max(1-|1|,0),1)*1 =
  pow(0,1)*1 = 0` — Fresnel fully suppresses the cube map. `rgb = mix(baseColor,
  cubeColor, 0) = baseColor = (100,50,25)`. Matches `Color(100,50,25,255)` (line 159-162) exactly —
  and these are the *same exact* integer values independently derived above from first principles,
  not merely internally self-consistent.

Check (b) genuinely discriminates a "Fresnel not implemented" regression: without the fix, the
`blendFactor` would stay at the flat `Amount=1`, producing gray `(128,128,128)` instead of the
correct `(100,50,25)` — clearly distinct values, so this assertion would fail as intended.

### Logic
`makeSolidCube()` (lines 85-96) applies a single flat gray to all six faces — the correct minimal
fixture for isolating the *blend factor* itself (as opposed to "which face" tests like the sibling
`eyeposition`/`worldtransform` files, which instead use `makeDistinctCube()` to isolate *direction*).

### Testing
Two checks, one honestly self-labelled non-discriminating (a) and one that is the real
discriminator (b) — an appropriately minimal design for a single-property boolean-style test
(same "one weak, one strong, both self-aware" pattern already documented for the sibling EasyGL
`basiceffect_specular` report in this audit).

## Detailed Findings

### F1 — Same `DiffuseColor`/`EmissiveColor` recombination defect as this batch's other `EnvironmentMapEffect` files; masked here identically

- Severity: HIGH
- Confidence: HIGH
- Category: fna-parity (production code exercised by, but not exposed by, this test)
- Location/symbol: `src/CNA/Internal/Backends/Bgfx/shaders/fs_env_map3d.sc:28` — `litRGB =
  (u_emissiveColor.xyz + lightSum) * u_diffuseColor.xyz` should be `lightSum * u_diffuseColor.xyz +
  u_emissiveColor.xyz` per `HLSL/Lighting.fxh`'s `ComputeLights()`; the identical shape is
  reproduced in `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.frag.glsl:39`. Full derivation
  in this batch's `bgfx_environmentmapeffect_eyeposition_test.cpp.audit.md` (F1).
- This file's own coverage: `EmissiveColor=(0.5,0.5,0.5)` (line 105) is non-zero, but
  `DiffuseColor` is never set away from its default `(1,1,1,1)` — since `(emissive+lightSum)*1 ≡
  lightSum*1+emissive*1`, the two formulas coincide numerically for every value this test exercises,
  masking the defect.
- Suggested future action (not implemented by this audit): see the `eyeposition` report's F1 for the
  concrete shader fix.

## Cross-File Observations

- Per Task 364/884 (comment lines 9-13), the standard quad winding requires
  `RasterizerState::CullNone` (line 119) under Bgfx's real `CullCounterClockwiseFace` default —
  independently re-verified against `RasterizerState.cpp:11`/`BgfxGraphicsBackend.cpp:1781-1782` in
  this batch's `eyeposition` report and consistent here.
- Shares the "Identity-view puts the eye vector perpendicular to the normal" geometry with this
  batch's `eyeposition`/`fog`/`specular`/`amount_one` siblings — check (a) here is architecturally
  the same degenerate-but-well-defined configuration already independently confirmed non-degenerate
  in the `amount_one` sibling report (screen-center pixel-sampling offset avoids the true
  `eyeDir=(0,0,0)` singularity).

## Missing or Weak Tests

- See F1.
- No explicit assertion that check (a) and check (b) differ from each other (relying instead on two
  independently hardcoded, visibly-distinct expected constants); not a real gap since both expected
  values are non-trivial and far apart (`(128,128,128)` vs `(100,50,25)`), but a more defensive test
  could add `!colourMatch(a,b)` as an extra explicit statement of intent.

## Positive Findings

- Check (b)'s expected constants are exact (not merely within a tolerance band) re-derivations of
  the real production formula from first principles.
- The self-labelled "(not discriminating)" comment on check (a) is accurate and, per this shard's
  established pattern, a genuinely useful transparency signal for future maintainers.

## Final Assessment

A precise, correctly-designed two-check Fresnel test; both constants check out exactly against the
current production formula. This audit's independent review of the shared `EnvironmentMapEffect`
shader surfaced the same untracked `DiffuseColor`/`EmissiveColor` recombination bug (F1) documented
across this batch, which this file's scene does not (and by its own stated purpose need not)
exercise.
