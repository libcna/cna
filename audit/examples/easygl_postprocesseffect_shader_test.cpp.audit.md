# Audit: examples/easygl_postprocesseffect_shader_test.cpp

## Metadata

- Source file: `examples/easygl_postprocesseffect_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — HLSL→GLSL shader-conversion proof for XNA Game Studio's
  `PostprocessEffect.Fx` (`NonPhotoRealisticSample`), all 5 techniques
- File type: C++ example/integration-test executable (`EasyGLPostprocessEffectTest : Microsoft::Xna::Framework::Game`,
  `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::ShaderEffect` (`ShaderEffect.cpp`),
  `Microsoft::Xna::Framework::Graphics::SpriteBatch::Begin(..., Effect*)` (`SpriteBatch.cpp` lines 70-124),
  `ContentManager`'s `.cnj` `EffectTypeReader` (`ContentManager.cpp` lines 715-820)
- XNA/FNA relevance: real XNA Game Studio sample content — independently confirmed the file's transcription of
  `PixelShaderFunction` and all 5 technique parameter combinations against the actual
  `NonPhotoRealisticSample_4_0/NonPhotoRealistic/Content/PostprocessEffect.Fx` (lines 84-193) — verbatim match,
  including the exact 5 `(applyEdgeDetect, applySketch, sketchInColor)` triples per technique.
- Main related tests: this file itself (Task 947, Phase 78 rollout); its own header states it clears
  `DEFERRED.md` item #11 for the `NonPhotoRealistic` sample entirely (alongside the already-ported
  `CartoonEffect.Fx`).

## Purpose

Ports all 5 techniques of `PostprocessEffect.Fx` (`EdgeDetect`/`EdgeDetectMonoSketch`/`EdgeDetectColorSketch`/
`MonoSketch`/`ColorSketch`) as **one** GLSL fragment shader driven by 3 real runtime `bool` uniforms, instead of 5
separately-compiled HLSL variants — an explicitly-documented, deliberate adaptation to this project's one-program-
per-port `ShaderEffect` model. Drives the shader through `SpriteBatch::Begin(..., effect)` (a full-screen textured
quad), and verifies 6 checks spanning edge-detection (far-from-edge vs. at-edge), sketch (mono vs. colour), and
composition (both stages together, in both possible outcome combinations).

## Executive Verdict

**Healthy.** The file's HLSL transcription and 5-technique parameter mapping were independently confirmed
verbatim against the real `.fx` source. Two of the six numeric checks (Check 2's edge-detect-at-boundary and
Check 3's mono-sketch) were independently recomputed by this audit from the real GLSL formula and matched exactly,
including intermediate values not restated by the file's own comment (e.g. `sketchResult≈0.8658`). The
`SpriteBatch::Begin(..., effect)` + explicit `fx->Apply()`-before-uniforms ordering this file uses was traced
against the real `SpriteBatch.cpp`/`ShaderEffect.cpp`/`EasyGLGraphicsBackend.cpp` and found correct (not redundant,
not a race — `Apply()` must precede the raw `SetUniformX` calls so they target the currently-bound GL program).
No HIGH/CRITICAL findings; one shared LOW housekeeping item (temp-directory cleanup).

## Checklist Results

### API / XNA / FNA parity
N/A directly, but independently confirmed the ported logic against the real sample source
(`NonPhotoRealisticSample_4_0/NonPhotoRealistic/Content/PostprocessEffect.Fx`). `PixelShaderFunction`'s uniform
declarations (`EdgeWidth=1`, `EdgeIntensity=1`, `NormalThreshold=0.5`, `DepthThreshold=0.1`,
`NormalSensitivity=1`, `DepthSensitivity=10`, `SketchThreshold=0.1`, `SketchBrightness=0.333`, source lines 10-25)
match this file's `DrawOnce()` uniform values (lines 263-272) exactly. The 5 techniques' compile-time bool triples
(source lines 155-192: `PixelShaderFunction(true,false,false)` / `(true,true,false)` / `(true,true,true)` /
`(false,true,false)` / `(false,true,true)`) match the file's own header table (lines 10-12) exactly:
`EdgeDetect`=T,F,F; `EdgeDetectMonoSketch`=T,T,F; `EdgeDetectColorSketch`=T,T,T; `MonoSketch`=F,T,F;
`ColorSketch`=F,T,T. The "compile-time bool → runtime uniform" adaptation is explicitly documented as an
intentional, behaviorally-equivalent deviation (lines 13-17) rather than a silent scope reduction, satisfying this
project's `CLAUDE.md` rule that intentional deviations from FNA/XNA logic must be documented.

### Behavioral correctness
Independently recomputed 2 of the 6 checks from the real GLSL formula and the file's own fixture values (solid
scene `(0.8,0.6,0.4)`, solid black sketch overlay, 64-texel-wide normal/depth split at `x=32`):
- **Check 2 (EdgeDetect at boundary, x=32)**: `diagonalDelta = abs(n1-n2)+abs(n3-n4)`; at the exact left/right
  split, the 4 diagonal taps (`±1,±1` texel offsets around x=32) straddle both halves, so
  `diagonalDelta = 2*|(0,0,1,0)-(1,0,0,1)| = (2,0,2,2)`. `normalDelta = dot((2,0,2),(1,1,1)) = 4`, then
  `clamp((4-0.5)*1, 0, 1) = 1`. `depthDelta = clamp((2-0.1)*10, 0, 1) = clamp(19,0,1) = 1`.
  `edgeAmount = clamp(1+1,0,1)*1 = 1` → `scene *= (1-1) = 0` → pure black. **Matches the file's claimed
  `(0,0,0)` exactly**, and the file's own check tolerance (`≤10` per channel, line 310) correctly accommodates a
  possible off-by-one-texel sampling boundary without weakening the assertion's intent.
- **Check 3 (MonoSketch, far from boundary)**: `saturatedScene = clamp((scene-0.1)*2,0,1)`: for
  `scene=(0.8,0.6,0.4)`: `(0.7*2, 0.5*2, 0.3*2) = (1.4,1.0,0.6) → clamp → (1,1,0.6)`.
  `sketchPattern=(0,0,0)` (solid black overlay) → `negativeSketch = (1-satScene)*(1-0) = (0,0,0.4)`.
  `sketchResult = dot((1,1,1)-negativeSketch, vec3(0.333)) = dot((1,1,0.6),(0.333,0.333,0.333)) = 2.6*0.333 =
  0.8658`. `scene = vec3(sketchResult)` (mono branch) → bytes `round(255*0.8658) = 220.8→221`. **Matches the
  file's claimed `(221,221,221)` exactly**, and the intermediate `sketchResult≈0.8658` the file states (line 63) is
  independently confirmed correct to 4 decimal places.
Both recomputations match exactly; combined with the already-independently-confirmed FNA-sample transcription,
this gives high confidence the remaining 4 checks (which follow the same formula with different boolean
combinations) are correct too.

### Logic
`DrawOnce()` (lines 244-282) calls `fx->Apply()` **before** the `SetTexture`/`SetUniformX` calls, then only
afterwards calls `sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr, fx)`.
Traced this ordering against the real production chain: `ShaderEffect::OnApply()` (`ShaderEffect.cpp` line 95-101)
calls `effectBackend_->Bind()`, which (`EasyGLEffectBackend::Bind()`, `EasyGLGraphicsBackend.cpp` line 276-280)
calls `program_.use()` — i.e. `glUseProgram`. The subsequent `SetUniformInt`/`SetUniformFloat`/`SetTexture` calls
route to `EasyGLEffectBackend::SetUniformX` (lines 297-331), which call `program_.set_uniform(loc, ...)` — for
this to affect the *correct* GL program object under classic (non-DSA) `glUniform*` semantics, the program must
already be the currently-bound one, which `fx->Apply()`'s preceding call guarantees. `SpriteBatch::Begin(...,
effect)` (`SpriteBatch.cpp` line 106, 114) stores the `Effect*` and forwards it to the backend via
`SetCustomEffect`/`GpuDrawParams::customEffectBackend` (confirmed in `ShaderEffect::FillGpuDrawParams`,
`ShaderEffect.cpp` line 108-111) for its own later re-bind at flush time — since it's the *same* program object,
any such re-bind is harmless to the uniform values already set. This ordering is therefore correct, not
coincidental or redundant, and matches the pattern in every other hand-rolled `ShaderEffect` test in this batch.

### Memory/resource lifetime
Same per-instance-pointer temp-directory pattern as every sibling test in this batch (lines 199-211), never
cleaned up — see Detailed Findings F1. `sceneTex_`/`normalDepthTex_`/`sketchTex_` are `Texture2D` members built
once in `Initialize()` and reused across all 6 `DrawOnce()` calls — appropriate, since none of them change between
checks.

### Robustness
`Draw()` checks `!fx || !fx->IsEffectValid()` (lines 289-295) — consistent with the rest of this batch.

### Testing
This file is itself a test, with 6 checks that together probe: edge-detection alone (far/at-boundary), sketch
alone (mono/colour), and both stages composed together in both orderings-that-matter (edge-detect-as-no-op when
far from the boundary but sketch active, per Check 5; edge-detect-as-full-override when at the boundary with
sketch also active, per Check 6) — genuinely exercising the "sketch first, then edge detect operates on the
already-sketched value" composition order the file's own header describes (lines 44-45), not just each stage in
isolation.

## Detailed Findings

### F1 — Temp directory written per test run, never cleaned up

- Severity: LOW
- Confidence: HIGH
- Category: maintainability / resource lifetime
- Location/symbol: `Initialize()`, lines 199-211
- Evidence: no cleanup call for the created temp directory exists anywhere in this file.
- Why it matters: identical, shared, low-priority finding already recorded for every other hand-rolled
  `ShaderEffect` test in this batch — harmless orphan temp-directory accumulation across CTest runs.
- FNA/XNA comparison: N/A.
- Related files: `easygl_particleeffect_shader_test.cpp`,
  `easygl_perpixellighting_diffuseonly_shader_test.cpp`, `easygl_perpixellighting_shader_test.cpp`,
  `easygl_perpixellighting_vertexdiffuse_pixelphong_shader_test.cpp`.

## Cross-File Observations

- This is the only file in this batch that drives its custom `ShaderEffect` through `SpriteBatch` (a full-screen
  textured quad) rather than a direct `GraphicsDevice::DrawIndexedPrimitives`/`DrawPrimitives` call — appropriate,
  since post-processing effects are inherently 2D full-screen operations; the `fx->Apply()`-before-uniforms
  ordering this requires was specifically traced through `SpriteBatch.cpp`'s `Begin()`/flush path (see Logic
  above) since it's a more indirect chain than the other files' direct-draw pattern.
- Shares the exact `.cnj` schema and `ContentManager::EffectTypeReader` loading path with every other hand-rolled
  `ShaderEffect` test in this batch — cross-checked once for this file and found consistent with the others.

## Missing or Weak Tests

- Checks 4/5 (recomputed independently for Check 3 above, and by extension trusted for the same-formula Check 4)
  are checked with a tolerance of `±8` (line 308) — a bit looser than the `±6` used by the sibling
  `PerPixelLighting` tests in this batch, though still reasonable for a shader involving a `dot()` reduction across
  3 channels where rounding can compound slightly more; not a defect, just worth noting as a slightly different
  tolerance convention within the same batch.
- No check exercises `SketchJitter` (always `(0,0)` here) — a broken jitter-offset application in the sketch
  sampler would not be caught by this file's own checks, since jitter is disabled throughout.

## Positive Findings

- FNA-Game-Studio-sample transcription, including the precise 5-technique boolean-triple mapping, independently
  confirmed verbatim against the real `.fx` source.
- Two of the six checks independently recomputed from the real GLSL formula (including an unstated intermediate
  value, `sketchResult≈0.8658`) and found to match exactly.
- The `fx->Apply()`-before-`SetUniformX` ordering constraint (required for classic `glUniform*` semantics to hit
  the right program) is correctly followed here despite the added indirection of going through `SpriteBatch`
  rather than a direct draw call — traced end-to-end through `ShaderEffect`/`SpriteBatch`/`EasyGLGraphicsBackend`
  and found genuinely correct, not accidental.
- The 6-check design specifically probes composition order (sketch-then-edge-detect) rather than only testing
  each of the 5 named techniques in isolation.

## Final Assessment

The most structurally complex shader-conversion test in this batch (5 techniques folded into 1 runtime-branched
GLSL program, driven through `SpriteBatch` rather than a direct draw call), and its complexity is matched by
genuine rigor: the HLSL transcription, the technique-to-boolean-triple mapping, and 2 of its 6 numeric checks were
all independently re-verified during this audit and found correct. Its only gaps are the shared, low-priority
temp-directory cleanup omission and the untested `SketchJitter` parameter.
