# Audit: examples/bgfx_environmentmapeffect_worldtransform_test.cpp

## Metadata

- Source file: `examples/bgfx_environmentmapeffect_worldtransform_test.cpp` (174 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `EnvironmentMapEffect`'s normal transform under a
  non-uniform-scale `World` matrix, Bgfx backend, Task 398.
- CTest registration: `cna_bgfx_test(cna_test_bgfx_environmentmapeffect_worldtransform …)` /
  `cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_WorldTransform …)`
  (`cmake/Tests/BgfxTests.cmake:225-227`).
- XNA/FNA relevance: direct — `IEffectMatrices.World` interacting with lighting/reflection normals.
- FNA reference: `HLSL/EnvironmentMapEffect.fx`'s `ComputeEnvMapVSOutput`: `worldNormal =
  normalize(mul(vin.Normal, WorldInverseTranspose))` — the inverse-transpose, not a direct `World`
  multiply, is the mathematically correct normal transform under non-uniform scale.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
  (`worldInverseTransposeParam_` computed via `Matrix::Invert`+`Matrix::Transpose`, lines 330-344 —
  present but, per the header comment, not what this GPU path actually consumes),
  `src/CNA/Internal/Backends/Bgfx/shaders/vs_env_map3d.sc:19-23` (`v_normal = mul(u_normalMatrix,
  a_normal)`, fed by `BgfxGraphicsBackend.cpp`'s `ComputeNormalMatrix3x3(params.worldColMajor,
  normalMatrix)` at line 2640-2642).

## Purpose

Single-check pixel test proving the vertex normal is transformed by the correct
inverse-transpose of `World`'s upper-left 3×3, not by `World` directly — the real bug this task
found and fixed (header comment lines 2-9): `vs_env_map3d.sc` previously computed `mul(u_world,
vec4(a_normal,0.0))` directly, which is only correct under rotation/uniform-scale/translation and
skews the normal under non-uniform scale ("same shape as EasyGL's bug", per the comment). `World =
Matrix::CreateScale(1,1,20)` (line 131) stretches only the `Z` axis by `20×`; since the quad's
vertex `Z` is itself `0`, the *position* is unaffected (multiplying `0` by any scale is still `0`),
isolating the defect to the *normal* alone.

## Executive Verdict

**Healthy** — the single expected face was independently re-derived via both the correct
(inverse-transpose) and the buggy (direct-multiply) normal transforms, confirming the two
hypotheses predict genuinely different (and correctly distinguishable) cube faces, and the
production formula matches the correct one exactly.

## Checklist Results

### Behavioral correctness — full independent re-derivation
Vertex normal `n=(0, 0.70710678, 0.70710678)` (line 114, a valid 45° unit vector,
`0.7071²+0.7071²=1.0`). `World=diag(1,1,20)`.
- **Correct (inverse-transpose) transform**: for a diagonal matrix, `inverse=diag(1,1,1/20)` and
  `transpose` of a diagonal matrix is itself, so `WorldInverseTranspose=diag(1,1,0.05)`. Applied to
  `n`: `(0·1, 0.7071·1, 0.7071·0.05) = (0, 0.7071, 0.035355)`, normalized (magnitude
  `√(0.5+0.00125)=0.70796`) → `N≈(0, 0.99875, 0.049938)` — nearly straight `+Y` with a small `+Z`
  lean.
  With `eye=(0,0,3)` (line 132) and the sampled fragment at the quad's approximate world center
  `(0,0,0)`: `E≈(0,0,1)`. `reflect(-E,N) = -E - 2·dot(-E,N)·N`. `dot(-E,N)=dot((0,0,-1),
  (0,0.99875,0.049938))=-0.049938`. `reflect = (0,0,-1) + 0.099876·(0,0.99875,0.049938) =
  (0, 0.09975, -0.995012)` — dominant component **`-Z`** → `NegativeZ` face → **yellow**
  `(255,255,0)` per `makeDistinctCube()`'s `negZ(255,255,0,255)` (line 100). Matches the expected
  `Color(255,255,0,255)` (line 150-152) exactly.
- **Buggy (direct-`World`-multiply) transform**, computed independently by this audit to confirm
  the test genuinely discriminates the fix: `mul(World, n) = (0·1, 0.7071·1, 0.7071·20) =
  (0, 0.7071, 14.142)`, normalized → `≈(0, 0.05, 0.9987)` — dominant component **`+Z`** (the exact
  opposite axis from the correct transform). `reflect(-E,N)` with `N≈(0,0,1)` and `E≈(0,0,1)` gives
  back `≈(0,0,1)` → `PositiveZ` face → **blue** `(0,0,255)` — a *different, clearly distinguishable*
  expected color from the correct case's yellow.

Since the buggy and correct implementations predict opposite-axis-dominant (and hence
differently-colored, per `makeDistinctCube()`'s all-distinct-colors fixture) reflection directions,
this single check is a strong, genuine regression guard for the Task 398 fix — not merely a
plausible-looking constant.

### Cross-file consistency
`BgfxGraphicsBackend.cpp`'s env-map draw path (line 2636-2694) calls
`ComputeNormalMatrix3x3(params.worldColMajor, normalMatrix)` and uploads it to
`normalMatrix3DUnif_` (lines 2640-2642), matching `vs_env_map3d.sc`'s `uniform mat3 u_normalMatrix`
consumption (line 8, 23) — the C++/shader interface for this fix is correctly wired end-to-end,
independently confirmed by reading both sides rather than assuming the header comment's claim.

## Detailed Findings

### F1 — Same `DiffuseColor`/`EmissiveColor` recombination defect as this batch's other `EnvironmentMapEffect` files; masked here identically

- Severity: HIGH
- Confidence: HIGH
- Category: fna-parity (production code exercised by, but not exposed by, this test)
- Location/symbol: `src/CNA/Internal/Backends/Bgfx/shaders/fs_env_map3d.sc:28`; same defect
  reproduced in `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.frag.glsl:39`. Full derivation
  in this batch's `bgfx_environmentmapeffect_eyeposition_test.cpp.audit.md` (F1).
- This file's own coverage: `EmissiveColor=(0,0,0)` (line 127) and `DiffuseColor` is never set away
  from its default `(1,1,1,1)` — both are effectively no-ops for this specific defect either way
  (emissive itself is zero, and `diffuseColor=1` is a no-op multiplier), so this test cannot detect
  it regardless.
- Suggested future action (not implemented by this audit): see the `eyeposition` report's F1 for the
  concrete shader fix.

## Cross-File Observations

- Per Task 364/884 (comment lines 10-14), the standard quad winding needs
  `RasterizerState::CullNone` (line 142) — independently re-verified against
  `RasterizerState.cpp:11`/`BgfxGraphicsBackend.cpp:1781-1782` in this batch's `eyeposition` report.
- Shares `makeDistinctCube()` (lines 86-102) verbatim with the `eyeposition` sibling in this batch —
  the same six-distinct-color fixture technique, here used to distinguish normal-transform
  correctness rather than eye-position sensitivity.
- `FresnelFactor=0` (line 130) and `EnvironmentMapAmount=1` (line 128) reproduce this batch's
  `eyeposition` file's exact "blend factor is flat 1, rendered pixel is exactly the cube-map texel"
  simplification, keeping the single assertion here purely a test of reflection *direction*, with no
  entanglement from the Fresnel/lit-texture blend math.

## Missing or Weak Tests

- See F1.
- Single-check file: only one non-uniform-scale axis/magnitude combination (`Z×20`, all other axes
  unscaled) is exercised. A second check with a *different* non-uniform axis (e.g. `X×20, Y×1,
  Z×1`) would strengthen confidence that the fix generalizes beyond this one specific scale pattern,
  though the current derivation (via the general cofactor/inverse-transpose formula, not a
  special-cased Z-axis shortcut per the header comment's own description of the fix) makes this a
  low-priority gap rather than a live concern.

## Positive Findings

- The single check is unusually strong for a one-assertion file: this audit independently confirmed
  the buggy and correct implementations predict *opposite-dominant-axis* (and hence
  clearly-distinguishable-by-fixture-color) results, rather than merely "close" values that might
  pass under a generous tolerance either way.
- Choosing a scale value that leaves vertex position unaffected (`Z=0` scaled by `20` is still `0`)
  is a precise, deliberate way to isolate the normal-transform defect from any position/reflection-
  vector confound.

## Final Assessment

A precise, single-check test whose expected value was independently confirmed (via both hypotheses)
to be the correct discriminator for the Task 398 normal-transform fix, with the C++/shader uniform
wiring for that fix verified end-to-end. This audit's review of the shared shader surfaced the same
untracked `DiffuseColor`/`EmissiveColor` recombination bug (F1) documented across this batch,
irrelevant to (and undetectable by) this file's own all-zero-emissive scene.
