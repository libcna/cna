# Audit: examples/webgpu_envmap3d_test.cpp

## Metadata

- Source file: `examples/webgpu_envmap3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `EnvironmentMapEffect` cube-map reflection pixel test,
  WebGPU backend (experimental, per `CLAUDE.md`).
- Test executable: `cna_test_webgpu_envmap3d`, CTest target `WebGPU_EnvMap3D`
  (`cmake/Tests/WebGpuTests.cmake:88-89`).
- XNA/FNA relevance: direct — `EnvironmentMapEffect.EnvironmentMap`/`EnvironmentMapAmount`/
  `FresnelFactor`, `TextureCube`.
- FNA reference: `HLSL/EnvironmentMapEffect.fx` (`PSEnvMap`/`ComputeFresnelFactor`),
  `HLSL/Lighting.fxh` (`ComputeLights`: `result.Diffuse = mul(diffuse, lightDiffuse) *
  DiffuseColor.rgb + EmissiveColor;` — emissive is **added**, not re-multiplied by DiffuseColor),
  `StockEffects/EffectHelpers.cs` (`SetMaterialColor`: documents the CPU-side "fold
  `ambient*diffuse` into the emissive parameter" optimization this project's own
  `EnvironmentMapEffect.cpp` replicates).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
  (`FillGpuDrawParams()` lines 397-460, notably the `p.emissiveColor` comment at line 421: "emissive +
  ambient * diffuse, pre-combined and pre-multiplied by alpha (matches FNA)"),
  `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp` (`CreateEnvMapResources()` lines
  3834-3987, fragment shader lines 3905-3929, `FillEnvMapParams()` lines 482-... , `QueueEnvMapDraw()`
  lines 4085-4142).

## Purpose

Four-check pixel test using hand-derived geometry (`View=World=Projection=Identity`, a camera-facing
quad, a 6-solid-colour cube map) to prove `EnvironmentMapEffect`'s reflection sampling and blend gating
genuinely run on this backend: (A) `FresnelFactor=0`/`EnvironmentMapAmount=1` samples the correct cube
face (cyan, the hand-derived `NegativeZ` reflection direction) via the flat-blend path; (B) the same
scene with `FresnelFactor=1` (the real XNA default) renders black — a head-on viewing angle
(`dot(E,N)=1`) collapses the Fresnel weighting term to exactly `0`, fully suppressing the env-map
contribution; (C) `EnvironmentMapAmount=0` (Fresnel disabled) also renders black, independently proving
`EnvironmentMapAmount` gates the blend; (D) the `DrawIndexedPrimitivesEx` counterpart of check A.

## Executive Verdict

**Significant correctness risk** — the four checks that are present are honestly and precisely
verified (independently re-derived by this audit and found correct for the current shader), but the
scene this file constructs (`DiffuseColor` and `EmissiveColor` both left at their class defaults for
every check) happens to be exactly the one configuration that masks a real, confirmed production defect
in `CreateEnvMapResources()`'s fragment shader: it re-multiplies the CPU-precomputed
"`EmissiveColor + AmbientLightColor*DiffuseColor`" term by `DiffuseColor` a second time, instead of
adding it unscaled as FNA's `Lighting.fxh` does. See F1.

## Checklist Results

### API / XNA / FNA parity

`setEnvironmentMapProperty`/`setEnvironmentMapAmountProperty`/`setFresnelFactorProperty` (lines 211-213,
229-231, 246-248, 264-266) map directly to the FNA `EnvironmentMapEffect` surface. `TextureCube`
face-colour setup via `SetData(CubeMapFace::…)` (lines 170-175) correctly exercises all 6 faces with
visibly distinct colours, so an accidentally-wrong face index would show up as a visibly wrong colour
rather than a lucky match (comment lines 158-160, verified true by inspection of `BuildCubeMap()`).

One parity **imprecision** worth flagging in this file's own header comment, not in production
behaviour: lines 14 and 199 assert "no directional lights enabled (DirectionalLight0/1/2 all
default-disabled)". This is not accurate for `DirectionalLight0` specifically —
`EnvironmentMapEffect::EnvironmentMapEffect()`'s constructor (`EnvironmentMapEffect.cpp` line 40)
explicitly calls `DirectionalLight0.setEnabledProperty(true)`. The claim is *functionally* harmless
here only because `DirectionalLight`'s own default constructor (`DirectionalLight.cpp`) leaves
`direction_`/`diffuseColor_` at `Vector3::Zero`, so `light0Dir=(0,0,0)` and `ndotl0` in the shader is
gated to `0` by the `dir0sq > 0.0` NaN guard regardless of the `Enabled` flag's true value — but the
comment's premise ("all default-disabled") is factually wrong, not just imprecise phrasing. Severity:
LOW/documentation-only (see F2).

### Behavioral correctness

Independently re-derived the fragment shader math (`CreateEnvMapResources()` lines 3905-3929) against
the test's own hand-derived geometry:
- `E = normalize(eyePos - worldPos) = (0,0,-1)`, `N = (0,0,-1)`, `reflDir = reflect(-E,N) = (0,0,-1)`
  — exactly the `NegativeZ` face direction, confirming Check A's premise.
- `viewAngle = dot(E,N) = 1` exactly ⇒ Fresnel term `pow(max(1-|1|,0), f) = 0` for any `f>0` — confirms
  Check B's premise exactly, independent of the emissive-color bug below (this term is a separate,
  correctly-implemented piece of the shader).
- Since `DirectionalLight0/1/2` all contribute `0` diffuse (per the parity note above) and
  `AmbientLightColor`/`EmissiveColor` are both left at their class defaults (`Vector3::Zero`), `lightSum
  = 0` and (per F1's derivation) `litRGB = 0` regardless of whether the shader's emissive/diffuse
  composition is correct or buggy — this is exactly what masks F1 in every one of this file's checks.

### Logic

**F1** — see Detailed Findings.

### Robustness

Checks A/B/C are a genuinely good differential design: B and C each independently gate the same blend
via a different parameter (Fresnel vs. Amount) against the same Check-A baseline scene, which is the
correct technique to isolate two independently-failing hypotheses. This part of the file is not in
question.

### Testing

**F1** is a real production defect this file's own scene construction cannot detect, because it never
varies `DiffuseColor` away from its class default (white, `(1,1,1,1)`) nor `EmissiveColor` away from
its class default (`(0,0,0)`) in any of its four checks — see F1 for why either alone is sufficient to
mask the bug.

## Detailed Findings

### F1 — `env_map3d.wgsl`'s fragment shader re-multiplies the CPU-precomputed emissive+ambient term by
`DiffuseColor` a second time, diverging from FNA's `Lighting.fxh` convention; this file's own scene
cannot detect it

- Severity: HIGH
- Confidence: HIGH (confirmed by direct source reading of both the CPU-side production code and the
  WGSL shader it feeds, cross-checked against FNA's C# `EffectHelpers.SetMaterialColor` and HLSL
  `Lighting.fxh`)
- Category: correctness / FNA parity — same class of defect as the cross-cutting-confirmed Bgfx/Vulkan
  `EnvironmentMapEffect` bug (`AUDIT_CROSS_CUTTING_FINDINGS.md`'s "NEW, Bgfx/Vulkan shared" entry), now
  independently confirmed in a **third** backend.
- Location/symbol: `WebGPUGraphicsBackend::CreateEnvMapResources()`, fragment shader `fs_main`
  (~line 3915): `let litRGB = (ep.emissiveAmount.xyz + lightSum) * ep.diffuseColor.rgb;` — `emissiveAmount.xyz`
  is populated by `FillEnvMapParams()` (line ~488, `out[8..10] = p.emissiveColor[0..2]`).
- Evidence chain:
  1. FNA's `Lighting.fxh::ComputeLights()` computes `result.Diffuse = mul(diffuse, lightDiffuse) *
     DiffuseColor.rgb + EmissiveColor;` — `EmissiveColor` is **added** after the diffuse×DiffuseColor
     product, never re-multiplied by `DiffuseColor`.
  2. FNA's C# `EffectHelpers.SetMaterialColor()` documents the CPU-side optimization this shader
     formula depends on: "If we set our emissive parameter to `emissive+(ambient*diffuse)`, the shader
     no longer needs to bother adding the ambient contribution," reducing the shader's job to
     `(sum(diffuse directional light) * DiffuseColor) + EmissiveColor`.
  3. This project's own `EnvironmentMapEffect::FillGpuDrawParams()` (lines 421-426) implements exactly
     that CPU-side optimization, with a comment that says so explicitly: `// emissive + ambient *
     diffuse, pre-combined and pre-multiplied by alpha (matches FNA)`.
  4. `CreateEnvMapResources()`'s WGSL then does **not** honor the contract that comment describes: it
     adds the already-correct, already-ambient-folded `emissiveAmount.xyz` to `lightSum` (the
     directional-light sum only — ambient is *not* part of `emissiveAmount`, it's folded separately;
     ambient itself never even reaches this shader as a distinct term) and multiplies the whole thing
     by `DiffuseColor` a second time, instead of adding `emissiveAmount.xyz` unscaled after the
     `lightSum * DiffuseColor` product.
- Why it doesn't show up in this test: substituting `DiffuseColor=(1,1,1)` (this test's default,
  never overridden by any of its 4 checks) makes `emissiveAmount * DiffuseColor == emissiveAmount`
  algebraically identical to the correct "add unscaled" behaviour — the bug is invisible whenever
  `DiffuseColor` is pure white, *regardless* of `EmissiveColor`'s value. Separately,
  `EmissiveColor`/`AmbientLightColor` are both left at their class default of `Vector3::Zero` in every
  check here, which independently zeroes `emissiveAmount` and masks the bug a second, unrelated way.
  Either masking condition alone is sufficient; this file has both.
- Concrete failing scenario the current test suite (not just this file) does not cover: set
  `DiffuseColor=(0.5,0.5,0.5)` and `EmissiveColor=(1,0,0)` with no ambient/directional light and
  Fresnel/EnvironmentMapAmount at `0` (isolating the lit-color path from the reflection path). FNA/a
  correct implementation would render `(1,0,0)` (pure red, unscaled emissive). This backend's actual
  shader renders `(0.5,0,0)` (emissive incorrectly halved by `DiffuseColor`).
- FNA/XNA comparison: see evidence chain above — `Lighting.fxh`'s `ComputeLights()` and its
  `EffectHelpers.SetMaterialColor()` CPU-side counterpart are the two references establishing the
  correct contract; this shader satisfies neither.
- Related files: `AUDIT_CROSS_CUTTING_FINDINGS.md`'s existing Bgfx-confirmed (5 test files) /
  Vulkan-suspected instance of the identical defect for `EnvironmentMapEffect` specifically (not
  `BasicEffect`/`lit_textured3d.wgsl`, which this audit separately verified correct — see
  `webgpu_littextured3d_test.cpp`'s own audit — the bug is isolated to the `EnvironmentMapEffect`
  shader family across at least 3 backends now: Bgfx, WebGPU, and very likely Vulkan pending that
  backend's own dedicated re-check).
- Suggested future action (not implemented by this audit): change `env_map3d.wgsl`'s `fs_main` to
  `let litRGB = lightSum * ep.diffuseColor.rgb + ep.emissiveAmount.xyz;`, matching the contract
  `EnvironmentMapEffect.cpp`'s own comment already documents; add a differential test in this file (or
  a sibling) with `DiffuseColor != white` and `EmissiveColor != black` to catch a regression.

### F2 — Header comment's "DirectionalLight0/1/2 all default-disabled" claim is factually wrong for
DirectionalLight0

- Severity: LOW
- Confidence: HIGH (constructor read directly)
- Category: documentation accuracy (test-authoring, not production behavior)
- Location/symbol: file header comment lines 14, 199; contradicted by
  `EnvironmentMapEffect::EnvironmentMapEffect()` (`EnvironmentMapEffect.cpp` line 40):
  `DirectionalLight0.setEnabledProperty(true);`
- Why it matters: harmless today only because `DirectionalLight`'s own default direction/diffuse are
  zero vectors (verified in `DirectionalLight.cpp`), so `Enabled=true` with all-zero parameters still
  contributes no light. A future change to `DirectionalLight`'s or `EnvironmentMapEffect`'s
  construction-time defaults could silently invalidate this test's premise while the comment continues
  to assert the opposite.
- Suggested future action: correct the comment to state "DirectionalLight0 is enabled by the
  constructor but has zero direction/diffuse by default, so it contributes nothing; DirectionalLight1/2
  are genuinely disabled."

## Cross-File Observations

- This is the **third** backend (after Bgfx, confirmed via 5 test files, and Vulkan, suspected but
  unconfirmed per the cross-cutting doc) found to share the `EnvironmentMapEffect`
  emissive-re-multiplied-by-diffuse defect — independently confirmed here by direct WGSL/CPU-side
  cross-reading, not inferred from test phrasing. Recommend updating
  `AUDIT_CROSS_CUTTING_FINDINGS.md`'s existing entry to add WebGPU as a confirmed (not just suspected)
  third instance.
- Contrasts with `webgpu_littextured3d_test.cpp`'s (`BasicEffect`/`lit_textured3d.wgsl`) audit, which
  independently verified the *correct* `lightSum*diffuseColor + emissiveColor` composition for that
  sibling shader — proving the bug is specific to the `EnvironmentMapEffect` shader's own WGSL
  authoring, not a shared uniform-packing or CPU-side mistake (the CPU-side `EnvironmentMapEffect.cpp`
  code is, itself, correct).
- Per this audit's cross-cutting mandate: this file uses `EnvironmentMapEffect`, not
  `SkinnedEffect`/`SkinnedPbrEffect`, so the confirmed `CreateSkinnedResources()` world-space-normal
  bug does not apply here. The fog-formula bug (EasyGL/Bgfx/Vulkan) also does not apply — this shader
  has its own, separately-correct fog gate (`fogEnabled`/`fogRaw` at lines 3898-3901, a simple
  `(FogEnd-z)/(FogRange)` linear-depth formula not exercised by this file's checks at all, since none
  of them enable fog — an untested but not necessarily buggy path, out of scope for this report).

## Missing or Weak Tests

- See F1 — no check in this file (or, so far as searched, elsewhere in `examples-tests-webgpu`) varies
  `DiffuseColor` away from white while also setting a non-zero `EmissiveColor`/`AmbientLightColor`,
  which is exactly the scenario needed to catch this defect.
- Fog is entirely unexercised by this file (all checks leave fog disabled).
- `EnvironmentMapSpecular`/the specular-add term (`ep.envMapSpecFresnelF.xyz * envSample.a *
  combinedAlpha`) is present in the shader but never independently exercised (the cube map's alpha
  channel is always opaque/1.0 in this test's `BuildCubeMap()`, and `EnvironmentMapSpecular` is never
  set away from its default `(0,0,0)`).

## Positive Findings

- Checks A/B/C's hand-derived reflection-vector and Fresnel-collapse geometry were independently
  re-verified by this audit and found exactly correct — a genuinely rigorous, well-reasoned test
  design for the parts it actually covers.
- The 6-distinct-solid-colour cube map (`BuildCubeMap()`) is a good technique: an accidentally-wrong
  face index would produce a visibly wrong, not coincidentally-matching, colour.
- Check D's isolation of the indexed-draw dispatch path from the non-indexed path (A/B/C) is the same
  proven-useful technique already noted favourably in this shard's other audits.

## Final Assessment

A carefully-constructed, mathematically rigorous test for the parts of `EnvironmentMapEffect` it
targets (reflection direction, Fresnel gating, amount gating), but its scene construction — leaving
`DiffuseColor`/`EmissiveColor`/`AmbientLightColor` all at their class defaults in every check — happens
to be exactly the condition set that hides a real, confirmed production defect in this backend's own
`env_map3d.wgsl` fragment shader (F1): emissive light is incorrectly re-multiplied by `DiffuseColor`
instead of being added unscaled, contradicting both FNA's `Lighting.fxh` and this project's own
`EnvironmentMapEffect.cpp` CPU-side comment describing the intended contract. This is the same class of
defect already confirmed in Bgfx and suspected in Vulkan — WebGPU is now a confirmed third instance.
