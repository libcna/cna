# Audit: examples/vulkan_basiceffect_textured3d_fog_test.cpp

## Metadata

- Source file: `examples/vulkan_basiceffect_textured3d_fog_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Vulkan backend `BasicEffect` linear-fog pixel integration test
  (Task 899), stride-20 `textured3d` pipeline
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_vulkan_basiceffect_textured3d_fog`
  / `Vulkan_BasicEffect_Textured3D_Fog`, `cmake/Tests/VulkanTests.cmake:624-626`)
- XNA/FNA relevance: direct — `BasicEffect.FogEnabled`/`.FogColor`/`.FogStart`/`.FogEnd`, `TextureEnabled=true`,
  `VertexColorEnabled=false`
- FNA reference: `FNA/src/Graphics/Effect/StockEffects/EffectHelpers.cs::SetFogVector()` (lines 117-142),
  `FNA/src/Graphics/Effect/StockEffects/HLSL/Common.fxh::ComputeFogFactor()`/`ApplyFog()` (lines 9-18)
- Related production code: `src/CNA/Internal/Backends/Vulkan/shaders/textured3d.vert.glsl` (lines 22-38, fog
  factor), `textured3d.frag.glsl` (lines 22-33, fog blend), `VulkanGraphicsBackend.cpp`'s `EnsureFogTex3DResources()`
  / `GetOrCreateFogTex3DDescSet()` (around line 4697-4796).

## Purpose

Companion to `vulkan_basiceffect_fog_test.cpp`, exercising the same 3-case fog formula (disabled / 50% / full)
but on the stride-20 `VertexPositionTexture` `textured3d` pipeline instead of the stride-32 lit-textured one —
per the file's own header, this is the Task 899 pipeline that needed its own dynamic-UBO fog infrastructure since
the shared 128-byte push constant used by `colored3d`/`textured3d`/`colored_textured3d`/`dual_texture3d` had zero
spare bytes. Same three checks, same numeric parameters as the sibling file: (a) fog off → pure blue; (b)
`Z=0.5,FogStart=0,FogEnd=1` → `(128,0,128)`; (c) `Z=0.9,FogEnd=0.5` → pure red.

## Executive Verdict

**Significant correctness risk** — identical root cause to `vulkan_basiceffect_fog_test.cpp`: the
`textured3d.vert.glsl` fog formula (`(FogEnd-Z)/(FogEnd-FogStart)`) is the same pattern this project's own Task
1111 explicitly proved is not equivalent to FNA's real fog math, and which was fixed only in EasyGL, never in
Vulkan. This file's two non-trivial checks assert values that are the opposite of what real FNA/XNA would render
for the same `FogStart=0` scene.

## Checklist Results

### API / XNA / FNA parity
`setFogEnabledProperty`/`setFogColorProperty`/`setFogStartProperty`/`setFogEndProperty`, `TextureEnabled=true`,
`VertexColorEnabled=false` (line 96, explicit, matching FNA's own default so this is documentation rather than a
behavior change) are all used correctly per `IEffectFog`/`IEffectTexture`. No API-surface issues.

### Behavioral correctness
Re-derived the same FNA formula as documented in `vulkan_basiceffect_fog_test.cpp`'s report (`EffectHelpers.
SetFogVector`/`Common.fxh.ComputeFogFactor`/`ApplyFog`) against this file's own case values. Case (b)
(`FogStart=0, FogEnd=1, Z=0.5`): real FNA `fnaFactor=saturate(-1*(0.5+0))=saturate(-0.5)=0` → pure blue expected
under true XNA behavior, not the `(128,0,128)` this test asserts. Case (c) (`FogStart=0, FogEnd=0.5, Z=0.9`):
`fnaFactor=saturate(-2*0.9)=saturate(-1.8)=0` → pure blue again under true XNA behavior, not the pure red this
test asserts.

Confirmed the shader itself (`textured3d.vert.glsl:36-38`): `fragFogFactor=clamp((FogEnd-Z)/(FogEnd-FogStart),0,1)`,
blended `outColor.rgb=mix(fog.fogColorEnabled.xyz,outColor.rgb,fragFogFactor)` (`textured3d.frag.glsl:32-33`) —
byte-for-byte the same formula shape as `lit_textured3d.vert.glsl`'s, and therefore the same `CNA_factor(z) ≡
1-fnaFactor(-z)` sign-mirrored relationship to the true FNA quantity, not the corrected (`z+FogEnd`) form Task
1111 introduced in EasyGL.

### Logic
Same three-case shape as the sibling file; only case (a) (fog disabled) is unconditionally correct, since the
formula is not exercised there at all.

### Robustness
N/A — no malformed-input path.

### Testing
Same limitation as the sibling file: this test's own expected constants were (evidently) derived from the same
shader it's supposed to be validating, so it cannot catch its own root-cause defect; it is simultaneously
internally consistent and incorrect relative to real FNA behavior.

## Detailed Findings

### F1 — `textured3d`'s fog-factor formula reproduces the Task-1111-fixed-in-EasyGL-only bug; two of three checks assert the opposite of real FNA behavior

- Severity: HIGH
- Confidence: HIGH (see the full derivation, git evidence, and cross-file precedent already established in
  `vulkan_basiceffect_fog_test.cpp.audit.md`'s F1 — the formula, the bug class, and the fix history are shared
  verbatim; only the specific shader file/pipeline differs)
- Category: correctness / FNA-parity / stale-comment
- Location/symbol: `textured3d.vert.glsl:36-38` (`fragFogFactor`), mirrored identically in
  `colored3d.vert.glsl:39-41`, `colored_textured3d.vert.glsl:37-40`, `dual_texture3d.vert.glsl:40-42` — i.e. every
  pipeline Task 899 touched shares this same, still-unfixed formula; test assertions at this file's lines
  137-147; stale claim at line 9 ("matches EasyGL's already-tested formula exactly, Task 195").
- Evidence: same `git show 74ad3bae` evidence as the sibling report — Task 1111 (2026-07-16) fixed only
  `examples/easygl_*_fog_test.cpp` and `EasyGLGraphicsBackend.cpp`; Task 899 (`c2386302`, 2026-07-07, which
  authored `textured3d.vert.glsl`'s fog term) predates and was never revisited by it.
- Why it matters: identical to the sibling finding — this affects the stride-20 `VertexPositionTexture` path
  used by any textured, unlit 3D draw with fog enabled, for the same "ordinary" `FogStart=0` configuration a
  developer would naturally choose.
- FNA/XNA comparison: `EffectHelpers.cs::SetFogVector()`, `Common.fxh::ComputeFogFactor()`/`ApplyFog()`.
- Related files: `vulkan_basiceffect_fog_test.cpp` (identical bug, `lit_textured3d` pipeline instead);
  `colored3d.vert.glsl`/`colored_textured3d.vert.glsl`/`dual_texture3d.vert.glsl` (same bug, not covered by a
  dedicated fog pixel test in this shard).
- Suggested future action: same as the sibling file — port Task 1111's corrected formula to all Task-899
  pipelines and re-derive this test's expected constants (or re-parameterize with a `FogStart`/`FogEnd` straddling
  zero, as the corrected EasyGL test does, so a genuine partial-fog case is exercised at all).

### F2 — Fog ignores `World`/`View` (object-space `Z` only), same class of limitation `docs/easygl_bugs.md` documents for EasyGL but with no Vulkan equivalent

- Severity: MEDIUM
- Confidence: HIGH
- Category: architecture / documentation-gap
- Location/symbol: `textured3d.vert.glsl:36-38` uses raw `inPos.z`; no `World`/`View` transform is applied to it
  anywhere in this shader (there is no `world` matrix in `textured3d`'s push-constant/UBO layout at all, unlike
  `lit_textured3d`'s).
- Evidence/why it matters: identical reasoning to the sibling file's F2 — this test (and its sibling) exclusively
  use `World=View=Identity`, so this limitation is untested and undocumented for Vulkan specifically.
- Suggested future action: same as sibling — document in a Vulkan-equivalent of `docs/easygl_bugs.md`.

## Cross-File Observations

- Structurally near-identical to `vulkan_basiceffect_fog_test.cpp` (same case values, same tolerance, same
  retry-loop pattern) — the two files together give the false impression of two independently-verified pipelines
  when both actually share one unverified (and, per this audit, incorrect) formula.
- `colored3d.vert.glsl`/`colored_textured3d.vert.glsl`/`dual_texture3d.vert.glsl` (all Task 899, all confirmed via
  direct grep to share the identical `(FogEnd-Z)/(FogEnd-FogStart)` pattern) have no dedicated fog pixel test in
  this shard at all — this audit's finding therefore likely also applies to those pipelines without any test
  currently exercising (or masking) it either way.

## Missing or Weak Tests

- Same as sibling file: no non-identity `World`/`View` case (F2); no `FogStart==FogEnd` degenerate case.
- No fog pixel test exists in this shard for `colored3d`/`colored_textured3d`/`dual_texture3d` at all, despite
  those pipelines sharing this exact fog implementation (and its bug).

## Positive Findings

- `RasterizerState::CullNone` workaround (line 119) is applied correctly and consistently with every sibling
  file's confirmed-necessary Task 896/908 pattern.
- The header's own account of the `textured3d`/`colored3d`/`colored_textured3d`/`dual_texture3d` "shared bundle"
  UBO architecture is corroborated by direct inspection of `VulkanGraphicsBackend.cpp`'s `EnsureFogTex3DResources`
  and the matching `FogParams` UBO declared identically across those four `.vert.glsl` files.

## Final Assessment

Same conclusion as `vulkan_basiceffect_fog_test.cpp`: a mechanically well-built test whose passing status
certifies a fog formula that is provably not equivalent to real FNA/XNA fog math, reproducing a bug this project
already found and fixed in a sibling backend (Task 1111) but never carried over to Vulkan.
