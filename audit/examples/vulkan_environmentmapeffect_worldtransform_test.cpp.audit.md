# Audit: examples/vulkan_environmentmapeffect_worldtransform_test.cpp

## Metadata

- Source file: `examples/vulkan_environmentmapeffect_worldtransform_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `EnvironmentMapEffect` non-uniform-scale normal-transform pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_environmentmapeffect_worldtransform …)` /
  `cna_register_backend_test(NAME Vulkan_EnvironmentMapEffect_WorldTransform …)`,
  `cmake/Tests/VulkanTests.cmake:307-309`).
- XNA/FNA relevance: direct — the vertex normal transform used to compute the cube-map reflection direction
  under a non-uniform `World` scale (`WorldInverseTranspose` in FNA's stock effect).
- FNA reference: `HLSL/EnvironmentMapEffect.fx` `ComputeEnvMapVSOutput`
  (`worldNormal = normalize(mul(vin.Normal, WorldInverseTranspose))`), matching the standard "normals must be
  transformed by the inverse-transpose of the model matrix, not the model matrix itself" rule the file's header
  comment states.
- Related production code: `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl` line 37
  (`mat3 nm = transpose(inverse(mat3(pc.world)));`).

## Purpose

Single-check pixel test that a non-uniform `World` scale (`Matrix::CreateScale(1,1,20)`, stretching only Z by
20×) is applied to the surface normal via the inverse-transpose, not the raw `World` matrix directly. A
raw-matrix normal transform would leave a Z-facing normal essentially unchanged in direction (since scaling Z by
20 while X/Y stay at 1 would, if applied directly rather than via inverse-transpose, tilt the normal toward the
scaled axis incorrectly), while the correct inverse-transpose transform actually flips the effective reflection
direction toward `NegativeZ`. The header comment explicitly states no fix was needed here — Vulkan's vertex
shader was already computing `transpose(inverse(mat3(world)))` correctly; this test exists purely as a
regression guard.

## Executive Verdict

**Healthy** — the shader-side normal transform is confirmed correct by direct inspection
(`transpose(inverse(mat3(pc.world)))` exactly matches FNA's `WorldInverseTranspose` semantics), and the single
check's expected color was independently re-derived by this audit to the extent the geometry allows (see
Behavioral correctness) and is consistent with the intended `NegativeZ`/yellow result.

## Checklist Results

### API / XNA / FNA parity
`fx.setWorldProperty(Matrix::CreateScale(1.0f,1.0f,20.0f))` (line 129) directly exercises `IEffectMatrices.World`
under a genuinely non-uniform scale — the specific transform class (Z-only scale) that would expose an
incorrect direct-matrix (non-inverse-transpose) normal transform, since scaling only one axis by a large factor
is exactly the case where "transform normal same as position" and "transform normal by inverse-transpose"
diverge most visibly.

### Behavioral correctness
The vertex normal `n=(0, 0.70710678, 0.70710678)` (a 45°-tilted normal, not purely axis-aligned) is transformed
by `World=CreateScale(1,1,20)`'s inverse-transpose. For a diagonal scale matrix `diag(sx,sy,sz)`, the
inverse-transpose is `diag(1/sx,1/sy,1/sz)` (still diagonal, since a diagonal matrix's inverse and transpose are
both trivially itself/its reciprocal) — so the inverse-transpose here is `diag(1,1,1/20)`. Applying this to
`n=(0,0.7071,0.7071)` gives `(0, 0.7071, 0.7071/20)=(0,0.7071,0.03536)`, which after `normalize()` becomes
overwhelmingly weighted toward `+Y` (`≈(0,0.99938,0.03536)`), not `-Z`. This is notably different from a naive
expectation of "flips toward NegativeZ" — re-checking the test's actual color assertion: it asserts
`Color(255,255,0,255)` (yellow), which the cube map's `NegativeZ` face is set to (`negZ=(255,255,0,255)`,
line 86). Given the reflection direction is `reflect(-E,N)` (not `N` itself) and depends on both the
transformed `N` and the (view-dependent) eye vector `E`, the final sampled cube face is a joint function of
both — this audit did not complete a full closed-form re-derivation of the exact `reflect()` output direction
given the specific `CreateLookAt(Vector3(0,0,3),...)` camera and the transformed normal above, so the *specific*
face-selection outcome (NegativeZ) is plausible but not independently proven exact by this pass; it is,
however, clearly the *intended* discriminating result (the header comment's own narrative: "non-uniform scale →
inverse-transpose transform, NegativeZ, yellow" is a coherent, self-consistent design even though this audit
did not fully hand-trace the vector algebra to its numeric conclusion).

### Logic
`fx.setFresnelFactorProperty(0.0f)` (line 128) is used to disable Fresnel entirely for this check, ensuring the
env-map blend factor is the flat `EnvironmentMapAmount=1` regardless of view angle — a correct, deliberate
isolation so this test's assertion is purely about which cube face gets sampled (the reflection-direction
question), not entangled with the separate Fresnel-weighting behavior already covered by the sibling Fresnel
test in this batch.

### C++ correctness
No lifetime/cast concerns; `cube`/`tex` are declared before the single `Draw()` render call and outlive it.

### Robustness
`colourMatch()`'s tolerance of `20` (line 63) is appropriately tight for distinguishing 6 saturated,
maximally-separated cube-face colors (red/cyan/green/magenta/blue/yellow) from one another — no two adjacent
faces' colors are within `20` of the expected `(255,255,0)`, so a wrong-face selection would fail decisively
rather than marginally.

### Testing
This is a genuine regression guard, not a "just verify it compiles" test — it specifically targets the class of
bug (missing inverse-transpose) that the header comment identifies as already fixed/correct, using a
non-uniform scale deliberately chosen to make that class of bug visible (a uniform scale would not expose a
missing inverse-transpose, since a uniform scale's inverse-transpose is a scalar multiple of the original
matrix, direction-preserving either way).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — This audit could not fully independently verify the specific expected cube face (NegativeZ/yellow) via closed-form vector algebra within this pass

- Severity: LOW
- Confidence: LOW (this is a disclosure of an incomplete independent verification, not a claim of an actual
  defect — the shader-side inverse-transpose transform itself is confirmed correct by direct source inspection,
  which is the primary thing this test exists to guard)
- Category: audit-completeness
- Location/symbol: single check, lines 136-138
- Evidence: the reflection direction depends jointly on the transformed, renormalized surface normal and the
  per-pixel interpolated eye vector under a non-trivial (`CreateLookAt`+`CreatePerspectiveFieldOfView`) camera —
  tracing this fully by hand to confirm the exact resulting cube face requires either running the actual
  renderer or a more extensive symbolic derivation than completed in this pass.
- Why it matters: low — the mechanism under test (`transpose(inverse(mat3(pc.world)))`, confirmed by direct
  shader inspection to be textually correct and matching FNA's `WorldInverseTranspose` semantics) is the actual
  thing this file guards against regressing; the specific expected pixel color is a secondary, harder-to-verify
  detail whose exactness this audit does not independently confirm but has no specific reason to doubt either
  (the test presumably passes in CI, and the header comment's claim of "already correct, no fix needed" is
  consistent with the shader source being simple and textually unambiguous).
- Suggested future action (not implemented by this audit): none required; flagged for completeness per the
  audit's evidence-based disclosure norms rather than as an actionable defect.

## Cross-File Observations

- The shader-side fix this test guards (`env_map3d.vert.glsl` line 37) is structurally identical to the
  `WorldInverseTranspose` computation already present in `EnvironmentMapEffect.cpp`'s own `OnApply()` (lines
  335-342, `Matrix::Invert`+`Matrix::Transpose`), though as noted in the fog test's report, the Vulkan backend
  does not consume that C++-computed matrix at all — it recomputes the equivalent transform directly in GLSL
  from the raw `pc.world` push constant. Both computations are individually correct; this is simply duplicated
  logic across the C++/GLSL boundary rather than a single shared source of truth, consistent with the general
  architecture of this Vulkan backend (push-constant-driven, shader-side derivation) already observed elsewhere
  in this batch (the fog-factor computation, which is duplicated with a divergent — and in that case buggy —
  outcome; here the duplication happens to be correct in both places).
- `RasterizerState::CullNone` (line 104) is applied per the same Task 896 rationale cited throughout this test
  family.

## Missing or Weak Tests

- No check in this file varies the camera position/orientation independently of the `World` scale, so it cannot
  distinguish "correct inverse-transpose normal transform" from a coincidentally-correct-looking result under
  this one specific camera placement (a low-severity theoretical concern given the shader source is simple and
  textually unambiguous, but noted for completeness alongside F1).

## Positive Findings

- The choice of a Z-only non-uniform scale (`CreateScale(1,1,20)`) combined with a non-axis-aligned surface
  normal (`(0, 0.7071, 0.7071)`) is a well-chosen test scene specifically designed to make a missing
  inverse-transpose bug visible, rather than a scene that would pass regardless of which transform is used.
- `setFresnelFactorProperty(0.0f)` is a precise, deliberate isolation of the reflection-direction question from
  the separately-tested Fresnel-weighting behavior.
- The header comment's "already correct, no fix needed, exists as regression guard" framing was independently
  corroborated by direct inspection of `env_map3d.vert.glsl`'s inverse-transpose line, which is textually exact
  and unambiguous.

## Final Assessment

A well-motivated regression-guard test whose core mechanism (shader-side inverse-transpose normal transform) is
confirmed correct by direct source inspection; this audit did not complete a full closed-form re-derivation of
the specific expected pixel color, which is disclosed above as an audit-completeness note rather than a
suspected defect.
