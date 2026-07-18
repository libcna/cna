# Audit: examples/easygl_bloom_combine_test.cpp

## Metadata

- Source file: `examples/easygl_bloom_combine_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration test
- File type: C++ example/integration-test executable (`EasyGLBloomCombineTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::ShaderEffect` (`ShaderEffect.hpp`/`.cpp`),
  `CNA::Internal::Backends::EasyGLEffectBackend::BindTexture`/`SetUniformInt`
  (`EasyGLGraphicsBackend.cpp` lines 297-353), `EasyGLSpriteBatchBackend::FlushBatch`
  (lines 1082-1171).
- XNA/FNA relevance: exercises `SpriteBatch::Begin(..., Effect*)` with a custom multi-texture-unit shader — an XNA
  4.0-real capability (custom `Effect` passed to `SpriteBatch.Begin`), though the specific shader (`BloomCombine.fx`)
  is from the Microsoft XNA Game Studio **BloomPostprocess** sample, not FNA itself.
- FNA reference: N/A for the shader body (the `BloomSample_4_0` sample project is not present anywhere in the local
  FNA reference tree at `/rv/data/library/github.com/FNA-XNA/FNA` — confirmed via `find ... -iname "*Sample*"`/
  `-iname "*.fx"`, which only turns up FNA's own stock-effect `.fx` files, e.g. `BasicEffect.fx`). The HLSL quoted in
  this file's own header comment (lines 4-21) could not be independently verified against a ground-truth source in
  this sandbox; only internal self-consistency (HLSL comment vs. GLSL translation vs. hand-computed expected pixel)
  was checked.
- Main related tests: sibling of `easygl_bloom_extract_test.cpp`, `easygl_bloom_gaussianblur_test.cpp` (the other two
  BloomPostprocess shaders) and `easygl_bloom_pipeline_test.cpp` (the 4-pass composition of all three).

## Purpose

Task 946 shader-conversion proof: ports `BloomCombine.fx`'s `AdjustSaturation()` + `PixelShaderFunction()` to GLSL
1:1 and exercises `ShaderEffect::SetTexture(1, Texture2D&)` — described in the header comment as "the first custom
shader in this codebase to sample two completely independent textures in one draw" (i.e. the real regression target
is the *second* sampler-unit binding path, not the combine math itself). Correctly placed under `examples/` as an
`easygl_`-prefixed backend integration test per `AUDIT_SCOPE.md`'s classification.

## Executive Verdict

**Mostly healthy** — the core claim (second-texture-unit binding actually reaches the shader) is proven with a
sharp, well-reasoned discriminating pixel value (magenta vs. pure blue, see header lines 39-41), and every uniform/
texture-binding call was traced against real `ShaderEffect`/`EasyGLEffectBackend` code and found to behave exactly
as the test assumes. The one real gap (F1) is that the test forces `BloomSaturation = BaseSaturation = 1.0`,
which makes `AdjustSaturation()`'s own `dot()`/`mix()` computation — the specific GLSL construct the file's own
header calls out as needing a non-trivial HLSL→GLSL rewrite (implicit-truncation `dot()`, scalar-broadcast `lerp()`)
— algebraically unreachable: `mix(x, y, 1.0)` returns `y` unconditionally, regardless of what `x` (`grey`) evaluates
to. A bug in that specific translated line would not be caught by this test or its `easygl_bloom_pipeline_test.cpp`
sibling (same parameterization, see Cross-File Observations).

## Checklist Results

### API / XNA / FNA parity
`ShaderEffect` is `NOXNA` (not part of XNA 4.0) — correctly marked as such in its own header. `SpriteBatch::Begin`'s
6-argument overload taking a custom `Effect*` (line 153: `sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque,
nullptr, nullptr, nullptr, &fx)`) is real XNA 4.0 API surface (`SpriteBatch.Begin(SpriteSortMode, BlendState,
SamplerState, DepthStencilState, RasterizerState, Effect)`).

### Behavioral correctness
Traced end-to-end:
- `fx.SetUniformInt("uBaseSampler", 1)` → `ShaderEffect::SetUniformInt` → `EasyGLEffectBackend::SetUniformInt`
  (`EasyGLGraphicsBackend.cpp:303-307`) → `glUniform1i` equivalent — tells the `uBaseSampler` GLSL sampler to read
  texture unit 1.
- `fx.SetTexture(1, baseTex_)` → `EasyGLEffectBackend::BindTexture(1, ...)` (lines 345-353) → `glActiveTexture(
  GL_TEXTURE1)`, bind, then restores `glActiveTexture(GL_TEXTURE0)` — a real, immediate GL texture-unit bind that
  persists in the GL context independent of which program is subsequently active.
- At `sb.End()` → `EasyGLSpriteBatchBackend::FlushBatch()` (lines 1082-1171), `current_texture_->BindGL()` (line
  1143) re-binds `bloomTex_` to the *currently active* unit, which is unit 0 (left there by the prior `BindTexture`
  call's own reset) — matches the shader's default (never explicitly set) `texture1` sampler reading unit 0.
- Order of operations in the test (`fx.Apply()` → set uniforms/texture → `sb.Begin/Draw/End`) is safe: GL uniform
  state is program-object state, not global state, so values set once via `glUniform*` while this program was bound
  remain valid even after `FlushBatch()` re-calls `prog->use()` on the same program object.

Hand-verified the expected-pixel math independently: `bloom'=(0,0,1,1)`, `base'=(1,0,0,1)`,
`base''=base'*(1-saturate(bloom'))=(1,0,0,1)*(1,1,0,0)=(1,0,0,0)`, `result=base''+bloom'=(1,0,1,1)` → byte
`(255,0,255,255)` (magenta) — matches the test's own derivation (header lines 34-38) and its assertion
(`R>=200 && G<=30 && B>=200`, lines 161-162).

### Logic
Single linear `Draw()` (guarded by `done_` to run exactly once): compile shader → clear green → apply effect → bind
second texture → draw bloom quad centered in the viewport → read back center pixel → compare → `Exit()`. No
branches beyond the `IsEffectValid()` guard.

### Memory/resource lifetime
`bloomTex_`/`baseTex_` are value members (not `unique_ptr`), constructed in `Initialize()` via
`Texture2D::CreateFromPixels` — standard RAII, no manual lifetime management needed. `ShaderEffect fx(...)` and
`SpriteBatch sb(...)` are stack-local to `Draw()`, destructed automatically before `Exit()` — no leak or
dangling-pointer risk.

### C++ correctness
No unchecked casts, no raw-pointer ownership. `getResult()` correctly reflects `result_`'s default (`1` = fail) if
`done_` is somehow never reached (defensive default).

### Performance
N/A — single-frame test, no hot path.

### Architecture
Correctly uses only the public XNA-facing `Microsoft::Xna::Framework::Graphics` surface plus the documented
`NOXNA` `ShaderEffect` extension API; no direct `CNA::Internal::Backends` coupling in the test itself (verified via
the production-code trace above, done separately).

### Robustness
`IsEffectValid()` guard (lines 134-139) fails loud (`[FAIL]` + `Exit()`) rather than crashing or silently
mis-rendering if GLSL compilation fails — good defensive test authoring.

### Testing
This file is itself a test. See Finding F1 for its own coverage gap.

## Detailed Findings

### F1 — `AdjustSaturation()`'s `dot()`/`mix()` computation is never actually exercised (saturation forced to 1.0)

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `Draw()` lines 149-150 (`fx.SetUniformFloat("uBloomSaturation", 1.0f);
  fx.SetUniformFloat("uBaseSaturation", 1.0f);`); `AdjustSaturation()` in `kFragSrc` (lines 89-92)
- Evidence: `mix(vec4(grey), color, saturation)` with `saturation == 1.0` returns `color` unconditionally per GLSL's
  `mix()` definition (`mix(x,y,a) = x*(1-a)+y*a`; at `a=1` the `x` term contributes 0 regardless of its value) — so
  the specific line the test's own header calls out as the interesting HLSL→GLSL translation risk (`dot(color.rgb,
  vec3(0.3,0.59,0.11))` replacing HLSL's implicit `float4`→`float3` truncation, and `vec4(grey)` replacing HLSL's
  scalar-to-vec4 broadcast) can contain an arbitrary bug — wrong constants, wrong swizzle, wrong broadcast — and
  still pass this test, because its output is multiplied by zero before it can affect the final color.
- Why it matters: the file's own header (lines 30-32) acknowledges the saturation math is "orthogonal and not
  exercised by this test" but frames that as a deliberate, understood simplification — which is fair for isolating
  the *texture-binding* claim this file targets — but it means no test in this shard (see Cross-File Observations)
  ever validates `AdjustSaturation()` at a fractional saturation, leaving that specific ported function completely
  unverified at runtime anywhere in the repository (as far as this shard's 4 bloom files show).
- FNA/XNA comparison: N/A (sample-project shader, not FNA itself; see Metadata).
- Related files: `easygl_bloom_pipeline_test.cpp` (same `uBloomSaturation`/`uBaseSaturation` = 1.0 pattern, see
  Cross-File Observations).
- Suggested future action (not implemented by this audit): add one more check in either this file or the pipeline
  test with `BloomSaturation`/`BaseSaturation` set to e.g. `0.0` against a two-channel-distinct source color, so the
  `dot()`/`mix()` line has an observable effect on the output pixel.

## Cross-File Observations

- `easygl_bloom_pipeline_test.cpp` (`combineFx.SetUniformFloat("uBloomSaturation", 1.0f);
  combineFx.SetUniformFloat("uBaseSaturation", 1.0f);`, lines 269-270 of that file) reuses the identical
  `BloomCombine.fx` GLSL body and the identical saturation=1.0 parameterization — confirming F1 is not an
  isolated gap in this one file but a shard-wide blind spot for `AdjustSaturation()`'s saturation<1 branch.
- The GLSL `AdjustSaturation` body here is byte-identical to the one embedded in `easygl_bloom_pipeline_test.cpp`
  (`kCombineFragSrc`, lines 118-121 of that file) — the two files were clearly kept in sync deliberately.

## Missing or Weak Tests

- See F1.
- Alpha channel of the combine result is never asserted (`centPx.getAProperty()` is printed but not compared) —
  low-value gap since the hand-derived expected alpha (`1`) is the same regardless of whether the bug being guarded
  against (missing 2nd-texture-unit bind) is present or not, so it wouldn't add discriminating power; noted for
  completeness only, not elevated to its own finding.

## Positive Findings

- The chosen test colors (pure blue bloom, pure red base) and the resulting magenta-vs-blue discriminator (header
  lines 39-41) is a genuinely sharp, well-designed regression signal for the specific bug class it targets (2nd
  texture unit silently unbound) — a wrong-but-plausible implementation (base sampling as black) is unambiguously
  distinguishable from the correct one, not a fuzzy tolerance check.
- Every GL-level claim in the header comment (uniform binding, texture-unit persistence across `Bind()`/`use()`
  calls) was independently traced against `EasyGLEffectBackend`/`EasyGLSpriteBatchBackend` and found accurate.

## Final Assessment

A well-targeted, evidence-grounded regression test for the specific capability (`ShaderEffect::SetTexture` to a
non-zero unit) it was written to prove, with one honestly-disclosed but real coverage gap: the saturation-adjustment
math it also claims to port is structurally unreachable at the test's chosen parameter value, so a latent bug in
that specific GLSL translation would currently go undetected anywhere in this shard.
