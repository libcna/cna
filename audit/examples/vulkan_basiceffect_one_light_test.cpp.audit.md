# Audit: examples/vulkan_basiceffect_one_light_test.cpp

## Metadata

- Source file: `examples/vulkan_basiceffect_one_light_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Vulkan backend `BasicEffect` single-`DirectionalLight` pixel test
  (Task 368)
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_vulkan_basiceffect_one_light` /
  `Vulkan_BasicEffect_OneLight`, `cmake/Tests/VulkanTests.cmake:515-517`)
- XNA/FNA relevance: direct — `BasicEffect.LightingEnabled`, `AmbientLightColor`, `DiffuseColor`,
  `DirectionalLight0.Enabled`/`.Direction`/`.DiffuseColor`
- FNA reference: `HLSL/Lighting.fxh` (`ComputeLights`, diffuse-only term), `DirectionalLight.cs`'s `Enabled`
  setter (zeroes GPU-facing diffuse/specular)
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp::FillGpuDrawParams()`
  (lines 81-91, `DirectionalLight0.Enabled` gating), `src/CNA/Internal/Backends/Vulkan/shaders/
  lit_textured3d.frag.glsl` (lines 44-71, diffuse computation).

## Purpose

4-check pixel test for `BasicEffect`'s single-light diffuse formula: (1) `NdotL=0.5` partial-lit case, asserting
`(Ambient+Light*0.5)*Diffuse` and explicitly disproving two plausible wrong implementations (fully-saturated
`NdotL=1`, and an ambient-dropped variant); (2) back-facing normal → ambient-only (negative dot clamped to 0);
(3) `DirectionalLight0.Enabled=false` → ambient-only (light contributes exactly 0, not just a dimmed value).
Uses a stride-32 `VertexPositionNormalTexture` quad with a white 1×1 texture to isolate the lighting formula from
texture sampling, and `World=View=Projection=Identity` (all left at `BasicEffect`'s own defaults, never set
explicitly).

## Executive Verdict

**Healthy** — every expected constant was independently re-derived by hand against the actual
`lit_textured3d.frag.glsl` diffuse formula and the `BasicEffect.cpp` light-gating logic, and all four checks
match exactly.

## Checklist Results

### API / XNA / FNA parity
`setLightingEnabledProperty`, `setAmbientLightColorProperty`, `setDiffuseColorProperty`,
`DirectionalLight0.setEnabledProperty`/`.setDirectionProperty`/`.setDiffuseColorProperty` all map correctly to
FNA's `IEffectLights` surface, used with correct `Vector3` types throughout.

### Behavioral correctness — independent re-derivation
`kAmbient(0.1,0.1,0.1)`, `kMaterialDiffuse(0.5,0.5,1.0)`, `kLightDiffuse(1.0,0.6,0.2)`, `kLightDir(0,0,1)`
(direction field points *from* the light, so `-lightDir=(0,0,-1)` per FNA convention, matched by the shader's own
`dot(N,-nL0)`), `kNormalLit(0.8660254,0,-0.5)`.

- `NdotL0 = dot(N,-lightDir) = dot((0.866,0,-0.5),(0,0,-1)) = 0.5` — matches the file's own "NdotL=0.5" claim.
- `lit = (ambient+NdotL0*lightDiffuse)*materialDiffuse = (0.1+0.5, 0.1+0.3, 0.1+0.1)*(0.5,0.5,1.0)
  = (0.6,0.4,0.2)*(0.5,0.5,1.0) = (0.3,0.2,0.2)` → `×255 = (76.5,51,51) ≈ (77,51,51)` — **matches
  `kExpectedLit(77,51,51)` exactly**, and matches `lit_textured3d.frag.glsl`'s actual formula
  (`lightSum = ambient + NdotL0*light0Diffuse`, `lit = lightSum*fragTint.rgb`, texture white so `tex=1`).
- `kFullySaturated(140,89,77)` (the wrongly-expected `NdotL=1` value the test disproves):
  `(ambient+1.0*lightDiffuse)*materialDiffuse = (1.1,0.7,0.3)*(0.5,0.5,1.0) = (0.55,0.35,0.3)×255
  ≈(140.25,89.25,76.5)≈(140,89,77)` — confirmed this is indeed the fully-saturated value, correctly disproved.
- `kAmbientIgnored(64,38,26)` (the wrongly-expected ambient-dropped value the test disproves):
  `NdotL0*lightDiffuse*materialDiffuse = (0.5,0.3,0.1)*(0.5,0.5,1.0) = (0.25,0.15,0.1)×255≈(63.75,38.25,25.5)
  ≈(64,38,26)` — confirmed correctly disproved.
- `kExpectedAmbientOnly(13,13,26)` (back-facing normal and disabled-light cases):
  `ambient*materialDiffuse = (0.1,0.1,0.1)*(0.5,0.5,1.0) = (0.05,0.05,0.1)×255=(12.75,12.75,25.5)≈(13,13,26)`
  — matches. For the back-facing normal (`kNormalBackFacing = -kNormalLit`), `dot(N,-lightDir)` becomes negative,
  correctly clamped to `0` by the shader's `NdotL0=max(dotL0,0.0)` (`lit_textured3d.frag.glsl:55`). For the
  disabled-light case, `BasicEffect::FillGpuDrawParams()` lines 85-90 correctly force `ld=Vector3::Zero` when
  `!light0On`, zeroing the diffuse term identically.

### Logic
Four checks each isolate a genuinely distinct, independently-failable hypothesis (correct partial-lit value vs.
two specific wrong formulas, back-face clamping, light-disabled gating) — good test design, not merely "assert
one number."

### Memory/resource lifetime
No dynamic allocation beyond the usual `GraphicsDeviceManager`/`Texture2D`/`BasicEffect` stack objects; no
lifetime concerns.

### C++ correctness
`renderWith()` (lines 92-128) constructs a fresh `BasicEffect` per call — no state leakage between the four
`Draw()`-time scenarios.

One point worth flagging as **INFO, not a defect**: `World`/`View`/`Projection` are never set (left at
`BasicEffect`'s own `Identity` defaults), so `eyePositionWorld` (derived in `BasicEffect.cpp:126-130`, but
computed unconditionally whenever `lightingEnabled_` is true) resolves to `(0,0,0)`, and the quad's sampled
center `worldPos` is also exactly `(0,0,0)` — meaning the shader's specular half-vector computation (`E =
normalize(eyePos-worldPos)`, `lit_textured3d.frag.glsl:50`) evaluates `normalize(vec3(0))`, a mathematically
degenerate `0/0` case. This audit traced the consequence: since this test never sets `SpecularColor` on either
`BasicEffect` or `DirectionalLight0`, `DirectionalLight0`'s own default `SpecularColor` is `Vector3::Zero`
(`DirectionalLight.hpp`, default-constructed `Vector3`), so the final `specularRGB` term is multiplied by zero
regardless of whether `E` is `NaN`. On the SPIR-V target this compiles to, the `max()`/`FMax` extended
instruction is specified to return the non-NaN operand when exactly one input is `NaN`, so
`max(dot(h0,N),0.0)` resolves to `0.0` rather than propagating `NaN`, and the whole specular term evaluates to a
clean, finite `0` — not a real defect in the current (Vulkan/SPIR-V) backend, but a latent fragility that would
need re-checking if this exact shader logic were ever ported to a target without that NaN-avoidance guarantee.

### Robustness
Covered above — the degenerate-eye-position case resolves safely given SPIR-V's `FMax` semantics, but is worth
noting as a fragile pattern rather than a deliberately-designed safeguard.

### Testing
All four checks are strong, correctly-isolated, and independently confirmed against the real production formula.

## Detailed Findings

No HIGH/CRITICAL/MEDIUM findings — every asserted value was independently re-derived and found correct.

### F1 — `View`/`Projection` left unset, relying on an unstated `Identity` default and an incidental SPIR-V NaN-avoidance guarantee for a degenerate specular half-vector

- Severity: LOW
- Confidence: MEDIUM (the degenerate-input analysis is exact; the "resolves safely" conclusion depends on
  SPIR-V's `GLSL.std.450 FMax` NaN-avoidance semantics, which this audit did not runtime-verify by execution,
  only by reasoning from the target compilation model)
- Category: robustness / maintainability
- Location/symbol: `renderWith()` (lines 92-128) never calls `setViewProperty`/`setProjectionProperty`;
  `lit_textured3d.frag.glsl:50` (`E = normalize(eyePos_pad.xyz - fragWorldPos)`)
- Evidence: see full derivation above.
- Why it matters: low priority since the material/light `SpecularColor`s both being effectively zero here make
  this safe in practice on the current backend, but the pattern ("compute a half-vector from a possibly-zero
  eye-to-surface vector, whenever lighting is enabled, regardless of whether specular actually matters for the
  material") is inherently fragile and worth a defensive `if (dot(v,v) > 0)` guard rather than relying on an
  extended-instruction-set guarantee that is specific to the current compilation target.
- FNA/XNA comparison: N/A — this is a CNA-internal implementation-robustness observation, not an FNA behavior
  question.
- Suggested future action: none required for this test file; noted as a production-code robustness observation
  surfaced while cross-checking this test's own correctness.

## Cross-File Observations

- Shares the identical degenerate-eye-position pattern with `vulkan_basiceffect_multilight_emissive_test.cpp`
  (also never sets `View`/`Projection`) — see that file's report for the same F1-equivalent note.
- `vulkan_basiceffect_specular_test.cpp`/`preferperpixellighting_test.cpp` both set a real `View` via
  `Matrix::CreateLookAt`, so they do not hit this degenerate case — only the two files that omit camera setup
  entirely are affected.
- `RasterizerState::CullNone` (line 120, "Task 896" comment) confirmed accurate via the same cross-check
  performed for every sibling file in this batch.

## Missing or Weak Tests

- No case exercises `DirectionalLight0.SpecularColor` on this file (reasonable — this file is diffuse-focused;
  `vulkan_basiceffect_specular_test.cpp` covers specular separately).

## Positive Findings

- All four checks independently re-derived and confirmed exactly correct against both the FNA-style diffuse
  formula and the actual `lit_textured3d.frag.glsl` shader code.
- Explicitly disproves two plausible-but-wrong implementations (fully-saturated dot product, ambient-dropped)
  rather than only asserting the correct value — meaningfully stronger than a single-assertion test.
- `DirectionalLight0.Enabled=false` zeroing the light's contribution (not just dimming it) is correctly isolated
  and matches `BasicEffect.cpp`'s actual gating logic exactly.

## Final Assessment

A well-constructed, fully-verified diffuse-lighting test with no corrective action needed. The only note is an
INFO/LOW-severity observation about a latent (currently harmless, SPIR-V-guarantee-dependent) degenerate-vector
fragility in the shared production shader, surfaced incidentally while confirming this test's own correctness.
