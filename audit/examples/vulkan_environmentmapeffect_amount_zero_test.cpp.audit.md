# Audit: examples/vulkan_environmentmapeffect_amount_zero_test.cpp

## Metadata

- Source file: `examples/vulkan_environmentmapeffect_amount_zero_test.cpp` (152 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `EnvironmentMapEffect.EnvironmentMapAmount=0`
  cube-ignored verification, Vulkan backend (Task 393)
- File type: standalone `Game`-subclass executable
  (`VulkanEnvironmentMapAmountZeroTest`), CTest-registered
- XNA/FNA relevance: direct — `EnvironmentMapEffect.EnvironmentMapAmount`
- FNA reference: `HLSL/EnvironmentMapEffect.fx` (`PSEnvMap`'s `lerp(color.rgb, envmap.rgb,
  pin.Specular.rgb)`, where `pin.Specular.rgb` carries `EnvironmentMapAmount`)
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`,
  `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.frag.glsl` (line 52)

## Purpose

Single-check test proving `EnvironmentMapAmount=0` makes the cube map's RGB contribution
completely invisible — a bright green cube map should have zero effect on the rendered color,
which should equal the plain texture×emissive result. The header comment is explicit about a
known limitation: at `Amount=0`, the (buggy, pre-Task-394) additive formula and the (correct)
lerp formula produce the *same* result (`lerp(base, env, 0) = base` and `base + env*0 = base` are
identical), so this specific test cannot discriminate between the two formulas — that
discrimination is deliberately left to the sibling `amount_one` test (Task 394), which this
comment explicitly cross-references.

## Executive Verdict

**Needs attention** — the test's stated self-awareness about its own non-discriminating nature
(w.r.t. the additive/lerp formula question) is accurate and a good practice; the numeric
assertion itself is correct. As with its siblings in this batch, it inherits the un-detectable
`env_map3d.vert.glsl` Y-flip defect (F1).

## Checklist Results

### API / XNA / FNA parity
`setEnvironmentMapAmountProperty(0.0f)` (line 120) and `setEnvironmentMapSpecularProperty(
Vector3(0,0,0))` (line 121, isolating the amount term alone from the separate specular-alpha
term) are correct uses of the FNA-facing API.

### Behavioral correctness
Re-derived against the live shader: `blendFactor = pow(...) * EnvironmentMapAmount`. With
`EnvironmentMapAmount=0`, `blendFactor=0` unconditionally (the `pow(...)` factor is irrelevant
since it's multiplied by zero either way — true whether or not Fresnel is enabled). `mix(baseColor,
envColor, 0) = baseColor` exactly, and `EnvironmentMapSpecular=(0,0,0)` adds nothing. `baseColor =
litRGB*texColor`: `litRGB = (emissive+lightSum)*diffuse = (0.5,0.5,0.5)*(1,1,1) = (0.5,0.5,0.5)`
(default `DiffuseColor=(1,1,1)`, `DirectionalLight0`'s default diffuse is `(0,0,0)` so
`lightSum=0`, confirmed at `DirectionalLight.hpp:77`'s "all colors black" default). `texColor =
kTex(200,100,50)/255`. `baseColor = (0.5*200/255, 0.5*100/255, 0.5*50/255)*255 = (100,50,25)`.
Matches the test's expectation `Color(100,50,25,255)` (line 130) exactly.

### Logic
The test correctly varies only the cube's own color (bright green, `(0,255,0)`, maximally
different from any plausible base color) while holding everything else constant — if the cube
color leaked through at all (even partially), the green channel of the result would visibly
shift upward from the expected `50`; it doesn't need a subtle near-miss color to prove exclusion,
a bold contrasting color is the right choice for an "is this input ignored" test.

### Testing
The header comment's self-disclosed limitation (this check cannot distinguish lerp-at-zero from
additive-at-zero) is accurate and was independently confirmed by hand: both formulas evaluate to
`baseColor` exactly when `Amount=0`. This is good test-authoring hygiene — the same kind of
transparent self-flagging previously seen in `easygl_basiceffect_specular_test.cpp`'s check (b)
(see that file's own audit report) — though here the limitation doesn't weaken the test's own
correctness claim, since "cube ignored at Amount=0" is still a real, useful, correctly-verified
property regardless of which formula produces it.

## Detailed Findings

### F1 — Shares the `env_map3d.vert.glsl` missing-Y-flip defect (see `vulkan_env_map_test.cpp.audit.md` F1 for full analysis)

- Severity: HIGH
- Confidence: HIGH
- Category: correctness (production shader) / test-coverage (structural blind spot)
- Location/symbol: `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl:35`
- Evidence: identical to the full analysis in `vulkan_env_map_test.cpp.audit.md`'s F1.
- Why it matters for this file specifically: `World=View=Projection=Identity`, quad fills the
  viewport, only the center pixel is read (`readCenter()`, lines 66-72) — a vertical mirror of
  the render is invisible at the exact center, so this test cannot detect the missing flip.
- FNA/XNA comparison: N/A.
- Related files: see `vulkan_env_map_test.cpp.audit.md` for full cross-shader evidence; also
  affects `vulkan_environmentmapeffect_amount_one_test.cpp`,
  `vulkan_environmentmapeffect_combined_test.cpp`, and
  `vulkan_environmentmapeffect_eyeposition_test.cpp`.
- Suggested future action (not implemented by this audit): add the missing Y-flip to
  `env_map3d.vert.glsl`.

## Cross-File Observations

- `git log` corroborates Task 393 as a real, closed test-authoring task:
  `c952060f test(Task 393): verify EnvironmentMapEffect EnvironmentMapAmount=0 ignores cube map`,
  immediately preceding Task 394's real fix (`a5237f55`).
- This file's header comment explicitly and correctly forward-references the `amount_one` test
  for the discriminating case — cross-checked and confirmed that reference is accurate (see
  `vulkan_environmentmapeffect_amount_one_test.cpp.audit.md`).

## Missing or Weak Tests

None beyond the self-disclosed (and here, harmless) formula-discrimination limitation, which is
by design deferred to the sibling `amount_one` test.

## Positive Findings

- The test's own header comment accurately and proactively discloses the one thing this specific
  check cannot prove, rather than overclaiming coverage — verified accurate by this audit's own
  independent re-derivation.
- Choice of a maximally-contrasting cube color (bright green against an expected muted
  orange-brown result) is a sound test-design choice for an exclusion test.

## Final Assessment

A correct, honest, and well-scoped test; its only gap is the shared, structurally-undetectable
Y-flip issue (F1) common to this entire Vulkan `EnvironmentMapEffect` test family.
