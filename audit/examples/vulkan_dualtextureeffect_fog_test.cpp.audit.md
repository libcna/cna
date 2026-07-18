# Audit: examples/vulkan_dualtextureeffect_fog_test.cpp

## Metadata

- Source file: `examples/vulkan_dualtextureeffect_fog_test.cpp` (177 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `DualTextureEffect` linear-fog pixel integration test,
  Vulkan backend
- File type: standalone `Game`-subclass executable (`DualTextureFogVulkanTest`), CTest-registered
- XNA/FNA relevance: direct — `DualTextureEffect`/`IEffectFog` (`FogEnabled`/`FogColor`/`FogStart`/
  `FogEnd`)
- FNA reference: `HLSL/DualTextureEffect.fx` (`PSDualTexture`: `color.rgb *= 2; color *= overlay *
  pin.Diffuse;` then `ApplyFog`), `HLSL/Common.fxh` (`ComputeFogFactor`/`ApplyFog`'s
  `lerp(color.rgb, FogColor, 1-fogFactor)`-equivalent object-space-Z formula)
- Related production code: `src/Microsoft/Xna/Framework/Graphics/DualTextureEffect.cpp`
  (`OnApply()` lines 180-246, `FillGpuDrawParams()` lines 248-275),
  `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` (`DrawPrimitivesEx`'s
  `needsDualTex` branch, lines 7532-7541; `FillExtPushConst()`, lines 3575-3592),
  `src/CNA/Internal/Backends/Vulkan/shaders/dual_texture3d.vert.glsl` /
  `dual_texture3d.frag.glsl` (Task 899's dedicated dual-texture pipeline, split off from
  `textured3d.vert.glsl` to give the 2-sampler descriptor set its own dynamic fog UBO at
  binding=2).

## Purpose

Three-check pixel test proving the Vulkan dual-texture pipeline's fog blend: (a) fog disabled →
pure material color, (b) fog at 50% between `FogStart`/`FogEnd` → a color exactly halfway between
`FogColor` and the material color, (c) `Z` past `FogEnd` → fully fogged (pure `FogColor`). The
header comment documents the Task 899 fix (dedicated `dual_texture3d.vert/frag.glsl` files with
their own descriptor-set layout, since `textured3d.vert.glsl`'s own newly-added fog UBO binding
collided with dual-texture's 2-sampler layout) and explains the `Texture2=gray(128,128,128)`
trick used to cancel `DualTextureEffect`'s `color.rgb *= 2` doubling factor so the pre-fog color
reduces to exactly `DiffuseColor`.

## Executive Verdict

**Healthy** — all three checks were independently re-derived against the actual current
`dual_texture3d.frag.glsl`/`.vert.glsl` source and match both the file's own comments and FNA's
`DualTextureEffect.fx` semantics to within ordinary GPU rounding.

## Checklist Results

### API / XNA / FNA parity
`setFogEnabledProperty`/`setFogColorProperty`/`setFogStartProperty`/`setFogEndProperty`
(lines 104-107) map directly to FNA's `IEffectFog`. `setDiffuseColorProperty(Vector3(0,0,1))`
(line 103) matches `DualTextureEffect.DiffuseColor`'s `Vector3` (not `Color`) type. No API
deviation found.

### Behavioral correctness
Re-derived all three checks against `dual_texture3d.frag.glsl` (`tex1.rgb *= 2.0; outColor =
tex1*tex2*fragTint; outColor.rgb = mix(fog.fogColorEnabled.xyz, outColor.rgb, fragFogFactor);`)
and `dual_texture3d.vert.glsl`'s `fragFogFactor = clamp((fogEnd - Z)/(fogEnd - fogStart), 0, 1)`:
- Cancellation: `Texture=white(1,1,1,1)`, `Texture2=gray(0.502,0.502,0.502,1)`. `tex1.rgb*=2 →
  (2,2,2)`; `tex1*tex2 = (2*0.502, ...) ≈ (1.004,1.004,1.004)` — clamps to `1.0` in the
  framebuffer, i.e. cancels to the identity multiplier the comment claims.
- (a) fog OFF (`fragFogFactor` forced to `1.0` since `fogEnabled=false`): `outColor.rgb =
  tex1*tex2*fragTint = (1.004,1.004,1.004)*(0,0,1,1) = (0,0,1.004)` → clamped `(0,0,255)` = pure
  blue. Matches `kBlue`.
- (b) `Z=0.5`, `FogStart=0`, `FogEnd=1`: `fragFogFactor = clamp((1-0.5)/(1-0),0,1) = 0.5`.
  `mix(red,(0,0,1),0.5) = (0.5,0,0.5)` → `(128,0,128)`. Matches `Color(128,0,128,255)` exactly.
- (c) `FogEnd=0.5`, `Z=0.9`: `fragFogFactor = clamp((0.5-0.9)/(0.5-0),0,1) = clamp(-0.8,0,1) = 0`
  → `mix(red, blue, 0) = red`. Matches `kRed`.

All three derivations were checked against the live shader text (`dual_texture3d.frag.glsl:29-36`,
`dual_texture3d.vert.glsl:40-44`), not just the test's own comment, and they agree exactly.

### Logic
`renderQuad()`'s retry loop (`for (int i = 0; i < 20; ++i) { ...; if (nonzero) break; }`, lines
119-130) tolerates one or more genuinely-black transient frames before the real content appears —
a reasonable, widely-used pattern in this test family, not specific to this file.

### C++ correctness
No issues. `matches()`'s `±30` tolerance (line 79) is generous but appropriate for a 3-check test
whose expected values are exact fractions of 255 with GPU-interpolation/rounding noise.

### Cross-file consistency
Confirmed the diffuse-color plumbing end-to-end: `DualTextureEffect::FillGpuDrawParams()`
(`DualTextureEffect.cpp:260-263`) writes `diffuseColor*alpha` into `p.diffuseColor[0..3]`;
`VulkanGraphicsBackend::FillExtPushConst()` (`VulkanGraphicsBackend.cpp:3581-3582`) copies that
into push-constant floats `[16..19]`; `dual_texture3d.vert.glsl`'s `PC` struct places
`diffuseColor` at the identical offset (right after the `mat4 mvp`). No field-order mismatch.

### Architecture
The dedicated `dual_texture3d.vert/frag.glsl` pipeline and its own 3-binding descriptor set
(2 samplers + a dynamic fog UBO at binding=2) is a clean fix for the binding collision the header
comment describes; confirmed the fog UBO is written into a per-draw ring-buffer slot
(`dualTexFogUBOSlot++`, `VulkanGraphicsBackend.cpp:6525-6534`), so the 3 sequential draws in this
test's `Draw()` (each with different fog parameters) cannot clobber one another's fog data even
if they land in the same frame's command buffer.

### Testing
Genuinely discriminating: (a)/(b)/(c) each isolate a different segment of the fog ramp
(`fogFactor=1`, `0.5`, `0` respectively), and the gray-texture cancellation trick is verified
correct rather than merely asserted.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. This file's own claims and math check out against the current
production shader/effect code.

## Cross-File Observations

- Unlike `env_map3d.vert.glsl` (see the sibling `vulkan_environmentmapeffect_*` reports in this
  same batch), `dual_texture3d.vert.glsl` **does** apply the Vulkan Y-flip (`pos.y = -pos.y;`,
  line 36) consistent with every other core-3D Vulkan vertex shader
  (`colored3d`/`textured3d`/`alpha_test3d`/`lit_textured3d` all do the same). This file is not
  affected by the Y-flip gap found elsewhere in this batch.
- The Task 896 `RasterizerState::CullNone` comment (lines 123-125) was independently checked: the
  quad's winding (`TL→BL→BR`, ccw in standard math orientation) is indeed the winding XNA's
  default `CullCounterClockwise` culls, so the comment's claim is accurate, not stale.

## Missing or Weak Tests

None specific to this file; the 3-point sweep (off / 50% / full) is proportionate to what
`IEffectFog`'s formula needs to prove.

## Positive Findings

- All three numeric expectations were independently re-derived from the live GLSL source (not
  just cross-checked against the file's own comment) and match exactly.
- The gray-texture `*2` cancellation technique is a clean way to isolate the fog blend from the
  dual-texture multiply, and was verified to actually cancel (to within clamp rounding) rather
  than merely asserted by the comment.
- Confirmed the per-draw fog-UBO ring buffer prevents cross-draw data races within a single
  frame, which matters given this test issues 3 different-fog-parameter draws before presenting.

## Final Assessment

A solid, precisely-derived fog test with no defects found in either the test itself or the
production code path it exercises.
