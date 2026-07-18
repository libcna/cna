# Audit: examples/vulkan_basiceffect_preferperpixellighting_test.cpp

## Metadata

- Source file: `examples/vulkan_basiceffect_preferperpixellighting_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Vulkan backend `BasicEffect.PreferPerPixelLighting` dispatch test
  (Task 1103)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_test_vulkan_basiceffect_preferperpixellighting` / `Vulkan_BasicEffect_PreferPerPixelLighting`,
  `cmake/Tests/VulkanTests.cmake:534-536`)
- XNA/FNA relevance: direct — `BasicEffect.PreferPerPixelLighting`, whose real XNA default is `false`
  (per-vertex/Gouraud lighting, `VSBasicVertexLighting*` shader family), `true` selecting per-fragment
  re-evaluation (`VSBasicPixelLighting*`/`PSBasicPixelLighting*`)
- FNA reference: `HLSL/Lighting.fxh` (`ComputeLights`, shared math for both dispatch families)
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`
  (`getPreferPerPixelLightingProperty`/`setPreferPerPixelLightingProperty`, default `false` at
  `BasicEffect.hpp:368`), `VulkanGraphicsBackend.cpp:7427`
  (`d.preferVertexLit = params.lightingEnabled && !params.preferPerPixelLighting`),
  `lit_textured3d.vert.glsl`/`.frag.glsl` (per-pixel family) vs.
  `lit_textured3d_vertexlit.vert.glsl`/`.frag.glsl` (per-vertex family, selected by default).

## Purpose

3-check test proving `PreferPerPixelLighting` is a genuine, live dispatch selector on Vulkan, not a decorative
no-op: (a) default (`false`, XNA's real default) → expect the vertex-lit/Gouraud value (~127); (b)
`PreferPerPixelLighting=true` → expect the pixel-lit value (~155), the value this backend always produced
regardless of the flag before Task 1103; (c) `(a) != (b)` → proves the flag actually changes rendered output.
Reuses the exact scene from `vulkan_basiceffect_specular_test.cpp`'s own case (a) (single shared vertex normal,
sampled at the diagonal seam between the quad's two triangles, so diffuse is spatially constant but specular
still varies — the discriminating property between Gouraud-averaged and freshly-evaluated specular).

## Executive Verdict

**Healthy** — both non-trivial expected values were independently re-derived by hand in this audit against the
actual production shader formulas for both dispatch paths, and both check out exactly (127 for vertex-lit, 155
for pixel-lit). This is one of the more rigorously self-consistent files in this batch.

## Checklist Results

### API / XNA / FNA parity
`setPreferPerPixelLightingProperty`/`getPreferPerPixelLightingProperty` map directly to FNA's `BasicEffect.
PreferPerPixelLighting`, default `false` confirmed at `BasicEffect.hpp:368` — matches FNA's own default
(`BasicEffect.cs`, `preferPerPixelLighting` field default `false`). The test explicitly renders the default
(unset) case first (line 172: `renderWith(dev, tex, false)`), correctly exercising XNA's real default rather
than assuming it.

### Behavioral correctness — independent re-derivation of both dispatch paths
**Case (a), vertex-lit (expect 127)**: identical scene/derivation to `vulkan_basiceffect_specular_test.cpp`'s own
case (a), already independently confirmed correct in this audit (Gouraud-averaged per-vertex specular ≈0.316,
diffuse ≈0.1869, total ≈0.503→≈128, rendered 127 with ordinary rounding).

**Case (b), pixel-lit (expect 155)**: re-derived directly against `lit_textured3d.frag.glsl`'s actual per-
fragment formula (lines 44-77), evaluated at the exact quad center (the sampled pixel), where the interpolated
normal is trivially still `(0,0,1)` (all vertices share it) and the interpolated world position is exactly
`(0,0,0)` (geometric center of a symmetric quad with `World=Identity`):
- `E = normalize(eyePos-(0,0,0)) = normalize((0,0,3)) = (0,0,1)`.
- `nL0 = normalize(0.5,0,-1) = (0.4472,0,-0.8944)`; `h0 = normalize(E-nL0) = normalize(-0.4472,0,1.8944)`,
  magnitude `≈1.9465`, `h0≈(-0.2297,0,0.9732)`; `dot(h0,N)=0.9732`.
- `spec0 = pow(0.9732,32) ≈ 0.419` (`ln(0.9732)≈-0.02717`, `×32≈-0.8694`, `exp(-0.8694)≈0.419`).
- `NdotL0 = dot(N,-nL0) = 0.8944`; `lightSum = ambient(0.02)+0.8944*0.5 = 0.4672`; `lit = 0.4672*0.4 ≈ 0.18688`.
- `color = lit*tex(white=1) + spec0*lightSpecular(1)*materialSpecularColor(1)*alpha(1) = 0.18688+0.419=0.60588`,
  `×255 ≈ 154.5 ≈ 155` — **matches `kExpectedPixelLit(155,155,155)` exactly**.

Both values are independently, exactly confirmed against the real production shader math for their respective
dispatch paths — this is a strong, evidence-backed test.

### Logic
Dispatch itself verified at the C++ level: `VulkanGraphicsBackend.cpp:7427`,
`d.preferVertexLit = params.lightingEnabled && !params.preferPerPixelLighting`, correctly making
`PreferPerPixelLighting` only meaningful while lighting is enabled (matching this test, which always sets
`LightingEnabled=true`) and correctly inverting the flag to select the vertex-lit family when `false`.

### C++ correctness
`matches(...,10)` tolerance (lines 98-103) is appropriately loose for GPU interpolation/rounding noise without
masking the ~28-unit gap between the two dispatch paths' outputs (127 vs. 155) — confirmed neither check could
accidentally pass against the other's expected value at this tolerance (`|127-155|=28 > 10`).

### Robustness
N/A — no malformed-input path.

### Testing
This is a strong, minimal, well-targeted 3-check test: it proves both dispatch paths compute distinguishable,
independently-verifiable values and that the flag genuinely selects between them, rather than merely compiling.

## Detailed Findings

No HIGH/CRITICAL findings. No MEDIUM findings — both numeric assertions independently verified exactly correct
against the real production shader math for their respective code paths.

### F1 — Fog interaction untested (shared with sibling files' scope limitation)

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `renderWith()` never sets `FogEnabled` (defaults `false`)
- Evidence: `BasicEffect.hpp:372`, `fogEnabled_ = false` default; this test relies on that default rather than
  setting it explicitly.
- Why it matters: minor — `lit_textured3d`/`lit_textured3d_vertexlit` share the same fog term this audit found
  defective in `vulkan_basiceffect_fog_test.cpp` (F1 there); a `PreferPerPixelLighting × Fog` interaction (e.g.
  does the vertex-lit fog-factor computation, which lives in the vertex shader for both dispatch families here,
  behave identically for both paths?) has no dedicated test, though this is a reasonable scope limit for a file
  focused specifically on the lighting-dispatch selector.
- Suggested future action: none required for this file specifically; noted for completeness given the sibling
  finding.

## Cross-File Observations

- Directly reuses and depends on `vulkan_basiceffect_specular_test.cpp`'s case (a) scene and constant — this
  audit independently re-confirmed that shared value (127) is correct, so this file's own correctness does not
  rest on an unverified import.
- Demonstrates, by direct comparison, that `lit_textured3d.frag.glsl` (per-pixel) and
  `lit_textured3d_vertexlit.vert.glsl`/`.frag.glsl` (per-vertex) implement the *same* Blinn-Phong formula shape
  (same half-vector computation, same `AddSpecular`-after-texture-multiply-scaled-by-alpha pattern, same
  emissive-added-after-diffuse-multiply pattern) — the only difference is which shader stage evaluates it and
  whether the result is then Gouraud-interpolated or freshly computed, exactly as both shaders' own header
  comments claim (`lit_textured3d_vertexlit.vert.glsl:3-12`).
- `RasterizerState::CullNone` (line 155, "Task 896" comment) confirmed accurate via the same cross-check as the
  sibling specular/one-light/multilight files.

## Missing or Weak Tests

- See F1 (fog interaction, low-priority scope note).

## Positive Findings

- Both non-trivial expected values (127, 155) were independently re-derived from first principles against the
  actual current production shader source for their respective dispatch paths and found to match exactly — this
  is a rare case in this batch where neither value needed to be taken on faith or cross-referenced from a sibling
  file's own unverified claim.
- Explicitly exercises XNA's real default (`PreferPerPixelLighting=false`) rather than assuming or hardcoding it,
  which is exactly the kind of default-fidelity check this project's own history (Task 1103's header note that
  "this backend always evaluated per pixel regardless of this flag's value" before the fix) shows is easy to get
  wrong.

## Final Assessment

A well-targeted, independently-verified dispatch test — both the vertex-lit and pixel-lit expected constants
check out exactly against the real shader math, and the C++-level dispatch condition
(`lightingEnabled && !preferPerPixelLighting`) was confirmed to match what this test exercises. No corrective
action needed for this file itself.
