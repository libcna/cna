# Audit: examples/vulkan_alphatest_vertexcolor_test.cpp

## Metadata

- Source file: `examples/vulkan_alphatest_vertexcolor_test.cpp` (168 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `AlphaTestEffect.VertexColorEnabled` fix verification
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_alphatest_vertexcolor …)` /
  `cna_register_backend_test(NAME Vulkan_AlphaTest_VertexColor …)`, `cmake/Tests/VulkanTests.cmake:540-542`).
- XNA/FNA relevance: direct — `AlphaTestEffect.VertexColorEnabled`, and specifically whether the
  *combined* alpha (texture × vertex × diffuse) or only the diffuse alpha gates the alpha test.
- FNA reference: `HLSL/AlphaTestEffect.fx::VSAlphaTestVc()` (`vout.Diffuse *= vin.Color` — vertex color
  multiplies into the same `Diffuse` channel the pixel shader samples alpha from) confirms combined alpha
  is the correct FNA semantics, not diffuse-alone.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp`
  (`setVertexColorEnabledProperty()` lines 162-170, `FillGpuDrawParams()` line 318:
  `p.vertexColorEnabled = vertexColorEnabled_`), `src/CNA/Internal/Backends/Vulkan/shaders/
  alpha_test_colored3d.vert.glsl` (the stride-24 vertex-color-aware variant added by this fix),
  `alpha_test3d.frag.glsl` (shared, unmodified fragment shader that reads `outColor.a` post-multiply).
- git corroboration: `ab34b5c2`/`60ffbdbd` "fix(Task 887): AlphaTestEffect.VertexColorEnabled ignored on
  Vulkan/Bgfx" matches this file's header comment ("Task 887... found this exact gap on Vulkan/Bgfx:
  their alpha-test pipeline/shader only ever declared position+texcoord vertex inputs, never a color
  attribute").

## Purpose

Verifies the Task 887 fix that gave the Vulkan alpha-test pipeline a genuine vertex-color-aware shader
variant. Uses a combined-alpha value (`VertexColor.A=200/255 * EffectAlpha=0.8 = 160/255`) deliberately
chosen so that: (a) it differs materially from the diffuse-alone alpha (`0.8*255=204/255`), and (b) two
different `ReferenceAlpha` values (100 and 180) straddle the combined value (160) while both sitting on
the *same side* of the diffuse-alone value (204) — meaning if the bug regressed (vertex color ignored),
case B (`reference=180`) would incorrectly pass instead of being discarded.

## Executive Verdict

**Healthy** — this audit independently re-derived the expected RGB output from the
`inColor * diffuseColor` vertex-shader multiply (`alpha_test_colored3d.vert.glsl:32`) and the combined-
alpha gating value used by the fragment shader's `clip()`, and both match the file's asserted expectations
exactly. The two-`ReferenceAlpha`-value test design is a genuinely discriminating regression guard against
the specific historical bug, not an arbitrary threshold choice.

## Checklist Results

### API / XNA / FNA parity
`setVertexColorEnabledProperty(true)` (line 93) is the correct FNA `VertexColorEnabled` surface.
`VertexPositionColorTexture` (stride-24) is the correct XNA vertex type for this combination
(position+color+texcoord).

### Behavioral correctness — full re-derivation
`kVertexColor=(200,100,50,200)`, `kDiffuse=(0.6,0.4,0.8)`, `kEffectAlpha=0.8`.
Premultiplied `diffuseColor` param (`AlphaTestEffect.cpp` OnApply/FillGpuDrawParams formula,
`(DiffuseColor*Alpha, Alpha)`) = `(0.48, 0.32, 0.64, 0.8)`.
`alpha_test_colored3d.vert.glsl:32`: `fragTint = inColor * diffuseColor` (component-wise, `inColor`
normalized to `[0,1]`):
- R: `(200/255)*0.48 = 0.7843*0.48 = 0.3765 → *255 = 96.0`
- G: `(100/255)*0.32 = 0.3922*0.32 = 0.1255 → *255 = 32.0`
- B: `(50/255)*0.64 = 0.1961*0.64 = 0.1255 → *255 = 32.0`
Texture is white `(1,1,1,1)` (identity), so `outColor = fragTint` unchanged → `(96,32,32)` — matches
`kExpectedRgb(96,32,32)` exactly.
Combined alpha: `(200/255)*0.8 = 0.7843*0.8 = 0.6275 → *255 ≈ 160`, matching the file's own stated
"160/255" derivation (comment, lines 134/140).
- **Case A** (`reference=100`, `AlphaFunction=Greater`): `x = (100+0.5)/255 = 0.3941`; shader keeps when
  `a ≥ x` (per the `Greater` `z=-1,w=1` encoding re-derived in this batch's sweep-test report):
  `0.6275 ≥ 0.3941` → keep. Matches "passes" and `kExpectedRgb`.
- **Case B** (`reference=180`): `x = (180+0.5)/255 = 0.7078`; `0.6275 < 0.7078` → discard. Matches
  `kBlack` (discarded, clear color persists).
- **Regression check**: if vertex-color alpha were ignored (bug scenario), combined alpha would instead be
  the diffuse-alone `Alpha=0.8*255=204/255=0.8`. At `reference=180`, `x=0.7078`; `0.8 ≥ 0.7078` → would
  incorrectly *pass* instead of discard. This confirms Case B is a real, non-vacuous regression guard for
  exactly the bug this file's header describes.

### Logic
`renderWith()` (lines 88-116) parameterizes `referenceAlpha` per call while reusing the same vertex data
and texture across both cases — clean separation of the one variable under test (the alpha-test threshold)
from everything else.

### C++ correctness
The retry-loop comment at lines 111-113 correctly notes the loop's "skip blank frames" check
(`R||G||B != 0`) also happens to be satisfied by the discarded case's own black clear color being
indistinguishable from a genuine blank frame — and correctly reasons that this is harmless because the
loop still returns whatever was last read (which is the correct black value either way). This is accurate:
worth noting as good self-aware commenting, not a defect.

### Robustness
Both directions of the discrimination (vertex color inflating or deflating alpha) are implicitly covered
by choosing a combined value (160) below the diffuse-alone value (204) — a vertex alpha >255 isn't
possible, so only the "vertex color legitimately lowers the passed alpha" direction is exercised. This is
the only direction the real bug (vertex color ignored) could manifest as, so it's a complete, non-redundant
test for this specific defect.

### Testing
Two assertions, both genuinely discriminating; no "compiles and doesn't crash" filler.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Only one `VertexColor` value is exercised; a vertex-color RGB regression (as opposed to alpha) is covered by the RGB assertion but a partial regression (e.g. only alpha wired, RGB left at diffuse-alone) is not separately isolated
- Severity: LOW
- Confidence: MEDIUM
- Category: test-coverage
- Location: `kVertexColor(200,100,50,200)` (line 41), used unchanged for both cases
- Evidence: Case A's RGB check (`kExpectedRgb`) does cover "did vertex-color RGB get multiplied in", and
  Case B's alpha check covers "did vertex-color alpha get multiplied in" — so both are technically
  covered, but by *different* assertions on *different* cases rather than a single test that varies vertex
  color across multiple distinct values to rule out a coincidental match.
- Why it matters: minor — the two assertions already jointly cover the defect space Task 887 actually
  found (shader lacked a color attribute at all, not "reads color but ignores one channel"), so this is a
  theoretical completeness nit rather than a practical gap given the documented bug shape.

## Cross-File Observations

- This is the vertex-color-aware counterpart to `vulkan_alphatest_null_texture_test.cpp` (no-vertex-color)
  and `vulkan_alphatest_comparefunction_sweep_test.cpp` (no vertex color, sweeps `CompareFunction`
  instead) — together the three files in this batch give reasonably orthogonal coverage of
  `AlphaTestEffect`'s texture/vertex-color/compare-function axes, though none of the three combines *all
  three* variables varying simultaneously in one scene.
- The dedicated `alpha_test_colored3d.vert.glsl` shader this test validates was a genuinely new file added
  by the Task 887 fix (not a modification of the existing `alpha_test3d.vert.glsl`), which is the correct
  approach given Vulkan's static vertex-input-attribute binding model (stride-20 vs. stride-24 inputs
  cannot share one vertex shader without either variant or runtime branching) — an architecturally sound
  fix, not a stopgap.

## Missing or Weak Tests

See F1 — low priority; the existing two assertions already cover the documented defect completely.

## Positive Findings

- Full independent re-derivation of the expected RGB and both alpha-threshold outcomes matches the file's
  own asserted values exactly, including confirming the specific mechanism (`ReferenceAlpha=180` straddles
  combined-alpha but not diffuse-alone-alpha) that makes Case B a genuine regression guard rather than an
  arbitrary number.
- Good self-aware inline comment (lines 111-113) correctly reasoning about why the blank-frame-skip
  heuristic doesn't compromise the discarded-pixel case's correctness.
- Clean isolation of the single variable under test (`referenceAlpha`) between the two draw calls.

## Final Assessment

A well-targeted, correctly-verified regression test for a real, documented, previously-fixed defect
(Task 887). Both assertions were independently confirmed against the actual shader source and the FNA-
derived combined-alpha semantics; no discrepancies found.
