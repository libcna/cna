# Audit: examples/easygl_env_map_test.cpp

## Metadata

- Source file: `examples/easygl_env_map_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (Task 134, extended Task 192), `examples-tests-easygl`
  shard
- File type: C++ integration-test executable (`Game` subclass, `main()`), pixel-readback style,
  4 independent sub-tests in one `Draw()` call
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`,
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (`EnsureEnvMapped3DProgram`)
- FNA reference: `src/Graphics/Effect/StockEffects/HLSL/EnvironmentMapEffect.fx` (`PSEnvMap`/
  `PSEnvMapSpecular`, `ComputeFresnelFactor`)
- Main related tests: `easygl_environmentmapeffect_amount_zero_test.cpp` (Task 393),
  `easygl_environmentmapeffect_amount_one_test.cpp` (Task 394), and
  `easygl_environmentmapeffect_combined_test.cpp` (Task 399) — all in this same batch, all later
  in the task sequence than this file (Task 134/192) and collectively supersede this file's own
  understanding of the shader formula (see F1).

## Purpose

Verifies four independent `EnvironmentMapEffect` pixel contributions in isolation:
(a) `EmissiveColor`, (b) a second `EmissiveColor` value (verifies the setter, not just a
default), (c) `EnvironmentMapSpecular`, (d) `EnvironmentMapAmount` with a solid-color cube map.
Each sub-test clears the framebuffer, draws a full-screen NDC quad with `World=View=Projection=
Identity`, and reads back the center pixel.

## Executive Verdict

**Needs attention** — all four sub-tests currently pass against the real shader (independently
re-derived below), but the file's own header comment documenting "Shader formula" (lines 4-7) is
stale: it describes the pre-fix, buggy formula
(`litRGB=(uEmissiveColor+uLight0Diffuse*NdotL)*uDiffuseColor.rgb`, additive env-map blend) rather
than the current, fixed shader (`litRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor`, lerp-based
env-map blend via `mix()`) that Tasks 393/394 (this same batch) explicitly document as having
replaced it. See F1.

## Checklist Results

### API / XNA / FNA parity
`EnvironmentMapEffect` properties used (`setDiffuseColorProperty`, `setAmbientLightColorProperty`,
`setTextureProperty`, `setEnvironmentMapProperty`, `setEmissiveColorProperty`,
`setEnvironmentMapAmountProperty`, `setEnvironmentMapSpecularProperty`, matrix setters) all match
FNA's public `EnvironmentMapEffect` surface.

### Behavioral correctness
Independently re-derived the actual current shader math (`EnsureEnvMapped3DProgram`,
`EasyGLGraphicsBackend.cpp` lines 3145-3270) for all four sub-tests, using
`EnvironmentMapEffect`'s real constructor defaults (`EnvironmentMapEffect.cpp` lines 38-43:
`EnvironmentMapAmount=1.0f`, `FresnelFactor=1.0f` ⇒ `fresnelEnabled_=true` by default) since
`setupBase()` (test lines 142-150) never resets `FresnelFactor`:
- **(a)/(b) EmissiveColor→color, envAmount=0**: `blendFactor = vFresnel = pow(...)*envMapAmount(0)
  = 0` regardless of the Fresnel term's value, so `mix(baseColor, envSample*alpha, 0) = baseColor
  = litRGB*texColor.rgb = EmissiveColor` (since `lightSum=0`, `DiffuseColor=(1,1,1)`,
  `texColor=white`). Matches the test's expected `kRed`/`kGreen`. **Correct, and the
  Fresnel-default-being-enabled doesn't matter here since it's multiplied by `envMapAmount=0`.**
- **(c) EnvironmentMapSpecular=(0,0,1), envAmount=0**: `blendFactor=0` (same reasoning), so
  `rgb = 0 + uEnvMapSpecular*envSample.a*combinedAlpha = (0,0,1)*1*1 = blue`. Matches `kBlue`.
  **Correct**, and additionally independent of `envSample.rgb` (any cube-map color) since only
  `envSample.a` (the cube's per-texel alpha, here `1.0` from an opaque `Color`) feeds the specular
  term — the test never sets a cube for this sub-test at all (reuses `whiteCube` from
  `setupBase()`, whose alpha is 1), consistent.
- **(d) EnvironmentMapAmount=1, blue cube**: this is where the stale header formula and the real
  (lerp) formula would visibly diverge in general — but in this specific test, `World=View=
  Projection=Identity` means `EyePosition = viewInverse.translation = (0,0,0)` and every fragment's
  `worldPos.xy` equals its own NDC-ish position with `z=0`, so `eyeVector = normalize(EyePosition -
  worldPos)` always has `z=0` for every fragment on the quad — hence `viewAngle = dot(eyeVector,
  worldNormal=(0,0,1)) = 0` identically across the entire quad (not just approximately at the
  center). With `FresnelFactor=1` (default, left enabled): `blendFactor =
  pow(max(1-|0|,0),1)*envMapAmount(1) = 1*1 = 1` — full replace. Since `litRGB*texColor=0`
  (EmissiveColor and lightSum both 0) here, `mix(0, blueCube*1, 1) = blue`, matching the test's
  expected `kBlue` — but this is a **lerp-formula result that happens to equal what the stale
  additive-formula comment (line 18-19) would also predict** (`0 + blueCube*1 + 0 = blue`),
  because `blendFactor` saturates to exactly `envMapAmount` in this specific degenerate geometry,
  not because the two formulas actually agree in general. See F1.

### Logic
`colourMatch()` (tol=40, line 50) is looser than the sibling `environmentmapeffect_*` files
(tol=20) — reasonable given this file uses a 200×200 backbuffer versus their 64×64, and doesn't
appear to compensate for any known extra source of error; not a defect, just an observed
inconsistency in tolerance choice across sibling files testing the same effect.

### Memory/resource lifetime
`whiteCube`/`blueCube` (lines 128-129) are `std::unique_ptr<TextureCube>` correctly scoped to the
whole `Draw()` call, outliving all four sub-tests that reference them via raw `.get()` pointers —
no dangling risk since the unique_ptrs are not reset/destroyed until `Draw()` returns (after
`Exit()`, line 214, which stops the game loop but doesn't itself destroy locals mid-function).

### C++ correctness
No issues found.

### Performance
N/A — single `Draw()` call, four cheap full-screen-quad draws.

### Thread safety
N/A.

### Architecture
Correctly limited to public XNA-facing API.

### Maintainability
See F1 — the header "Shader formula" documentation block needs updating to reflect the current
lerp-based, Fresnel-modulated blend and the additive-after-diffuse-multiply emissive composition
that superseded what's currently written there.

### Portability
N/A.

### Robustness
N/A.

### Testing
This file is itself a test. See Missing or Weak Tests / F2.

### Cross-file consistency
See F1 — this file's header comment is now inconsistent with `EasyGLGraphicsBackend.cpp`'s actual
`EnsureEnvMapped3DProgram()` shader source, and inconsistent with the formula documented (and
verified against source) in the later `easygl_environmentmapeffect_amount_one_test.cpp` (Task
394) and `easygl_environmentmapeffect_combined_test.cpp` (Task 399) files in this same batch.

## Detailed Findings

### F1 — Header comment's documented shader formula is stale relative to the current (fixed) shader source

- Severity: MEDIUM
- Confidence: HIGH
- Category: maintainability / documentation accuracy
- Location/symbol: file header comment lines 4-7:
  ```
  // Shader formula:
  //   litRGB   = (uEmissiveColor + uLight0Diffuse * NdotL) * uDiffuseColor.rgb
  //   rgb      = litRGB * texColor.rgb + envColor * uEnvMapAmount + uEnvMapSpecular
  ```
  versus the actual current shader (`EasyGLGraphicsBackend.cpp` lines 3229, 3236):
  ```cpp
  vec3 litRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;
  ...
  vec3 rgb=mix(baseColor,envSample.rgb*combinedAlpha,blendFactor)+uEnvMapSpecular*envSample.a*combinedAlpha;
  ```
- Evidence: the header's `litRGB` line describes the exact buggy multiplicative-emissive
  composition that `easygl_emissive_ambient_composition_test.cpp`'s header (same batch) documents
  as fixed in an "audit_net.md remediation, sixth round" pass, and the header's `rgb` line
  describes a plain additive env-map blend with no Fresnel term at all, whereas the real formula
  is a Fresnel-modulated `mix()` (lerp), per `easygl_environmentmapeffect_amount_zero_test.cpp`'s
  own header (Task 393) explicitly stating "this is an ADDITIVE blend, not FNA's real lerp" was
  true only *at the time Task 393 was written* — Task 394 (`easygl_environmentmapeffect_amount_one_test.cpp`,
  same batch) exists specifically to verify the fix to a real lerp, and the current source
  confirms that fix is in place.
- Why it matters: this file's four assertions all still pass (as re-derived above) only because
  each sub-test happens to sit at a point where the stale-documented formula and the real formula
  coincide (envAmount=0 for a/b/c; a saturated `blendFactor=1` for d, driven by an incidental
  always-perpendicular eye/normal geometry under `Identity` view). A maintainer trusting this
  file's header comment as ground truth (rather than the actual shader source) would carry forward
  an incorrect mental model of both the emissive composition and the env-map blend formula.
- FNA/XNA comparison: the real formula (`mix`-based blend, additive emissive) is the one that
  actually matches FNA's `PSEnvMap`/`PSEnvMapSpecular` (`lerp(color.rgb, envmap.rgb,
  pin.Specular.rgb)`); the header's documented formula does not.
- Related files: `easygl_environmentmapeffect_amount_zero_test.cpp`,
  `easygl_environmentmapeffect_amount_one_test.cpp`,
  `easygl_environmentmapeffect_combined_test.cpp`, `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`.
- Suggested future action (not implemented by this audit): update this file's header comment to
  match the current shader formula (additive emissive composition, Fresnel-modulated `mix()` env-map
  blend), or replace it with a short pointer to the Task 393/394/399 files' more current derivations.

### F2 — Sub-test (d)'s Fresnel-independence is coincidental (an identity-camera artifact), not a deliberate test design choice

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage / robustness
- Location/symbol: `setupBase()` (lines 142-150) never calls `setFresnelFactorProperty(0.0f)`,
  leaving `FresnelFactor` at the constructor default of `1.0f` (`fresnelEnabled_=true`) for all
  four sub-tests.
- Evidence: as derived above, sub-test (d)'s correctness currently depends on `viewAngle` being
  exactly `0` at every fragment — true only because `World=View=Projection=Identity` puts the
  eye position and every fragment's world position in the same `z=0` plane as the quad's own
  normal-perpendicular axis. Contrast with `easygl_emissive_ambient_composition_test.cpp` and
  `easygl_environmentmapeffect_amount_zero_test.cpp`/`_amount_one_test.cpp`, which are more careful
  about isolating one variable at a time (the amount_zero/amount_one files' own headers explicitly
  discuss Fresnel/blend-factor confounds).
- Why it matters: if this quad's geometry or camera were ever changed to something non-degenerate
  (as `easygl_environmentmapeffect_combined_test.cpp`'s own header explicitly does, deliberately,
  for its own different purpose), sub-test (d)'s assumption that `EnvironmentMapAmount` alone
  controls the blend would silently break, because the Fresnel term would then meaningfully
  attenuate `blendFactor` below `envMapAmount`.
- FNA/XNA comparison: N/A (test-robustness observation).
- Suggested future action (not implemented by this audit): either set `FresnelFactor=0` explicitly
  in `setupBase()` to make the intent unambiguous (matching what the sibling amount_zero/amount_one
  tests do), or add a one-line comment noting the reliance on this geometry's incidental
  `viewAngle=0` property.

## Cross-File Observations

- This file (Task 134/192) predates and is functionally superseded in documentation accuracy by
  three later files in this same batch (Tasks 393, 394, 399) that each explicitly discuss and
  correctly document the lerp-based blend formula this file's header comment still describes as
  additive.
- The `// Task 896 finding` comment about needing `RasterizerState::CullNone` (line 114-115)
  recurs verbatim across this entire batch of files — a consistent, deliberate convention.

## Missing or Weak Tests

- No sub-test isolates `EnvironmentMapAmount` at a value strictly between 0 and 1 with a
  non-saturated cube-map color (the amount_one_test.cpp file in this same batch does cover that
  gap for `Amount=1` specifically with a gray, non-saturating cube).
- No sub-test exercises the Fresnel term's actual attenuating effect (F2) — every sub-test either
  has `envMapAmount=0` (Fresnel irrelevant) or sits at the coincidental `viewAngle=0` saturation
  point.

## Positive Findings

- Clear separation of concerns across the four sub-tests (a)-(d), each isolating one property
  (`EmissiveColor` ×2, `EnvironmentMapSpecular`, `EnvironmentMapAmount`) — good test structure even
  though the underlying formula documentation has drifted from the implementation.
- Reuses a `makeSolidCube()` helper cleanly across sub-tests, avoiding duplicated cube-map setup
  code.

## Final Assessment

All four assertions in this file currently pass against the real (fixed) shader, but only because
each sub-test's specific parameter choice happens to sit at a point where the file's own
stale-documented (buggy, additive) formula and the actual current (fixed, lerp-based) formula
produce the same numeric result. The header comment should be updated to avoid misleading a future
reader who trusts it over the shader source; this is a documentation-accuracy problem, not a test
failure.
