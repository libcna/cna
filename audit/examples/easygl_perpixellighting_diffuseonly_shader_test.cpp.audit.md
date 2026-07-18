# Audit: examples/easygl_perpixellighting_diffuseonly_shader_test.cpp

## Metadata

- Source file: `examples/easygl_perpixellighting_diffuseonly_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — HLSL→GLSL shader-conversion proof for XNA Game Studio's
  `PerPixelLighting.fx`, `PerPixelDiffuse` technique
- File type: C++ example/integration-test executable
  (`EasyGLPerPixelLightingDiffuseOnlyTest : Microsoft::Xna::Framework::Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::ShaderEffect` (`ShaderEffect.cpp`),
  `ContentManager`'s `.cnj` `EffectTypeReader` (`ContentManager.cpp` lines 715-820)
- XNA/FNA relevance: not FNA source (FNA doesn't ship this sample) but a real XNA Game Studio sample shader —
  confirmed the file's own transcription of `PerPixelLighting.fx`'s `PerPixelDiffuseVS`/`DiffuseOnlyPS` against
  the actual sample source at `PerPixelLightingSample_4_0/PerPixelLighting/Content/PerPixelLighting.fx` (lines
  142-180, technique `PerPixelDiffuse` at lines 214-227) during this audit — verbatim match.
- Main related tests: this file itself (Task 947, Phase 78 rollout); sibling of
  `easygl_perpixellighting_shader_test.cpp` and `easygl_perpixellighting_vertexdiffuse_pixelphong_shader_test.cpp`
  (both audited in this same batch), all 3 covering different techniques from the same `.fx` file.

## Purpose

Ports `PerPixelLighting.fx`'s `PerPixelDiffuse` technique (ambient + per-pixel Lambertian diffuse, no specular) to
GLSL and verifies it renders correctly for two `World` matrices (Identity, RotationY(180°)) that flip the surface
normal relative to a fixed light/camera, proving the `World` matrix genuinely reaches the shader's per-pixel
lighting calculation rather than being ignored.

## Executive Verdict

**Healthy.** Both of this file's expected pixel values were independently recomputed by this audit from the actual
GLSL formula and the actual test geometry/light setup, and both match the file's own claimed values exactly. The
FNA-sample-source transcription (`DiffuseOnlyPS`) was independently confirmed verbatim against the real `.fx` file.
No HIGH/CRITICAL findings; one shared LOW housekeeping item (temp directory cleanup) already established
elsewhere in this shard.

## Checklist Results

### API / XNA / FNA parity
N/A directly (custom `ShaderEffect`, not an XNA-namespace stock effect) — but the ported HLSL logic is real XNA
Game Studio sample content, independently confirmed byte-for-byte against
`PerPixelLightingSample_4_0/PerPixelLighting/Content/PerPixelLighting.fx`'s `DiffuseOnlyPS` (source lines 169-180):
`directionToLight = normalize(lightPosition - input.WorldPosition)`, `diffuseIntensity =
saturate(dot(directionToLight, input.WorldNormal))`, `diffuse = diffuseLightColor * diffuseIntensity`, `color =
diffuse + ambientLightColor`, `color.a = 1.0` — matches the ported GLSL (`kFragSrc`, lines 97-104) term-for-term.

### Behavioral correctness
Independently re-derived both checks from the actual GLSL/scene setup (quad centred at origin, light `(0,0,5)`,
camera `(0,0,3)`, `ambientLightColor=(0.1,0.05,0.02)`, `diffuseLightColor=(0.4,0.3,0.2)`):
- Check A (`World=Identity`): at the exact screen centre, interpolated `WorldPosition=(0,0,0)` (since `World` is
  identity and the quad is centred on the origin), `directionToLight = normalize((0,0,5)) = (0,0,1)`,
  `worldNormal = mat3(Identity)*(0,0,1) = (0,0,1)`, `diffuseIntensity = dot((0,0,1),(0,0,1)) = 1`, `diffuse =
  (0.4,0.3,0.2)`, `color = (0.5,0.35,0.22)` → bytes `round(255*(0.5,0.35,0.22)) = (128, 89.25→89, 56.1→56)`.
  **Matches the file's claimed `(128,89,56)` exactly.**
- Check B (`World=RotationY(180°)`): rotating the quad's corners 180° about Y maps `(x,y,0)→(-x,y,0)`, which for
  this X-symmetric quad leaves the same on-screen footprint (centre still `(0,0,0)`), but
  `worldNormal = mat3(RotationY(180°))*(0,0,1) = (0,0,-1)` (a 180° Y-rotation flips X and Z). `diffuseIntensity =
  clamp(dot((0,0,1),(0,0,-1)),0,1) = clamp(-1,0,1) = 0`, so `diffuse=(0,0,0)`, `color = ambient only =
  (0.1,0.05,0.02)` → bytes `(25.5→26, 12.75→13, 5.1→5)`. **Matches the file's claimed `(26,13,5)` exactly.**
Both checks recompute correctly; the "distinct from Check A" claim (proving `World` genuinely reaches the shader)
is a real, verified discriminating property of this test, not window dressing — the two expected colours differ
in every channel by a wide margin (128 vs 26, well outside the ±6 tolerance used for both checks).

### Logic
`kVertSrc` (lines 71-86) is noted by the file's own comment (line 4-5, 69-70) to be "identical to the
already-ported `PerPixelDiffuseAndPhong` technique's own vertex shader" — confirmed true: both this file and
`easygl_perpixellighting_shader_test.cpp` in this same batch use character-for-character the same
`PerPixelDiffuseVS` GLSL body (`worldPos4`/`vWorldPosition`/`vWorldNormal`/`gl_Position` computation), matching the
real HLSL sample's own reuse of the same vertex shader function across both techniques (confirmed against
`PerPixelLighting.fx` source: `PerPixelDiffuse` and `PerPixelDiffuseAndPhong` both compile
`VertexShader = compile vs_2_0 PerPixelDiffuseVS()`).

### Memory/resource lifetime
Temp `.cnj`/GLSL files written under a per-instance-pointer-suffixed temp directory (lines 122-125) are never
cleaned up — see Detailed Findings F1, identical pattern to every other hand-rolled `ShaderEffect` test in this
batch.

### Robustness
`Draw()` checks `!fx || !fx->IsEffectValid()` (lines 193-198) before proceeding, consistent with every sibling test
in this batch.

### Testing
This file is itself a test; its 2-check design (Identity vs. RotationY180) specifically isolates whether `World`
reaches the per-pixel normal calculation, which is the one thing that could silently regress in a diffuse-only
shader (there is no specular term to also verify, unlike its `PerPixelDiffuseAndPhong` sibling).

## Detailed Findings

### F1 — Temp directory written per test run, never cleaned up

- Severity: LOW
- Confidence: HIGH
- Category: maintainability / resource lifetime
- Location/symbol: `Initialize()`, lines 122-134
- Evidence: no `std::filesystem::remove_all` (or equivalent) call exists anywhere in this file for the temp
  directory it creates.
- Why it matters: same as the identical finding already recorded for
  `easygl_particleeffect_shader_test.cpp` in this same batch — a harmless, low-priority accumulation of
  orphan temp-directory content across CTest runs, not a correctness issue.
- FNA/XNA comparison: N/A.
- Related files: every other hand-rolled `ShaderEffect` test in this batch shares this same pattern.

## Cross-File Observations

- The one real difference between this file and `easygl_perpixellighting_shader_test.cpp` is the fragment shader
  (`DiffuseOnlyPS` vs. `DiffuseAndPhongPS` — no specular term at all here); the vertex shader and every other
  aspect of the harness (geometry, temp-file mechanics, `.cnj` schema, check structure) are shared verbatim,
  correctly reflecting the fact that both techniques in the real `.fx` file share the same vertex shader.
- Unlike `easygl_perpixellighting_vertexdiffuse_pixelphong_shader_test.cpp` (same shard, this batch), this file's
  fragment shader does NOT need `precision highp float` for `lightPosition` (it uses `precision mediump float`,
  line 90) because `lightPosition` is declared only in this file's fragment shader, never in its vertex shader —
  so there is no cross-stage GLSL ES precision-qualifier mismatch to work around here, unlike the
  vertex-diffuse/pixel-phong sibling where both stages declare it. Verified this reasoning is internally
  consistent across the 3 sibling files rather than assumed.

## Missing or Weak Tests

- Only 2 checks (Identity, RotationY180) — reasonable for a diffuse-only shader with no specular term to also
  probe, but there is no boundary check near `diffuseIntensity`'s clamp point (e.g. a `World` orientation that
  puts the light exactly at grazing incidence, `dot≈0`), which would be the one place a `saturate`/`clamp`
  omission could hide.

## Positive Findings

- FNA-Game-Studio-sample transcription independently confirmed verbatim against the real `.fx` source.
- Both expected pixel values independently recomputed from first principles during this audit and found to match
  exactly, including correct rounding behavior.
- Correctly reuses the sibling technique's own vertex shader rather than re-deriving it, mirroring the real HLSL
  sample's own structure.

## Final Assessment

A concise, accurately-verified shader-conversion test. Its FNA-Game-Studio-sample transcription and both hand-
derived expected pixel values were independently re-checked during this audit and found correct; its only gap is
the shared, low-priority temp-directory cleanup omission common to this test family.
