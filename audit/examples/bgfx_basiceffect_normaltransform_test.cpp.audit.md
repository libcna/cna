# Audit: examples/bgfx_basiceffect_normaltransform_test.cpp

## Metadata

- Source file: `examples/bgfx_basiceffect_normaltransform_test.cpp`
- Audit status: AUDITED (static; Bgfx is not in the D-P4 opportunistic-build feasibility list for this
  sandbox — no `cmake-build*` directory exists here)
- Subsystem: `examples-tests-bgfx` shard — `BasicEffect` lit-textured normal-transform-under-a-real-camera
  pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_bgfx_test(cna_test_bgfx_basiceffect_normaltransform …)` / `cna_register_backend_test(NAME Bgfx_BasicEffect_NormalTransform …)`, `cmake/Tests/BgfxTests.cmake:359-362`)
- XNA/FNA relevance: direct — `BasicEffect`'s world-space lighting normal must be transformed by
  `WorldInverseTranspose`, never by the full `WorldViewProj`
- FNA reference: `HLSL/Lighting.fxh` (`ComputeCommonVSOutputWithLighting`: `worldNormal =
  normalize(mul(normal, WorldInverseTranspose))`, entirely independent of `View`/`Projection`)
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`ComputeNormalMatrix3x3()` lines 93-103, upload sites lines 2770-2773/3279-3281),
  `src/CNA/Internal/Backends/Bgfx/shaders/vs_lit_textured3d.sc` (line 26),
  `vs_lit_textured3d_vertexlit.sc` (lines 41-42)

## Purpose

Single-check regression test for a real, previously-shipped bug: `vs_lit_textured3d.sc` used to transform the
vertex normal by the full `u_wvp` matrix (`v_normal = normalize(mul(u_wvp, vec4(a_normal,0.0)).xyz)`), which
corrupts the normal's direction under **any** non-identity `View`/`Projection`, not merely non-uniform `World`
scale (a strictly broader bug than `EnvironmentMapEffect`'s analogous Task 398 finding, which only manifested
under non-uniform scale). The file deliberately keeps `World` at Identity — proving the bug (and its fix) is a
function of the camera alone, isolating it from any World-scale confound — and uses a real
`Matrix::CreateLookAt`/`Matrix::CreatePerspectiveFieldOfView` camera, the first Bgfx lit-textured test in this
codebase's history to do so (per the file's own header, every prior Bgfx lit-textured test used identity
View/Projection, which makes `WVP≈World` and completely masks this class of bug).

## Executive Verdict

**Healthy** — the fix (`u_normalMatrix`, a CPU-computed inverse-transpose of `World`'s upper-left 3×3) is
present, wired into both the per-pixel-lit and per-vertex-lit shader variants, and the single expected value
was independently re-derived by this audit and matches exactly. The file shares the same stale cull-state
documentation issue as its 7 siblings in this batch (F1), and — being the one file in this batch with only a
single check — has correspondingly thin coverage of the feature it names (see Missing/Weak Tests).

## Checklist Results

### API / XNA / FNA parity
`fx.setWorldProperty(Matrix::getIdentityProperty())` / `setViewProperty(Matrix::CreateLookAt(...))` /
`setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(...))` (lines 133-135) map directly to FNA's
`IEffectMatrices.World`/`View`/`Projection`. No non-XNA surface exercised.

### Behavioral correctness
Re-derived the expected pixel value by hand: `kAmbient=0.02`, `kMaterialDiffuse=0.4`, `kLightDiffuse=0.5`,
`kLightDirRaw=(0.5,0,-1)` normalized to `(0.4472,0,-0.8944)`, `kNormal=(0,0,1)` (World=Identity, so the
inverse-transpose of Identity is Identity — the normal is untouched regardless of whether the fix or the bug
is active, by construction of this specific scene using Identity `World`; the test isolates the *camera*
variable, not the *World-scale* variable, consistent with its own stated purpose). `NdotL = dot(-lightDir, N)
= 0.8944`. `diffuse = (ambient + NdotL*lightDiffuse) * materialDiffuse = (0.02 + 0.8944*0.5)*0.4 = 0.4672*0.4
= 0.18688`. `×255 = 47.65` → rounds to **48**, exactly matching `kExpected(48,48,48,255)`. Confirms the shader
and CPU-side `ComputeNormalMatrix3x3()` correctly leave the normal at `(0,0,1)` under a real (non-identity)
camera — the specific behavior this test exists to check — since if the old WVP-based bug were still active,
the normal actually rendered would be a different (camera-baked, non-unit-length-preserved) vector, producing
a visibly different `NdotL` and thus a different pixel value than 48.

### Logic
`ComputeNormalMatrix3x3()` (`BgfxGraphicsBackend.cpp` lines 93-103) computes the classic 3×3 adjugate/cofactor
inverse of `World`'s upper-left block, laid out row-major in the 9-float output array. Independently traced:
since `bgfx::UniformType::Mat3` uniforms are consumed column-major by the GLSL-family shader
(`mul(u_normalMatrix, a_normal)` in `vs_lit_textured3d.sc`), uploading `inv(World3x3)` in row-major order is
equivalent to uploading `transpose(inv(World3x3))` when read column-major — exactly the desired
`WorldInverseTranspose` formula, achieved via a layout inversion rather than an explicit transpose step. This
is a real, non-obvious correctness dependency between `ComputeNormalMatrix3x3()`'s output convention and how
bgfx interprets `Mat3` uniforms; it is not spelled out in the CPU-side comment (which only says "Normal matrix
= transpose(inverse(world3x3)), via the cofactor/det shortcut") but was verified correct for this scene (World
= Identity, where the transpose distinction is invisible) and is consistent with the analogous, longer-lived
`EnvironmentMapEffect` Task 398 fix this comment says it mirrors.

### C++ correctness
No UB; `ComputeNormalMatrix3x3` guards `det != 0.0f` before taking `1.0f/det` (line 99), avoiding a division
producing `inf`/`nan` propagated into the shader for a degenerate (zero-determinant) `World` — acceptable
degenerate-input handling (falls back to a zeroed matrix rather than `inf`/`nan`, which would otherwise
silently corrupt every subsequent lit draw's normal until a valid `World` is set again).

### Robustness
Single check, single scene — no boundary case (e.g. a `World` with actual non-uniform scale combined with the
camera, which would test the general formula rather than the Identity-World special case where the
inverse-transpose is trivially Identity regardless of correctness). See Missing/Weak Tests.

### Testing
This is a regression test for a real, specifically-identified bug (WVP-based normal corruption under a
camera), and its single check does correctly exercise "is the normal transform camera-independent" — but
because `World` is deliberately Identity, the test cannot distinguish "normal correctly computed via
inverse-transpose" from "normal accidentally correct because Identity's inverse-transpose is also Identity and
some unrelated code path happens to also leave it untouched under this specific camera." A combined
non-identity-`World` + non-identity-camera case (covered by `EnvironmentMapEffect`'s own Task 398 test suite,
per the file's cross-reference, but not by this file nor apparently by any other BasicEffect-specific Bgfx
test in this shard) would be a strictly stronger regression guard for exactly this class of bug.

### Cross-file consistency
The `u_normalMatrix` CPU computation and upload is shared, byte-identical code
(`ComputeNormalMatrix3x3(params.worldColMajor, ...)`) between the per-pixel-lit path (lines 2770-2773) and the
per-vertex-lit path (lines 3279-3281, used by default here since `PreferPerPixelLighting` is never set) —
confirmed both shader variants (`vs_lit_textured3d.sc` line 26, `vs_lit_textured3d_vertexlit.sc` lines 41-42)
consume it identically (`normalize(mul(u_normalMatrix, a_normal))`), so this fix genuinely covers whichever
lighting-evaluation path this scene's default dispatch selects, not just the historically-first one.

## Detailed Findings

### F1 — Header comment's cull-state "not fixed there or here" claim is stale (shared with 7 sibling files)

- Severity: MEDIUM
- Confidence: HIGH
- Category: documentation-accuracy / stale-comment
- Location/symbol: header comment lines 25-28 (`"tracked as Task 896, not fixed there or here"`)
- Evidence: identical to the finding recorded for `bgfx_basiceffect_multilight_emissive_test.cpp.audit.md` —
  `b6a00bc6 fix(Task 896): push GraphicsDevice's real default RasterizerState to all 3 backends` is confirmed
  (`git merge-base --is-ancestor b6a00bc6 HEAD`) to be an ancestor of the current checkout, and
  `GraphicsDevice.cpp` line 207 confirms the fix (`setRasterizerStateProperty(rasterizerState_)` in the
  constructor) is live, pushing FNA's real `CullCounterClockwiseFace` default to all three backends uniformly.
  This file's own last content change is commit `cd63bac6` (Jul 7 11:14), predating `b6a00bc6` (Jul 7 19:39).
- Why it matters: the comment's causal claim ("Bgfx is the only one of the 3 backends that actually matches
  FNA's real default") is no longer accurate; the explicit `RasterizerState::CullNone` workaround the test
  still performs (line 152) remains correct and necessary, but for a different, now-shared-across-all-backends
  reason than the comment states.
- FNA/XNA comparison: N/A (documentation-accuracy, not a behavior question).
- Related files: same finding recorded once per file across all 8 files in this batch.
- Suggested future action (not implemented by this audit): refresh the comment to note Task 896 closed the gap.

## Cross-File Observations

- Shares `ComputeNormalMatrix3x3()` and its upload sites with `bgfx_basiceffect_specular_test.cpp` and
  `bgfx_basiceffect_preferperpixellighting_test.cpp` (both also use a real camera and lit-textured shading);
  all three were cross-checked for consistent normal-matrix handling and found consistent.
- This file's header comment is the most detailed and self-aware of the 8 in this batch about *why* the test
  scene is shaped the way it is (Identity `World` isolating the camera variable) — a genuinely useful piece of
  test-design documentation, undermined only by the stale cull-state claim (F1) shared with its siblings.

## Missing or Weak Tests

- No test in this shard combines a non-identity `World` (with non-uniform scale) **and** a non-identity
  camera in the same scene for `BasicEffect`'s lit-textured path — the general case the inverse-transpose
  formula exists to handle. This file's Identity-`World` design is a deliberate, well-reasoned isolation of
  one variable (per its own comment), but leaves the fully-general case's Bgfx-specific correctness resting on
  code-sharing with `EnvironmentMapEffect`'s own (differently-scened) Task 398 test rather than a direct
  `BasicEffect`-specific check. Not a defect in this file, but a coverage gap worth flagging.

## Positive Findings

- The single expected pixel value was independently re-derived from FNA's actual lighting formula and matches
  exactly (48 vs. hand-computed 47.65, correct standard rounding).
- The test's own header comment is unusually precise about what variable it isolates (camera, not World
  scale) and why (masking risk from every prior Bgfx lit-textured test using identity View/Projection) —
  genuinely load-bearing documentation, not boilerplate.
- The CPU-side inverse-transpose computation was independently verified correct (including the non-obvious
  row-major-upload-equals-column-major-transpose detail), not merely assumed correct because the comment
  says so.

## Final Assessment

A correct, well-targeted regression test for a genuinely serious class of bug (camera-corrupted lighting
normals), let down only by the shared stale cull-state comment (F1) and a narrower-than-ideal scene (Identity
`World` only) that leaves the fully general non-uniform-scale-plus-camera case unverified for `BasicEffect`
specifically.
