# Audit: examples/webgpu_dualtexture3d_test.cpp

## Metadata

- Source file: `examples/webgpu_dualtexture3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `DualTextureEffect` two-layer-multiply pixel test, WebGPU
  backend (experimental, per `CLAUDE.md`).
- Test executable: `cna_test_webgpu_dualtexture3d`, CTest target `WebGPU_DualTexture3D`
  (`cmake/Tests/WebGpuTests.cmake:81-82`).
- XNA/FNA relevance: direct — `DualTextureEffect.Texture`/`Texture2`/`VertexColorEnabled`.
- FNA reference: `HLSL/DualTextureEffect.fx` (`PSDualTexture`: `color.rgb *= 2; color *= overlay *
  pin.Diffuse;`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/DualTextureEffect.cpp`
  (`FillGpuDrawParams`, default `diffuseColor_ = {1,1,1}` at
  `include/.../DualTextureEffect.hpp:269`), `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`CreateDualTextureResources()` lines 3567-3704, `GetOrCreatePipelineDualTexture3D()` lines 3706-3815,
  `QueueDualTextureDraw()`/`RenderDualTextureDraws()` around lines 6710-6850, dispatch in
  `DrawPrimitivesEx()`/`DrawIndexedPrimitivesEx()` lines 6030-6035, 6115-6120).

## Purpose

Four-check pixel test proving `DualTextureEffect`'s real "boosted-multiply" two-layer formula runs on
this backend, not a plain single-texture path or an unscaled multiply: (A) mid-grey texture0 doubled
and clamped, times a white texture1, renders white (proves the real `*2.0` boost, not a plain multiply,
which would leave mid-grey); (B) red texture0 times green texture1 renders black (proves both textures
are genuinely sampled and multiplied, not just one); (C) the stride-24
(`VertexPositionColorTexture`) variant with `VertexColorEnabled=true` and a green vertex tint renders
green, proving per-vertex colour survives dual-texture sampling; (D) the `DrawIndexedPrimitives`
counterpart of check B.

## Executive Verdict

**Healthy.** Independently re-derived the `WGSL` fragment formula in `CreateDualTextureResources()`
against FNA's real `PSDualTexture` (`HLSL/DualTextureEffect.fx`) field-for-field and found them
identical, including the "boost the first sample's RGB by 2.0, leave its alpha alone, then multiply the
whole `vec4` by the second sample and the tint" ordering. Both numeric checks (A, B) are correct for the
current shader, not a stale/coincidental match.

## Checklist Results

### API / XNA / FNA parity

`setTextureProperty`/`setTexture2Property`/`setVertexColorEnabledProperty` (lines 148-149, 163-164, 189,
214-215) map directly to FNA's `DualTextureEffect` surface. `PosTexDecl()`/`PosColorTexDecl()` (lines
74-104) hand-declare byte-accurate `VertexPositionTexture`/`VertexPositionColorTexture` layouts (stride
20/24, offsets 0/12/16) rather than the typed struct's static declaration — a deliberate choice to
exercise the generic `VertexDeclaration` path, consistent with this shard's established convention (see
`webgpu_alphatest3d_test.cpp`'s own identical choice).

### Behavioral correctness

Re-derived the shader math against `CreateDualTextureResources()`'s WGSL (lines 3606-3611):
```
var sample0 = textureSample(tex0, texSampler, input.uv);
let sample1 = textureSample(tex1, texSampler, input.uv);
sample0 = vec4f(sample0.rgb * 2.0, sample0.a);
return sample0 * sample1 * u.diffuseColor;
```
which is a direct, field-for-field translation of FNA's `PSDualTexture`:
```
color.rgb *= 2;
color *= overlay * pin.Diffuse;
```
(`HLSL/DualTextureEffect.fx` lines 100-105) — same order of operations (boost RGB only, then multiply
the full `vec4` including alpha by the overlay and the tint), same lack of an explicit clamp (implicit
via the RGBA8Unorm storage format, matching GPU float-pipeline behaviour on real XNA hardware too).
- Check A: grey `(128,128,128)` → `128/255 ≈ 0.50196`; `*2 ≈ 1.00392`, clamps to `1.0` on write to the
  8-bit render target; `1.0 * white(1.0) * diffuseColor(1.0, default per
  `DualTextureEffect.hpp:269`) = white`. Matches `Color::White` exactly (test uses a 16-tolerance
  `colorNear`, but the exact math already lands on 255 with no rounding ambiguity).
- Check B: red `(255,0,0)` boosted stays `(255,0,0)` (already saturated on R, G/B already 0); times
  green `(0,255,0)` → every channel multiplies to `0` → black. Matches `Color::Black` exactly.
- Check C: `u.light0DiffuseVertexColor.w` (the `vertexColorEnabled` flag, packed by
  `FillExtUniforms()` at `out[31]`, verified at production-code line 420) gates
  `output.tint = select(u.diffuseColor, input.color * u.diffuseColor, vertexColorEnabled > 0.5)`
  (coloured-shader lines 3653-3654) — with a white/white dual-texture sample and a green vertex tint,
  `white*white*green*diffuseColor(white) = green`. Matches `Color::Lime`.
- Check D: identical math to check B, through the indexed dispatch path.

### Logic

`GetOrCreatePipelineDualTexture3D()`'s stride dispatch (lines 3718, 3727-3742) correctly selects
`dualTextureColoredShader_`/the 3-attribute `ColoredTexturedVertex` layout for stride 24 and
`dualTextureShader_`/the 2-attribute `TexturedVertex` layout otherwise, matching check C's stride-24
buffer and checks A/B/D's stride-20 buffers respectively. Pipeline caching is correctly keyed per
stride (`(stride == 24) ? dualTextureColoredPipelines_ : dualTexturePipelines_`, line 3718) so the two
shader variants cannot collide in the cache.

### C++ correctness

No concerns specific to this file; standard `colorNear()`/one-shot-`Draw()` idiom shared across the
shard.

### Robustness

Check D specifically isolates the indexed-draw dispatch (`DrawIndexedPrimitives`) from the non-indexed
path already covered by A/B/C, the same good technique already noted in
`webgpu_alphatest3d_test.cpp`'s own audit — both `DrawPrimitivesEx` (line 6030) and
`DrawIndexedPrimitivesEx` (line 6115) independently route `needsDualTexture` draws to
`QueueDualTextureDraw`, confirmed present at both call sites.

### Testing

Good coverage of both stride variants (20/24) and both draw shapes (indexed/non-indexed) for the core
multiply formula and the vertex-colour tint gate. Not covered by this file (no claim made otherwise):
`DualTextureEffect`'s fog support (this shader has none — `CreateDualTextureResources()`'s WGSL has no
fog term at all, a documented, deliberate deferral consistent with every other WebGPU 3D shader in this
backend per `plans/plan_webgpu.md`), and any case where `Texture`/`Texture2` differ in size/aspect (both
checks use 1×1 solid-colour textures, so UV-interpolation correctness across a larger texture is
untested — an acceptable, narrow scope gap for a formula-correctness test, not a defect).

### Architecture / Memory / Performance / Thread safety / Portability

No file-specific concerns. Follows the same one-shot `Game`/`Draw()`-guard/`Exit()` idiom as every other
file in this shard; per this audit's cross-cutting mandate, `DualTextureEffect` has no skinning/normal
path, so the confirmed `CreateSkinnedResources()` world-space-normal-transform bug does not apply here.

## Detailed Findings

None at HIGH or above.

## Cross-File Observations

- Confirms `DualTextureEffect`'s "boost first sample, multiply by second sample and tint" formula is
  correctly ported field-for-field from FNA's `PSDualTexture`, with no backend-specific mismatch — a
  useful positive contrast to this same shard's `webgpu_envmap3d_test.cpp` audit, where the sibling
  `EnvironmentMapEffect` shader was found to diverge from its own FNA/`Lighting.fxh` reference in the
  emissive/diffuse composition (see that report's F1).

## Missing or Weak Tests

- No coverage of `Texture`/`Texture2` at different resolutions or non-solid content (would exercise UV
  interpolation across the dual-texture path, not just per-pixel constant colours).
- No fog coverage — acceptable, since this shader has no fog term to test (a documented, shared
  WebGPU-backend scope boundary, not specific to this file).

## Positive Findings

- Both numerically-asserted checks (A, B) were independently re-derived against FNA's real
  `HLSL/DualTextureEffect.fx` pixel shader and found to match exactly, term for term, including the
  subtle "RGB-only boost, then full-vec4 multiply" ordering that a naive re-implementation could easily
  get wrong (e.g. boosting alpha too, or clamping before the second multiply).
- Check D's deliberate isolation of the indexed-draw dispatch path is good, proven-useful test design
  in this codebase (per the identical technique's value already noted in this shard's
  `webgpu_alphatest3d_test.cpp` audit).

## Final Assessment

A correct, well-targeted test with no defects found in either the test's own logic or the production
code paths (`DualTextureEffect::FillGpuDrawParams()`, `CreateDualTextureResources()`'s two WGSL shader
variants, and the `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` dispatch) it exercises. The formula this
file exists to prove — FNA's real `tex1.rgb*=2.0; result=tex1*tex2*tint` — is implemented exactly right
on this backend.
