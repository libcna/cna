# Audit: examples/vulkan_dual_texture_test.cpp

## Metadata

- Source file: `examples/vulkan_dual_texture_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `DualTextureEffect` basic two-layer-multiply pixel test
  (Task 135)
- File type: standalone `Game`-subclass executable, CTest-registered integration test
- XNA/FNA relevance: direct — `DualTextureEffect.Texture`/`Texture2`/`DiffuseColor` multiply formula.
- FNA reference: `Graphics/Effect/StockEffects/HLSL/DualTextureEffect.fx` (`PSDualTexture`:
  `color.rgb *= 2; color *= overlay * pin.Diffuse;`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/DualTextureEffect.cpp`,
  `src/CNA/Internal/Backends/Vulkan/shaders/dual_texture3d.frag.glsl`/`.vert.glsl`,
  `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`GetOrCreatePipelineDualTex3D()` line 3909).

## Purpose

Single full-screen-quad test that draws a solid magenta `Texture` and a solid yellow `Texture2` through
`DualTextureEffect` with default `DiffuseColor` (white), and checks the centre pixel reads back as red
— proving the two textures are genuinely multiplied together (magenta×yellow=(1,0,1)×(1,1,0)=(1,0,0)
=red) rather than, say, one texture being ignored or the two being blended/averaged instead of
multiplied.

## Executive Verdict

**Healthy** — the multiply formula, texture setup, and defaults were all independently confirmed
correct against `DualTextureEffect.cpp` and the current Vulkan fragment shader.

## Checklist Results

### API / XNA / FNA parity
`Texture2D::CreateFromPixels()` (lines 55-56) is correctly `NOXNA`-tagged in
`Texture2D.hpp:228` (a CNA test convenience, not an XNA API). `DualTextureEffect`'s
`Texture`/`Texture2`/`World`/`View`/`Projection` setters used here (lines 74-78) are standard
`getX/setXProperty()` wrappers, correctly used.

### Behavioral correctness
Confirmed `DualTextureEffect`'s real defaults directly: `diffuseColor_ = Vector3{1.0f,1.0f,1.0f}`,
`alpha_ = 1.0f` (`DualTextureEffect.hpp:269-270`) — the test's comment ("diffuseColor (fragTint) =
white (1,1,1,1) from DualTextureEffect defaults") is accurate; the test correctly never sets
`DiffuseColor` explicitly, testing the real default rather than re-asserting an explicit one.
`FillGpuDrawParams()` (`DualTextureEffect.cpp:260-263`) forwards `diffuseColor_*alpha_` (RGB) and
`alpha_` — with both at their defaults this reduces to a pure white passthrough tint, matching the
"fragTint=white" assumption exactly. `dual_texture3d.frag.glsl`'s actual body:
```
vec4 tex1 = texture(uTexture,  fragUV);
vec4 tex2 = texture(uTexture2, fragUV);
tex1.rgb *= 2.0;
outColor  = tex1 * tex2 * fragTint;
```
— independently cross-checked against FNA's `PSDualTexture` (`color.rgb *= 2; color *= overlay *
pin.Diffuse;`) and found to be an exact structural match (texture0 doubled on RGB only, then
multiplied by texture1 and the tint). With `magenta_rgb=(1,0,1)` doubled to `(2,0,2)` (unclamped in the
shader itself), multiplied by `yellow_rgb=(1,1,0)` gives `(2,0,0)`, multiplied by white tint stays
`(2,0,0)`, and the GPU's fixed-function colour-attachment write clamps the final output to `[0,1]` →
`(1,0,0)` = red, exactly matching `kExpected` (`R>=200,G<=50,B<=50` check, lines 102-104). Confirmed
this clamping-after-doubling behavior doesn't produce a wrong result here specifically because both
input channels being doubled (magenta's R and B channels) are already at `1.0`/full saturation, so the
`×2` overshoot and subsequent implicit clamp is harmless for this particular test's colour choice.

### Logic
`device.SetDepthTestEnabled(false)` (line 70) is a reasonable simplification for this single-full-quad
test (no risk of self-occlusion or z-fighting since only one quad is ever drawn), and
`RasterizerState::CullNone` (line 82) is set with the same Task-896-derived justification as the other
files in this shard.

### C++ correctness
`done_`/`result_` guard pattern in `Draw()` (lines 61-62) correctly prevents the check logic from
re-running (and re-printing) on subsequent frames after `Exit()` is requested but before the game loop
actually terminates — consistent with the rest of this test family.

### Testing
A single pass/fail assertion (line 102-104) is a reasonable scope for "does the basic two-texture
multiply work at all" — narrower than the sibling `vulkan_dualtextureeffect_combined_test.cpp`, which
covers the same formula with four distinct texel samples and tighter per-channel tolerances (see that
file's own audit report). This file functions as the "does it work at all" smoke check in the family,
the combined test as the "is the formula numerically exact" check — a reasonable division of labor
across the shard, not a gap in either individual file.

## Detailed Findings

None at MEDIUM or above. No HIGH/CRITICAL findings.

## Cross-File Observations

- This file was the one specifically named in the Task 896 commit message
  (`b6a00bc6`) as having a fork-introduced bug during that mass RasterizerState-culling remediation
  ("One fork used the wrong `GraphicsDevice&` variable name in `vulkan_dual_texture_test.cpp` (`dev`
  instead of `device`), caught by the next full build and fixed"). Independently verified the current
  file contains **zero** occurrences of a stray `dev.` variable reference (`grep -n "\bdev\b\."`
  returned nothing) — confirming the fix described in that commit message is genuinely present in the
  current tree, not just claimed.
- The `*2` doubling-factor formula this file exercises implicitly (via saturated magenta/yellow inputs
  that happen to make overshoot harmless) is exercised *explicitly* and non-trivially by the sibling
  `vulkan_dualtextureeffect_doubling_test.cpp`, which is the file that actually caught this doubling
  factor being missing in an earlier version of the shader (see that file's own audit report).

## Missing or Weak Tests

None for this file's own stated scope. As noted above, its choice of fully-saturated texture colours
means it cannot by itself distinguish "correct doubling then multiply" from "no doubling, texture
values happened to already overshoot" — but that gap is explicitly covered by the sibling doubling
test, so it is not a real coverage hole for the shard as a whole.

## Positive Findings

- Correctly derives its expected result from first principles in its own header comment (magenta ×
  yellow = red via component-wise multiply), and this audit's independent re-derivation confirms the
  arithmetic.
- Good historical hygiene: the file was demonstrably touched by an automated 119-file remediation
  (Task 896) and this audit confirmed the specific bug that remediation introduced-then-fixed in this
  exact file is not present in the current version.

## Final Assessment

A correct, appropriately-scoped smoke test for `DualTextureEffect`'s core two-texture multiply
behavior. No defects found; its results were independently reproduced by hand against both the current
Vulkan shader and the FNA HLSL reference.
