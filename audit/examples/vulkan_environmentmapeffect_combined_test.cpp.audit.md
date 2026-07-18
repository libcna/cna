# Audit: examples/vulkan_environmentmapeffect_combined_test.cpp

## Metadata

- Source file: `examples/vulkan_environmentmapeffect_combined_test.cpp` (155 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — cross-feature `EnvironmentMapEffect` capstone,
  Vulkan backend (Task 399, combining Tasks 393-398)
- File type: standalone `Game`-subclass executable
  (`VulkanEnvironmentMapCombinedTest`), CTest-registered
- XNA/FNA relevance: direct — combines `EnvironmentMapAmount`, `EnvironmentMapSpecular`,
  `FresnelFactor`, `EyePosition` (derived), and a non-identity, non-uniform-scale `World`
- FNA reference: `HLSL/EnvironmentMapEffect.fx` (`PSEnvMapSpecular`: `color.rgb =
  lerp(color.rgb, envmap.rgb, pin.Specular.rgb); color.rgb += EnvironmentMapSpecular *
  envmap.a;`, `ComputeFresnelFactor`)
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`,
  `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl` /
  `env_map3d.frag.glsl`

## Purpose

Single, non-trivial scene combining everything the Task 393-398 series verified in isolation:
a translucent (`alpha=128`) solid-color cube map, `EnvironmentMapAmount=1`,
`EnvironmentMapSpecular=(0.4,0.4,0.4)`, the real FNA default `FresnelFactor=1` (enabled), a
genuine (non-identity) perspective camera (`CreateLookAt`/`CreatePerspectiveFieldOfView`), and a
non-uniform `World=CreateScale(2,1,1)`. The file itself contains no derivation comment (it
defers to `examples/easygl_environmentmapeffect_combined_test.cpp`'s fuller comment, per this
project's established cross-backend-sibling convention of keeping the full derivation in one
canonical file and pointing at it from the rest) — this audit independently re-derived the
expected value rather than trusting the deferred comment.

## Executive Verdict

**Needs attention** — the test's expected value is correct and was independently re-derived
end-to-end against the live Vulkan shader/effect code (not just checked against the referenced
EasyGL sibling's comment); it also inherits the batch-wide un-detectable `env_map3d.vert.glsl`
Y-flip defect (F1).

## Checklist Results

### API / XNA / FNA parity
All properties used (`EmissiveColor`, `EnvironmentMapAmount`, `EnvironmentMapSpecular`,
`FresnelFactor`, `World`, `View`, `Projection`) map correctly to FNA's `EnvironmentMapEffect`/
`IEffectMatrices` surface. `FresnelFactor=1.0f` (line 122) matches FNA's own real default (not
overridden away, deliberately exercising the Fresnel path per the file's Task 399 provenance).

### Behavioral correctness
Independently re-derived the full chain against the live `env_map3d.vert.glsl`/`.frag.glsl` and
`EnvironmentMapEffect::FillGpuDrawParams()`:
- `litRGB = (emissive+0)*diffuse(default 1,1,1) = (0.5,0.5,0.5)` (light diffuse zero by default,
  same as the other files in this batch). `texColor = kTex(200,100,50)/255`. `baseColor =
  (100,50,25)` (bytes).
- `combinedAlpha = DiffuseColor.a(1) * texColor.a(1) = 1`.
- Eye at world `(0,0,3)` (from `CreateLookAt`'s inverse translation), `World=Scale(2,1,1)`.
  Normal `(0,0,1)` transformed by `transpose(inverse(mat3(World)))`: since `World` is diagonal,
  its inverse-transpose is also diagonal (`(0.5,1,1)`), and the Z component (the only nonzero
  component of the object-space normal) is unaffected by the X/Y-only scale, so the world-space
  normal is still exactly `(0,0,1)` after normalization — this non-uniform scale was deliberately
  chosen (per the referenced EasyGL comment) to *not* perturb this particular quad's normal, so
  it isolates the rest of the pipeline from Task 398's separate normal-matrix fix.
  `worldPos` at the screen center ≈ `(0,0,0)` (object-space origin maps to world origin under
  this `World`, and the camera looks directly at the world origin, placing it at the exact
  center of a 1:1-aspect, symmetric-FOV viewport). `eyeVector = normalize((0,0,3)-(0,0,0)) =
  (0,0,1) = E`. `viewAngle = dot(E,N) = 1`. `blendFactor = pow(max(1-1,0),1)*1 = 0` exactly (not
  merely "negligible" as the referenced EasyGL comment approximates it — under this audit's
  idealized-center-pixel derivation it is exactly zero; the EasyGL comment's "~0.00004" accounts
  for real sub-pixel sampling offset, which does not change the byte-rounded result either way).
- With `blendFactor=0`: `mix(baseColor, envColor*combinedAlpha, 0) = baseColor = (100,50,25)`.
- `reflDir = reflect(-E,N) = reflect((0,0,-1),(0,0,1)) = (0,0,1)` → samples the cube's
  `PositiveZ` face, which (being a *solid*-color cube, all 6 faces set to the same
  `kTranslucentCube=(0,0,0,128)`) gives `envSample=(0,0,0,128/255≈0.502)` regardless of which
  face is actually hit — this test does not depend on correct face-selection, only on correct
  alpha handling.
- Specular add-on: `+ EnvironmentMapSpecular * envSample.a * combinedAlpha = (0.4,0.4,0.4) *
  0.502 * 1 ≈ (0.2008,0.2008,0.2008)` → `≈51` per channel.
- Total: `(100,50,25) + (51,51,51) = (151,101,76)`. Matches the test's asserted expectation
  `Color(151,101,76,255)` (line 130) exactly, and matches the referenced EasyGL sibling's own
  independently-stated derivation.

### Cross-file consistency
The alpha-scaling of the specular term (`envSample.a * combinedAlpha`, not just `envSample.a`)
was checked directly against FNA's `PSEnvMapSpecular`: `envmap = SAMPLE_CUBEMAP(...) * color.a;
... color.rgb += EnvironmentMapSpecular * envmap.a;` — i.e. FNA scales the *whole* cube sample
(including alpha) by `color.a` (`= DiffuseColor.a * texColor.a`, i.e. `combinedAlpha`) before
using its alpha in the specular add-on. `env_map3d.frag.glsl:52`'s
`envSample.a * combinedAlpha` reproduces this exactly (this is documented in the file as the
"Task 891" fix, and this audit confirms the fix is present and correctly applied).

## Detailed Findings

### F1 — Shares the `env_map3d.vert.glsl` missing-Y-flip defect (see `vulkan_env_map_test.cpp.audit.md` F1 for full analysis)

- Severity: HIGH
- Confidence: HIGH
- Category: correctness (production shader) / test-coverage (structural blind spot)
- Location/symbol: `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl:35`
- Evidence: identical to the full analysis in `vulkan_env_map_test.cpp.audit.md`'s F1.
- Why it matters for this file specifically: even though this test uses a genuine non-identity
  `World`/`View`/`Projection` (the most "realistic" scene in this batch), it still only reads the
  exact center pixel (`readCenter()`, lines 67-73), and the camera is deliberately aimed straight
  at the world origin (which maps to the exact center of the viewport) — so the same
  Y-negation-fixes-the-center-point argument applies: a vertical mirror of this render is
  invisible to this specific check. This is a strong illustration of why a "combined, more
  realistic scene" test is still not automatically immune to this defect class — a center-only
  readback undermines even an otherwise-sophisticated scene setup.
- FNA/XNA comparison: N/A.
- Related files: see `vulkan_env_map_test.cpp.audit.md` for full cross-shader evidence; also
  affects `vulkan_environmentmapeffect_amount_one_test.cpp`,
  `vulkan_environmentmapeffect_amount_zero_test.cpp`, and
  `vulkan_environmentmapeffect_eyeposition_test.cpp`.
- Suggested future action (not implemented by this audit): add the missing Y-flip to
  `env_map3d.vert.glsl`; for this specific file, consider also reading an off-center pixel once
  the flip is fixed, to make the "combined realistic scene" test earn its name against this
  defect class too.

## Cross-File Observations

- `git log` corroborates Task 399 as the real capstone: `ae649111 test(Task 399): cross-backend
  EnvironmentMapEffect capstone (Tasks 393-398 combined)`.
- This file's reliance on `examples/easygl_environmentmapeffect_combined_test.cpp` for its
  derivation comment was independently re-verified rather than trusted at face value — this
  audit re-derived the full formula chain from the live Vulkan shader source and confirms the
  referenced value is correct, not merely internally consistent with the EasyGL sibling's own
  claims.
- The non-uniform `World=Scale(2,1,1)` choice was confirmed (per the EasyGL comment's stated
  intent) to genuinely not interact with this quad's Z-aligned normal, so this test does not
  re-verify Task 398's normal-matrix fix — that remains that task's own dedicated test's
  responsibility, correctly scoped out here.

## Missing or Weak Tests

Beyond F1, none specific to this file — it is the intended "does everything compose" capstone,
not meant to isolate any single variable.

## Positive Findings

- The full multi-feature formula (lerp + alpha-scaled specular + Fresnel + EyePosition-driven
  reflection + non-identity, non-uniform World) was independently traced end-to-end and matches
  the asserted expected value exactly, to the byte.
- Correctly reuses a translucent cube (`alpha=128`) specifically to exercise the alpha-scaling
  interaction between the lerp target and the specular add-on (Task 891), rather than a
  fully-opaque cube that would leave that interaction untested.

## Final Assessment

A well-constructed, numerically-verified capstone test; its only defect is the batch-wide,
structurally-undetectable Y-flip issue (F1) shared with its 4 siblings.
