# Audit: examples/environmentmapeffect_alphascaledlerp_test.cpp

## Metadata

- Source file: `examples/environmentmapeffect_alphascaledlerp_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard — `EnvironmentMapEffect` base cube-map lerp alpha-scaling
  pixel test, shared verbatim source registered on all 3 3D-capable backends: EasyGL
  (`cmake/Tests/EasyGLTests.cmake:578-581`,
  `EasyGL_EnvironmentMapEffect_AlphaScaledLerp`), Bgfx (`cmake/Tests/BgfxTests.cmake:204-209`,
  `Bgfx_EnvironmentMapEffect_AlphaScaledLerp`), Vulkan
  (`cmake/Tests/VulkanTests.cmake:286-291`, `Vulkan_EnvironmentMapEffect_AlphaScaledLerp`) —
  confirmed by direct `grep`, matching the file's own header comment's "shared source across
  EasyGL/Vulkan/Bgfx" claim exactly.
- XNA/FNA relevance: direct — `EnvironmentMapEffect.EnvironmentMapAmount`,
  `EnvironmentMapSpecular`, `Alpha`, `Texture`, `EnvironmentMap`.
- FNA reference: `Graphics/Effect/StockEffects/HLSL/EnvironmentMapEffect.fx`'s `PSEnvMap`/
  `PSEnvMapSpecular`: `envmap = SAMPLE_CUBEMAP(EnvironmentMap, pin.EnvCoord) * color.a;` (i.e. the
  *whole* envmap sample, RGB and alpha both, is scaled by `color.a` = combined texture×diffuse
  alpha, before either the base-lerp RGB or the specular-alpha term consumes it) followed by
  `color.rgb = lerp(color.rgb, envmap.rgb, pin.Specular.rgb)`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
  (`FillGpuDrawParams()` lines 398-467), `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (fragment shader source, `mix(baseColor,envSample.rgb*combinedAlpha,blendFactor)`),
  `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.frag.glsl`/`.vert.glsl`,
  `src/CNA/Internal/Backends/Bgfx/shaders/fs_env_map3d.sc`/`vs_env_map3d.sc`.

## Purpose

Two-case pixel test for Task 891: proves the base cube-map lerp target
(`mix(baseColor, envSample.rgb, Amount)`'s second argument) is scaled by `combinedAlpha`
(texture-alpha × diffuse-alpha) exactly as FNA's real `PSEnvMap` does, using the identical
discriminating technique Task 395 already established for the *specular* term but applied here to
the *base lerp* term instead. `EnvironmentMapAmount=1.0` forces `mix()` to return its second
argument unconditionally, isolating the base lerp term cleanly; `EnvironmentMapSpecular=(0,0,0)`
zeroes the specular addition so it can't confound the result; the cube map is fully opaque
(`alpha=255`) so only the texture/diffuse alpha varies between the two cases. Case (a)
(`Alpha=1.0`) is explicitly documented as non-discriminating (`combinedAlpha=1` either way — a
sanity check only that the cube map is visible at all); case (b) (`Alpha=0.5`) is the actual
regression check, where FNA's real formula gives `CubeColor*0.5=(100,50,25)` and the pre-fix bug
would instead repeat case (a)'s `(200,100,50)` unscaled.

## Executive Verdict

**Healthy for its own stated purpose** — both expected constants were independently re-derived from
the actual current fragment-shader source on all 3 backends and match exactly; the Task 891 fix is
genuinely present and correct on EasyGL, Vulkan, and Bgfx alike. However, this test — like several
sibling `EnvironmentMapEffect` tests across the codebase — cannot detect two other confirmed,
still-open defects in the exact code paths it exercises, because its scene happens to be structured
in a way that masks both (see F1, extending two entries already tracked in
`AUDIT_CROSS_CUTTING_FINDINGS.md`).

## Checklist Results

### API / XNA / FNA parity
`setEnvironmentMapAmountProperty`/`setEnvironmentMapSpecularProperty`/`setEmissiveColorProperty`/
`setAlphaProperty`/`setTextureProperty`/`setEnvironmentMapProperty` (lines 116-124) all correctly
map to FNA's `EnvironmentMapEffect` surface with proper getter/setter wrappers — unlike
`BasicEffect::VertexColorEnabled`'s bare-field lapse, `EnvironmentMapEffect` follows the project's
own C# property convention throughout (confirmed by reading `EnvironmentMapEffect.cpp` lines 251-284).

### Behavioral correctness
Independently re-derived both cases against the current production code, not merely re-stating the
file's own comment:
- `FillGpuDrawParams()` (`EnvironmentMapEffect.cpp:418-421`) pushes
  `p.diffuseColor = (DiffuseColor.X*Alpha, .Y*Alpha, .Z*Alpha, Alpha)`. Since `DiffuseColor` is left
  at its class default `(1,1,1)` (`EnvironmentMapEffect.hpp:387`, never set by this test),
  `p.diffuseColor.a = Alpha` directly, and `texColor.a=1` (opaque white 1×1 base texture) — so
  `combinedAlpha = diffuseColor.a * texColor.a = Alpha` exactly, matching the test's own stated
  formula.
- All 3 backends' fragment shaders compute `float combinedAlpha = <diffuseColor>.a * texColor.a;`
  then `vec3 rgb = mix(baseColor, envSample.rgb * combinedAlpha, blendFactor) + <specular term>` —
  read directly from `env_map3d.frag.glsl:44,52`, `fs_env_map3d.sc:33,41`, and the equivalent EasyGL
  fragment source — confirmed identical across all 3, and confirmed this is the **post-fix**
  formula (envSample scaled by `combinedAlpha` before the lerp), not the pre-Task-891 unscaled bug.
- Case (a): `Amount=1.0` → `blendFactor=Amount=1.0`. Worth tracing carefully because
  `FresnelFactor` defaults to `1.0` in the constructor (`EnvironmentMapEffect.cpp:43`,
  `setFresnelFactorProperty(1.0f)`), which per that setter's own logic (`fresnelEnabled_ = (v !=
  0.0f)`) leaves `fresnelEnabled_=true` for every instance unless a caller explicitly zeroes it —
  and this test's `renderWith()` never touches `FresnelFactor`, so the Fresnel-weighted branch
  (`blendFactor = pow(max(1-|viewAngle|,0), fresnelFactor) * Amount`) is the one actually active,
  not the flat `Amount`-only branch. This audit traced `viewAngle = dot(E,N)` through to its
  concrete value for this exact scene: `World=View=Projection=Identity`, the quad's normal is
  `(0,0,1)`, and `eyePositionWorld` is derived as `Matrix::Invert(view_).getTranslationProperty()`
  = `(0,0,0)` (translation of the identity matrix). Since the quad's vertices are also all at
  `z=0`, every vertex's `eyeDir = eyePos - worldPos = (0,0,0)-(x,y,0) = (-x,-y,0)` has a **z-component
  of exactly 0** — meaning the eye direction, after per-vertex normalization, interpolation across
  the triangle, and per-fragment renormalization, stays in the quad's own `z=0` plane for every
  fragment (confirmed identically in all 3 backends: `env_map3d.vert.glsl`'s
  `vEyeDir = ep.eyePos_pad.xyz - worldPos`, `vs_env_map3d.sc`'s equivalent, and EasyGL's
  `EnsureEnvMapped3DProgram()`'s `eyeVector=normalize(uEyePosition-worldPos)`, all structurally
  identical). Therefore `viewAngle = dot(E,N) = E.z = 0` exactly, so
  `pow(max(1-|0|,0), fresnelFactor) = pow(1, fresnelFactor) = 1` **regardless of `fresnelFactor`'s
  actual value** — collapsing the Fresnel-enabled branch to a no-op multiplier and leaving
  `blendFactor = 1 * Amount = Amount = 1.0`, exactly matching the test's own expectation. This
  resolves cleanly: no defect, and the test's case (a) passes for the reason it should. See F2 for
  a coverage observation this derivation surfaces.
- Case (b): assuming `blendFactor` does resolve to `Amount=1.0` in practice (as the test's own
  passing assumption implies), `envSample.rgb*combinedAlpha = (200,100,50)*0.5 = (100,50,25)`,
  matching `Color(100,50,25,255)` — the fix is correctly modeled.

### Logic
`renderWith()`'s up-to-20-frame retry-until-non-black loop (lines 127-141) is the same defensive
pattern seen in the other files in this batch — appropriate here too.

### C++ correctness
`colourMatch()`/`closeTo()` (lines 82-89) use a `±20` tolerance, looser than
`dualtextureeffect_vertexcolor_test.cpp`'s `±8` — reasonable given `EnvironmentMapEffect`'s extra
lighting/Fresnel computation stages introduce more floating-point accumulation than a flat
multiply-only formula.

### Memory/resource lifetime
`makeSolidCube()` returns a `std::unique_ptr<TextureCube>` (matching `TextureCube`'s deleted copy
constructor, confirmed in its header) — correct ownership transfer, no leaks.

### Performance
N/A — trivial single-quad 64×64 pixel test.

### Thread safety
N/A.

### Architecture
Correctly backend-agnostic source, confirmed genuinely shared (byte-identical registration across
3 `cmake_<backend>_test` calls, matching the file's own claim).

### Maintainability
Detailed, FNA-source-accurate header comment explicitly quoting the real `PSEnvMap`/
`PSEnvMapSpecular` formula and distinguishing this task's fix (base lerp) from the already-fixed
Task 395 (specular term) — a strong documentation example.

### Portability
No platform-specific code.

### Robustness
See Logic above; retry loop and tolerance are appropriately calibrated.

### Testing
Directly tests the Task 891 fix and confirms it on all 3 backends via independent shader-source
re-derivation. Does **not**, and by its own narrow design cannot, detect two other defects already
confirmed in this exact code path elsewhere in the audit — see F1.

### Cross-file consistency
`EnvironmentMapEffect::FillGpuDrawParams()` and all 3 backends' fragment/vertex shaders were read in
full for this report, not assumed correct from the file's comment alone.

## Detailed Findings

### F1 — This test's scene structurally cannot detect two other confirmed `EnvironmentMapEffect` defects in the exact shader it exercises: the Bgfx/Vulkan `EmissiveColor`-re-multiply bug, and (Vulkan-only) the missing vertex Y-flip

- Severity: MEDIUM
- Confidence: HIGH for the emissive-remultiply confirmation (independently read the shader source);
  MEDIUM for the practical "would this scene reveal it" masking claim (reasoned from the concrete
  values used, not empirically re-run)
- Category: test-coverage / cross-cutting-defect-corroboration
- Location/symbol: `renderWith()` never varies `DiffuseColor` away from its class default `(1,1,1)`
  (`EnvironmentMapEffect.hpp:387`); `EmissiveColor` is explicitly forced to `(0,0,0)` (line 118); the
  quad's `View`/`Projection` are both `Matrix::getIdentityProperty()` (lines 122-123) and only the
  exact center pixel is sampled (`readCenter()`, lines 91-97).
- Evidence:
  1. **Emissive-remultiply bug** — `AUDIT_CROSS_CUTTING_FINDINGS.md` already documents (as
     "confirmed across 5 Bgfx test files... Test-file phrasing suggests Vulkan shares the same bug
     (unconfirmed pending a dedicated check)") that `EnvironmentMapEffect`'s fragment shader
     re-multiplies the already-alpha-premultiplied `EmissiveColor` (which on the CPU side already
     equals `(Emissive + Ambient*Diffuse)*Alpha`, per `EnvironmentMapEffect.cpp:367-371`/`424-426`)
     by `DiffuseColor` a *second* time inside the shader:
     `litRGB = (emissiveColor + lightSum) * diffuseColor.rgb` — confirmed by this audit's own direct
     reading of **both** `fs_env_map3d.sc:28` (Bgfx: `vec3 litRGB = (u_emissiveColor.xyz + lightSum)
     * u_diffuseColor.xyz;`) **and** `env_map3d.frag.glsl:39` (Vulkan:
     `vec3 litRGB = (ep.emissive_em.xyz + lightSum) * ep.diffuseColor.rgb;`) — **this independently
     confirms the cross-cutting document's "unconfirmed pending a dedicated check" note for Vulkan**:
     Vulkan has the identical bug, not merely "test-file phrasing suggesting" it might.
     This test cannot reveal it: `EmissiveColor` is forced to `(0,0,0)`, and `AmbientLightColor`
     (which also feeds the emissive term) is left at its class default `(0,0,0)`
     (`EnvironmentMapEffect.hpp:389`) — so the CPU-side `p.emissiveColor` pushed to the shader is
     `(0,0,0)` regardless of the bug, and re-multiplying zero by anything is still zero, exactly
     the same masking mechanism already described for the 5 Bgfx tests in the cross-cutting doc
     ("masked because none of that 6-file test family varies `DiffuseColor` away from its default").
  2. **Vulkan-only missing Y-flip** — `AUDIT_CROSS_CUTTING_FINDINGS.md` documents `env_map3d.vert.glsl`
     lacking the Y-flip present in every other core Vulkan 3D vertex shader, confirmed across 4 other
     named test files; this audit independently re-read `env_map3d.vert.glsl` in full and confirms
     no `pos.y = -pos.y`-equivalent line exists (contrast with `dual_texture3d.vert.glsl`/
     `dual_texture_colored3d.vert.glsl` in the same Vulkan shader directory, both of which do have
     it). This exact test's own scene (identity View/Projection, a Z-facing quad, and — critically —
     sampling only the exact center pixel) is symmetric under a vertical mirror in precisely the same
     way the 4 already-cited test files are, so this file joins that list as a 5th masked instance,
     specifically for the Vulkan-registered run of this test.
- Why it matters: this is a real, if narrow, test-coverage gap rather than a flaw in this file's own
  stated purpose (which it fulfills correctly) — a future contributor reading only this test's
  passing result could mistakenly believe `EnvironmentMapEffect`'s emissive/ambient lighting and
  Vulkan's vertical orientation are both verified-correct for this effect, when in fact neither is
  exercised by any value this scene uses.
- FNA/XNA comparison: FNA's real `EnvironmentMapEffect.fx` (`Lighting.fxh`'s `AddSpecular`/
  `ComputeLights` convention) adds `EmissiveColor` unscaled by `DiffuseColor` after it has already
  been combined with `AmbientLightColor*DiffuseColor` on the CPU side — the shader-side
  re-multiplication is the actual behavioral divergence from FNA, not merely a style difference.
- Related files: `src/CNA/Internal/Backends/Bgfx/shaders/fs_env_map3d.sc`,
  `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.frag.glsl`,
  `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl`,
  `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`.
- Suggested future action (not implemented by this audit): update
  `AUDIT_CROSS_CUTTING_FINDINGS.md`'s emissive-remultiply entry to mark Vulkan **confirmed** (not
  "unconfirmed pending a dedicated check") per this audit's direct shader read; add this file to the
  Y-flip entry's list of masked test files; consider a dedicated `EnvironmentMapEffect` test that
  varies `DiffuseColor` away from `(1,1,1)` while `EmissiveColor`/`AmbientLightColor` are non-zero
  (to actually exercise the emissive-remultiply defect), and a Vulkan-specific asymmetric-scene test
  (non-centered sample point, or a non-identity View) to actually exercise the Y-flip defect.

### F2 — This scene's identity-transform setup makes `viewAngle` exactly 0 for every fragment, structurally preventing this (and presumably sibling) `EnvironmentMapEffect` scenes from ever exercising the Fresnel `pow()` exponent's actual behavior

- Severity: LOW (a test-coverage observation, not a behavioral defect — this file's own assertions
  are unaffected and correctly pass for the right reason)
- Confidence: HIGH (fully traced arithmetically from the actual production code and confirmed
  structurally identical across all 3 backends)
- Category: test-coverage
- Location/symbol: `renderWith()`'s `World=View=Projection=Identity` (lines 122-124) combined with
  the quad's `z=0` vertices and normal `(0,0,1)` (lines 156-164); the derivation is spelled out in
  full under Behavioral correctness above.
- Evidence: because `eyePositionWorld` derives to `(0,0,0)` and every quad vertex is also at `z=0`,
  the eye-direction vector's z-component is exactly `0` for every vertex and therefore every
  interpolated fragment, in all 3 backends (`env_map3d.vert.glsl`, `vs_env_map3d.sc`,
  `EnsureEnvMapped3DProgram()`) — making `viewAngle = dot(E,N) = 0` and
  `pow(max(1-|viewAngle|,0), fresnelFactor) = pow(1, fresnelFactor) = 1` for **any** value of
  `fresnelFactor`, since `1` raised to any exponent is `1`. This means the Fresnel exponent term
  itself is never actually exercised by this scene shape — any bug in the `pow()` base/exponent
  order, a wrong `viewAngle` sign, or a completely broken `fresnelFactor` value would be invisible
  here, because the multiplier always collapses to exactly `1` regardless.
- Why it matters: this specific test's own case (a)/(b) assertions are about the base-lerp alpha
  scaling, not the Fresnel formula, so this doesn't weaken *this* file's own stated purpose — but it
  is a structural property of the common "quad at z=0, identity World/View/Projection" scene pattern
  used pervasively across this codebase's `EnvironmentMapEffect` tests (this file plus, per
  `AUDIT_CROSS_CUTTING_FINDINGS.md`, at least 5 other named test files sharing the same coplanar-eye
  geometry), meaning the Fresnel `pow()` term's actual numeric correctness may be under-verified
  project-wide if most/all `EnvironmentMapEffect` tests share this same "eye coplanar with surface"
  geometry rather than a genuinely off-axis camera. This audit did not have time to check every
  other `EnvironmentMapEffect` Fresnel-specific test (e.g. `easygl_environmentmapeffect_fresnel_test.cpp`)
  for whether they use a different, non-degenerate `viewAngle` — flagged here as a plausible pattern
  worth checking, not a confirmed gap in those other files.
- FNA/XNA comparison: N/A — this is purely a test-scene-geometry observation, not an XNA/FNA
  behavioral parity question.
- Related files: `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl`/`.frag.glsl`,
  `src/CNA/Internal/Backends/Bgfx/shaders/vs_env_map3d.sc`/`fs_env_map3d.sc`,
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`'s `EnsureEnvMapped3DProgram()`.
- Suggested future action (not implemented by this audit): when auditing
  `easygl_environmentmapeffect_fresnel_test.cpp`/`_fresnel_gradient_test.cpp` and their Bgfx/Vulkan
  equivalents, specifically check whether those tests use an off-axis `View`/eye position (so
  `viewAngle != 0`) — if they also use a coplanar-eye identity-transform setup, the Fresnel
  exponent's numeric correctness may have no test anywhere in the tree that can actually distinguish
  a correct `pow()` computation from a broken one.

## Cross-File Observations

- Confirms and strengthens two entries already in `AUDIT_CROSS_CUTTING_FINDINGS.md`: the
  emissive-remultiply bug (now confirmed on Vulkan by direct read, not just "test-file phrasing"),
  and the Vulkan Y-flip bug (this file is a 5th masked instance).
- `EnvironmentMapEffect`'s property API (unlike `BasicEffect::VertexColorEnabled`) correctly follows
  the project's getter/setter convention throughout — another positive counter-example alongside
  `DualTextureEffect`'s `VertexColorEnabled` (see that file's audit in this same batch).

## Missing or Weak Tests

- See F1 — no test in this shard varies `DiffuseColor` away from `(1,1,1)` while
  `EmissiveColor`/`AmbientLightColor` are non-zero, so the confirmed emissive-remultiply defect has
  no failing test anywhere in the tree that this audit found.
- See F1 — no Vulkan `EnvironmentMapEffect` test in this shard uses an asymmetric scene (non-center
  sample point or non-identity View), so the confirmed Y-flip defect likewise has no failing test.
- See F2 — no test in this shard was checked (within this audit's time budget) for whether it uses
  a genuinely off-axis `viewAngle`; if the whole `EnvironmentMapEffect` Fresnel-test family shares
  this file's coplanar-eye geometry, the Fresnel `pow()` exponent's correctness may be unverified
  project-wide.

## Positive Findings

- Both of this test's own stated expected constants were independently re-derived from the actual
  current shader source on all 3 backends and matched exactly — a genuinely verified, not merely
  trusted, pass.
- Strong, FNA-accurate header documentation distinguishing this fix from the related Task 395 fix.
- Correct property-based API usage throughout, avoiding the `BasicEffect` bare-field lapse.
- This audit's cross-file digging **resolved** a previously-"unconfirmed" item in
  `AUDIT_CROSS_CUTTING_FINDINGS.md` (Vulkan sharing the emissive-remultiply bug) via direct source
  comparison rather than leaving it open.

## Final Assessment

A correct, well-verified test for its own narrow, explicitly-stated purpose (Task 891's base-lerp
alpha scaling), confirmed genuinely fixed on all 3 backends. Its value is limited by scene choices
(default `DiffuseColor`, zeroed `EmissiveColor`, identity View, center-pixel-only sampling) that
happen to mask two other real, already-confirmed `EnvironmentMapEffect` defects in the exact
production code it touches (F1) — not a flaw in this file given its stated goal, but worth flagging
since a reader could reasonably over-credit this test's pass as broader coverage than it actually
provides. F2 is a resolved-but-noteworthy observation: this scene's geometry makes its own case (a)
assertion pass for a mathematically sound reason (traced and confirmed by this audit), while
incidentally revealing that the same geometry pattern would never be able to catch a broken Fresnel
exponent if one existed.
