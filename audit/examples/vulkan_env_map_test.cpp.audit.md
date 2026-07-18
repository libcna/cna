# Audit: examples/vulkan_env_map_test.cpp

## Metadata

- Source file: `examples/vulkan_env_map_test.cpp` (147 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `EnvironmentMapEffect` cube-map reflection smoke
  test, Vulkan backend (Task 136, an early/simple test predating the rigorous Task 393-399 suite
  audited alongside it in this batch)
- File type: standalone `Game`-subclass executable (`VulkanEnvMapTest`), CTest-registered
- XNA/FNA relevance: direct — `EnvironmentMapEffect` (`EmissiveColor`/`DiffuseColor`/
  `EnvironmentMapAmount`/`EnvironmentMapSpecular`/`Texture`/`EnvironmentMap`)
- FNA reference: `HLSL/EnvironmentMapEffect.fx` (`PSEnvMap`/`ComputeEnvMapVSOutput`)
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`,
  `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl` /
  `env_map3d.frag.glsl`, `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`GetOrCreatePipelineEnvMap3D`, `GetOrCreateEnvMapDescSet`)

## Purpose

Single-scenario pixel test: `EmissiveColor=(1,0,0)`, `DiffuseColor=(1,1,1)`, all-white 1×1
diffuse texture, all-white 1×1×6 cube map, `EnvironmentMapAmount=0` (cube contribution
suppressed), default (zero-diffuse, no non-default lights). Expected result: pure red
(`litRGB=(1,0,0)`). Pass condition is a loose threshold (`R>=200, G<=50, B<=50`), not an exact
pixel match.

## Executive Verdict

**Needs attention** — the test itself correctly proves the env-map pipeline draws and shades at
all, and its own math is correct, but (a) it is a substantially weaker smoke test than its
Task-393-399 successors also in this batch, and (b) this file — together with all four
`vulkan_environmentmapeffect_*` sibling files in this same batch — exercises a Vulkan vertex
shader (`env_map3d.vert.glsl`) that is missing the Vulkan-vs-OpenGL Y-flip every other core Vulkan
3D vertex shader in this codebase applies, a real rendering-correctness defect that this whole
family of center-pixel-only tests is structurally incapable of detecting (see F1, filed against
this batch's production code, not against the test's own logic).

## Checklist Results

### API / XNA / FNA parity
All property setters used (`setEmissiveColorProperty`/`setDiffuseColorProperty`/
`setEnvironmentMapAmountProperty`/`setEnvironmentMapSpecularProperty`/`setTextureProperty`/
`setEnvironmentMapProperty`) map correctly to FNA's `EnvironmentMapEffect` surface.

### Behavioral correctness
Re-derived against the live `env_map3d.frag.glsl`: light0 diffuse defaults to `(0,0,0)`
(`DirectionalLight`'s default ctor leaves `diffuseColor_` at `Vector3{}`, confirmed at
`DirectionalLight.hpp:77` and its own "all colors black" doc comment), so `lightSum=0` regardless
of `DirectionalLight0`'s `Enabled` state. `litRGB = (emissive+0)*diffuse = (1,0,0)*(1,1,1) =
(1,0,0)`. `texColor=white(1,1,1,1)` so `baseColor = litRGB*texColor = (1,0,0)`. With
`envMapAmount=0`, `blendFactor = pow(...)*0 = 0` regardless of the Fresnel term, so
`mix(baseColor, envColor, 0) = baseColor = red`, and `envMapSpecular=(0,0,0)` contributes nothing
further. Result `(255,0,0)` comfortably satisfies the test's loose pass condition
(`R>=200,G<=50,B<=50`). The comment's simplified formula ("litRGB = (emissive +
light0Diffuse*NdotL) * diffuse", omitting light1/light2) is accurate *for this specific test's
configuration* (only `DirectionalLight0` is ever touched, and even it stays at its zero-diffuse
default) even though the live shader (`env_map3d.frag.glsl:33-39`) actually sums all three
lights — not a stale claim, just a simplification valid for this scenario.

### Testing
The pass condition (`R>=200 && G<=50 && B<=50`) only distinguishes "rendered something red" from
the scene's own green clear color (`Color(0,255,0,255)`, line 85) — a coarse smoke test. Compare
to the `Task 393-399` sibling files in this same batch (`vulkan_environmentmapeffect_amount_one/
zero/combined/eyeposition_test.cpp`), which assert exact expected pixel triples derived to
single-digit precision. This file predates that rigor (Task 136 vs. Task 393+) and should be
read as a basic "does the env-map pipeline draw and shade at all" regression guard, not as
numeric verification of the blend formula.

## Detailed Findings

### F1 — `env_map3d.vert.glsl` is missing the Vulkan NDC Y-flip every other core Vulkan 3D vertex shader applies; no test in this file (or its 4 siblings in this batch) can detect it

- Severity: HIGH
- Confidence: HIGH
- Category: correctness (production shader) / test-coverage (structural blind spot)
- Location/symbol: `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl:35`
  (`gl_Position = pc.mvp * vec4(aPos, 1.0);` — no following Y negation)
- Evidence: every other core Vulkan 3D vertex shader in this backend applies a manual Y-flip
  immediately after computing `gl_Position`, with an explicit comment explaining why:
  `colored3d.vert.glsl:33` (`pos.y = -pos.y; // Vulkan NDC Y is inverted vs OpenGL`),
  `textured3d.vert.glsl:31`, `colored_textured3d.vert.glsl:32`, `dual_texture3d.vert.glsl:36`,
  `dual_texture_colored3d.vert.glsl:33`, `alpha_test3d.vert.glsl:30`,
  `alpha_test_colored3d.vert.glsl:29`, `lit_textured3d.vert.glsl:48`,
  `lit_textured3d_vertexlit.vert.glsl:57`, `colored3d_legacy.vert.glsl:28`,
  `skinned3d.vert.glsl:59`, `skinned3d_color.vert.glsl:58`,
  `skinned3d_vertexlit.vert.glsl:60`, `skinned3d_vertexlit_color.vert.glsl:59` all do this.
  `env_map3d.vert.glsl` (used by every `EnvironmentMapEffect` draw on this backend) does not.
  Checked for a compensating mechanism and found none: `FillEnvMapPushConst()`
  (`VulkanGraphicsBackend.cpp:4346-4351`) writes the exact same CPU-computed `world*view*
  projection` matrix (via `ToColumnMajor`, no flip) that `FillExtPushConst()` writes for the
  shaders that *do* flip in-shader; the viewport setup (`VulkanGraphicsBackend.cpp:6775-6790`
  and `6736-6741`) uses ordinary positive `height`, not the negative-height-viewport technique
  that would make an in-shader flip unnecessary; and `Matrix`/`GraphicsDevice::ExtractMatrices`
  contain no Vulkan-specific adjustment. (The same gap also exists in `pbr3d.vert.glsl`,
  `pbr3d_skinned.vert.glsl`, and `instanced3d.vert.glsl` — out of scope for this batch's 8 files,
  but corroborating evidence that this is a systemic gap in a cluster of newer pipelines, not a
  one-off in `env_map3d.vert.glsl` specifically.)
- Why it matters: without the flip, any `EnvironmentMapEffect` scene rendered via the Vulkan
  backend renders mirrored top-to-bottom relative to every other stock effect on the same
  backend (and relative to the other graphics backends, which is presumably why the flip
  convention exists at all — to give backends visual parity). This is wrong behavior on an
  extremely common path (any non-trivial `EnvironmentMapEffect` scene), not an edge case.
  Crucially, **this file's own test (and all four sibling `vulkan_environmentmapeffect_*_test.cpp`
  files in this batch) cannot detect it**: negating `gl_Position.y` is an involution that fixes
  the exact center of the viewport, and every one of these tests reads only the single center
  pixel (`Rectangle(kSize/2, kSize/2, 1, 1)`) of a quad that fills the entire viewport — so a
  vertical mirror of the rendered image leaves the sampled pixel's value completely unchanged
  regardless of how asymmetric the scene's World/View/lighting is. The eyeposition test in this
  same batch (which does vary the camera to a genuinely asymmetric position) is exactly the kind
  of test that *could* have caught this if it sampled an off-center pixel instead of the exact
  center.
- FNA/XNA comparison: N/A directly (this is a Vulkan-backend clip-space convention bug, not an
  XNA API/behavior question) — but it does mean the Vulkan backend's `EnvironmentMapEffect`
  output is not visually equivalent to FNA's (or to this project's other backends') output for
  any scene that isn't perfectly symmetric about the horizontal midline.
- Related files: `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl` (the actual
  defect site; not itself in this batch's audit list, but directly implicated by all 5
  `EnvironmentMapEffect`-related files in this batch). Also affects
  `vulkan_environmentmapeffect_amount_one_test.cpp`,
  `vulkan_environmentmapeffect_amount_zero_test.cpp`,
  `vulkan_environmentmapeffect_combined_test.cpp`, and
  `vulkan_environmentmapeffect_eyeposition_test.cpp` — see each of those reports for the same
  finding recorded against their own coverage.
- Suggested future action (not implemented by this audit — audit-only task): add `pos.y = -pos.y;`
  (or `gl_Position.y = -gl_Position.y;`) to `env_map3d.vert.glsl` to match the rest of the Vulkan
  3D shader suite, and consider a follow-up test that samples an off-center, vertically-asymmetric
  pixel (e.g. a scene with a light or texture gradient that varies by screen Y) specifically to
  catch this class of defect, since none of the current center-pixel tests can.

## Cross-File Observations

- This is the same production-code defect reported identically (with file-specific "why this
  test can't catch it" framing) in the four `vulkan_environmentmapeffect_amount_one/zero/
  combined/eyeposition_test.cpp` reports in this batch — reported here in full since this is the
  first file in manifest order to exercise `env_map3d.vert.glsl`.
- Function names cited in the header comment (`GetOrCreatePipelineEnvMap3D`,
  `GetOrCreateEnvMapDescSet`) were verified to exist verbatim in
  `VulkanGraphicsBackend.cpp` (lines 4233 and 4180 respectively) — not stale.

## Missing or Weak Tests

- The pass threshold is loose enough that it would not catch a moderately-wrong blend formula
  (e.g. an unintended ~20% desaturation would still pass). The Task 393-399 suite (also in this
  batch) supersedes this concern for the blend-formula question specifically; this file's
  continued value is as a minimal "does this pipeline draw non-garbage at all" smoke test.
- No test in this file (or its siblings) samples an off-center pixel, which is precisely why F1
  has gone undetected.

## Positive Findings

- The test's own math (emissive/diffuse/amount-zero interaction) is correct and was independently
  re-derived against the live shader.
- `TextureCube` construction (`TextureCube(device, 1, false, SurfaceFormat::Color)`) uses the
  correct parameter order per `TextureCube.hpp:35`.

## Final Assessment

The test itself does what its modest scope claims (proves the env-map pipeline shades at all),
but this audit's cross-check into the production shader it exercises surfaced a real,
un-caught Y-flip defect (F1) that is common to this file and all four `EnvironmentMapEffect`
sibling tests in this batch.
