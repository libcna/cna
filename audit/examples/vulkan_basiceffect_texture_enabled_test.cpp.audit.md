# Audit: examples/vulkan_basiceffect_texture_enabled_test.cpp

## Metadata

- Source file: `examples/vulkan_basiceffect_texture_enabled_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Vulkan backend `BasicEffect.TextureEnabled` pixel test (Task 366)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_test_vulkan_basiceffect_texture_enabled` / `Vulkan_BasicEffect_TextureEnabled`,
  `cmake/Tests/VulkanTests.cmake:497-499`)
- XNA/FNA relevance: direct — `BasicEffect.TextureEnabled`, `VertexColorEnabled` (implicit default `false`),
  `LightingEnabled` (implicit default `false`)
- FNA reference: `HLSL/Common.fxh`'s `ComputeCommonVSOutput` (`vout.Diffuse = DiffuseColor`) and the
  `PSBasicTxNoFog` shader (`SAMPLE_TEXTURE(Texture,pin.TexCoord) * pin.Diffuse`, no vertex-color multiply in this
  variant)
- Related production code: `src/CNA/Internal/Backends/Vulkan/shaders/textured3d.vert.glsl` (`fragTint =
  pc.diffuseColor`, unconditional — this stride has no vertex-color attribute at all) and
  `textured3d.frag.glsl` (`outColor = tex * fragTint`).

## Purpose

3-check pixel test proving `TextureEnabled=true` (with both `LightingEnabled` and `VertexColorEnabled` left at
their real FNA defaults, `false`) produces `TextureColor*DiffuseColor*Alpha`, with no vertex-color contribution
possible at all (stride-20 `VertexPositionTexture` has no color attribute). Uses `kTexColor(200,100,50)` and
`kDiffuse(0.8,0.4,0.6)`, chosen to match the vertex-color sibling test's own texture/diffuse pair so the
`(160,40,30)` product target is independently re-derivable and comparable across the two files.

## Executive Verdict

**Healthy** — the expected product and both disproof targets were independently re-derived and confirmed exactly
correct against the actual `textured3d.frag.glsl` shader, which correctly has no vertex-color path at all for
this vertex format.

## Checklist Results

### API / XNA / FNA parity
`setTextureEnabledProperty`/`setTextureProperty`/`setDiffuseColorProperty` are correct `IEffectTexture`/
`BasicEffect` members. `LightingEnabled`/`VertexColorEnabled` are correctly left unset to exercise FNA's real
defaults (confirmed `lightingEnabled_ = false`, `VertexColorEnabled = false` at `BasicEffect.hpp:48,367`) rather
than the test redundantly re-asserting them.

### Behavioral correctness — independent re-derivation
`kTexColor(200,100,50)`, `kDiffuse(0.8,0.4,0.6)`:
- `R: 200*0.8=160`, `G: 100*0.4=40`, `B: 50*0.6=30` → **matches `kExpected(160,40,30)` exactly**, and matches
  `textured3d.frag.glsl`'s actual formula (`outColor = tex * fragTint`, `fragTint = pc.diffuseColor` set
  unconditionally in `textured3d.vert.glsl`, no `vertexColorEnabled` branch present in this stride's shader at
  all since `VertexPositionTexture` carries no color attribute).
- `kDiffuseOnly(204,102,153)` (disproof target, diffuse alone with no texture multiply): `0.8*255=204`,
  `0.4*255=102`, `0.6*255=153` — confirmed this is indeed what a texture-ignoring bug would produce, correctly
  disproved by `!matches(got,kDiffuseOnly)`.
- `kTextureOnly(200,100,50)` (disproof target, texture alone with no diffuse multiply): trivially the raw texture
  color — confirmed correctly disproved.

### Logic
Three checks (product, not-diffuse-alone, not-texture-alone) correctly triangulate that both factors are
genuinely multiplied together, not that either one is silently dropped — good test design for a single-frame
draw with no retry-loop ambiguity (this file's `Draw()` does still use the same 20-iteration blank-frame retry
pattern as every sibling file, lines 106-120).

### C++ correctness
No issues; single fresh `BasicEffect`/`Texture2D` per `Draw()` call.

### Robustness
N/A — no malformed-input path.

### Testing
All three checks are correct and well-targeted; no gaps found for this file's stated scope.

## Detailed Findings

No HIGH/CRITICAL/MEDIUM findings.

### F1 — No case exercises `Alpha < 1.0` despite the file's own header claiming to validate the full `TextureColor*DiffuseColor*Alpha` product

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: header comment lines 6-9 (states the shader "must output `TextureColor*DiffuseColor*Alpha`");
  `Draw()` never calls `setAlphaProperty()`, leaving `Alpha` at its default `1.0` (`BasicEffect.hpp:366`) for
  every check.
- Evidence: with `Alpha` fixed at `1.0` throughout, the `*Alpha` factor in the header's own claimed formula is
  never actually distinguished from a hypothetical implementation that dropped `Alpha` from the RGB multiply
  entirely — both would produce the same result at `Alpha=1.0`.
- Why it matters: minor — `Alpha`'s effect on the RGB channels specifically (as opposed to the alpha channel of
  the output, which this test also never reads back) is a real, separate FNA behavior
  (`diffuseColor = DiffuseColor*Alpha` premultiply, confirmed at `BasicEffect.cpp:72-75`) that this file's own
  header claims to cover but does not actually exercise.
- FNA/XNA comparison: N/A (coverage gap, not a behavior mismatch — the underlying premultiply logic itself was
  independently confirmed correct by direct reading of `BasicEffect.cpp:72-75` in the course of this audit's
  broader review of `FillGpuDrawParams()`).
- Suggested future action: add a fourth check with `Alpha<1.0` and a non-opaque `BlendState` (or a direct
  alpha-channel readback) to actually distinguish the premultiply from a same-looking-at-`Alpha=1` bug.

## Cross-File Observations

- Directly comparable to `vulkan_basiceffect_texture_vertexcolor_enabled_test.cpp` (same `kTexColor`/`kDiffuse`
  pair, same `(160,40,30)` product reused there as one of its own disproof targets,
  `kTextureDiffuseOnly(160,40,30)`) — this cross-file numeric consistency was independently confirmed correct in
  both directions.
- `RasterizerState::CullNone` (line 114, "Task 896" comment) confirmed accurate via the same cross-check
  performed for every sibling file in this batch.
- Unlike the lighting-focused sibling files, this file has no `LightingEnabled`/`View`/`Projection` exposure at
  all, so it does not hit the degenerate-eye-position pattern noted in `one_light_test.cpp`'s/
  `multilight_emissive_test.cpp`'s reports.

## Missing or Weak Tests

- See F1 (`Alpha` premultiply untested).
- No case tests `Texture=nullptr` with `TextureEnabled=true` (an XNA misuse case; FNA's own behavior here is a
  null-reference exception in a real D3D-backed device, so this may be an intentionally out-of-scope case for a
  pixel test, but worth noting as absent).

## Positive Findings

- The chosen texture/diffuse values are shared and cross-verified with the vertex-color sibling test, giving two
  independent confirmations of the same base multiply.
- Both disproof targets (`kDiffuseOnly`, `kTextureOnly`) are correctly computed and meaningfully distinguish the
  correct product from the two most likely implementation bugs (dropped texture, dropped diffuse).

## Final Assessment

A correct, well-targeted, if minimal, texture-modulation test. The one gap (untested `Alpha` premultiply) is
low-priority and does not undermine the file's core claim, which is independently confirmed correct.
