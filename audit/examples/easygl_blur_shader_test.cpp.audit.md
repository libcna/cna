# Audit: examples/easygl_blur_shader_test.cpp

## Metadata

- Source file: `examples/easygl_blur_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration test
- File type: C++ example/integration-test executable (`EasyGLBlurTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::ShaderEffect` (`IEffectMatrices` implementation),
  `CNA::Internal::Backends::EasyGLGraphicsBackend::DrawIndexedPrimitivesEx`/`BindCustomEffectMatrices`
  (`EasyGLGraphicsBackend.cpp` lines 4507-4524, 4564-4605), `GraphicsDevice::DrawIndexedPrimitives`/
  `applySamplerStatesToBackend` (`GraphicsDevice.cpp` lines 591-632, 1592-1604).
- XNA/FNA relevance: exercises a real 3D `GraphicsDevice::DrawIndexedPrimitives` call driven by a custom `Effect`
  (Task 1079 capability) plus per-sampler `SamplerState` overrides — real XNA 4.0 surface. The specific shader
  (`Blur.fx`) is from the Microsoft XNA Game Studio **ShipGame** sample, not FNA.
- FNA reference: N/A for the shader body — `ShipGame_4_0` is not present in the local FNA reference tree (confirmed
  by search). This audit independently re-derived every one of the 5 checks' expected pixel values from the ported
  GLSL and confirmed they match the file's own stated expectations; the underlying HLSL transcription itself could
  not be checked against Microsoft's original source.
- Main related tests: references `easygl_animsprite_shader_test.cpp` (not in this batch) as the other ShipGame
  shader ported in the same rollout.

## Purpose

Task 947: ports all 5 techniques of ShipGame's `Blur.fx` (`Color`, `ColorTexture`, `BlurHorizontal`, `BlurVertical`,
`BlurHorizontalSplit`) into a single GLSL program parameterized by a runtime `mode` uniform, and drives it through a
real indexed 3D draw (`VertexPositionTexture` quad, `View`/`Projection` set via `IEffectMatrices`). Uniquely among
this batch's files, this test's own header documents an empirically-discovered measurement problem (a real,
non-`(0.5,0.5)` sub-texel `TexCoord` at the viewport-centre sample pixel) and how the test was adapted to remain
robust to it. Correctly placed as an `easygl_`-prefixed integration test.

## Executive Verdict

**Healthy** — this is the most rigorously self-validated file in the batch: every one of its 5 checks' expected byte
values was independently re-derived by this audit from the ported GLSL and matches exactly, and the file documents
its own mutation-testing pass (deliberately breaking the premultiply line and confirming the expected check failed
with the exact predicted alternate value, then reverting). The one real HLSL-fidelity question — whether
`BlurVertical`'s lack of premultiply vs. `BlurHorizontal`'s premultiply is a genuine upstream asymmetry or a
transcription slip — is well-reasoned (cross-checked against `ScreenManager.cs`'s pass ordering) but, per Metadata,
not verifiable against a ground-truth source from this sandbox.

## Checklist Results

### API / XNA / FNA parity
`IEffectMatrices`'s `World`/`View`/`Projection` properties (`ShaderEffect::setWorldProperty`/`setViewProperty`/
`setProjectionProperty`, used at lines 184-187) are correctly named per the CNA getX/setX convention and match every
stock `Effect`'s own interface. `SamplerState::PointClamp` (line 322) and `RasterizerState::CullNone` (line 303) are
real XNA 4.0 static members, used correctly.

### Behavioral correctness — independently re-derived, all 5 checks
Traced `GraphicsDevice::DrawIndexedPrimitives` (`GraphicsDevice.cpp:591-632`): calls `applySamplerStatesToBackend()`
(line 625) — which iterates every `SamplerStateCollection` slot and calls `backend_->ApplySamplerState(i, ...)`
(`GraphicsDevice.cpp:1592-1604`) — *before* `DrawIndexedPrimitivesEx`, confirming the test's own pattern of setting
`device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;` (line 322) ahead of the draw call genuinely
reaches the backend for that draw, rather than being a no-op or requiring a separate `Apply()` step.

Traced `EasyGLGraphicsBackend::DrawIndexedPrimitivesEx`'s `params.customEffectBackend` branch (lines 4577-4596) and
its shared `BindCustomEffectMatrices` helper (lines 4512-4523): binds the effect's own compiled program and sets
`World`/`View`/`Projection` uniforms by exactly those names — matching this file's shader's own uniform name
(`g_WorldViewProj`, a single pre-combined matrix, set explicitly by the test itself via
`fx->SetUniformMat4("g_WorldViewProj", ...)`, line 316, rather than relying on the backend's separate `World`/`View`/
`Projection` uniforms) — a deliberate, documented adaptation (header lines 40-41: "uses a single pre-combined
`g_WorldViewProj` (no separate `World`)"), consistent with how the backend's generic matrix-binding is bypassed by
a shader that doesn't declare those three separate uniform names.

Re-derived all 5 expected values independently from the ported GLSL (`kFragSrc`, lines 162-216):
- **Check A** (`mode=0`): `FragColor = g_Color` directly → `(0.3,0.6,0.9,0.8)*255 = (76.5,153,229.5,204)` ≈
  `(77,153,230,204)` — matches (line 74 expectation, `close()` tolerance `±6`).
- **Check B/C** (`mode=1`, `ColorTexture`): `g_Color * texture(...)`, `g_Color=(0.5,0.5,0.5,1.0)`. Left-half texel
  `(100,150,200,255)` → `*0.5 = (50,75,100,255)` (Check B, matches). Right-half `(50,25,75,255)` → `*0.5 =
  (25,12.5,37.5,255)` ≈ `(25,12,37,255)` int-truncated (Check C, matches).
- **Check D** (`mode=2`, `BlurHorizontal`): re-derived the premultiplied-sum math independently:
  left color `(220,0,0,128)` normalized `(0.8627,0,0)`, alpha `0.50196`; premultiplied R contribution per left tap =
  `0.8627*0.50196=0.4331`; `5` left taps → `2.1655`. Right color `(0,220,0,255)` normalized G `0.8627`, alpha `1.0`;
  `6` right taps → `5.1762`. Sum `/11`: R `0.19686*255≈50`, G `0.47056*255≈120`. Alpha is **not** premultiplied in
  this shader mode (`c.xyz *= c.w` only touches `.xyz`), so alpha sums raw: `(5*0.50196+6*1.0)/11=0.77362*255≈197`
  → `(50,120,0,197)` — matches exactly (line 86 expectation).
- **Check E** (`mode=3`, `BlurVertical`, no premultiply at all): top color `(220,0,0,255)`, 6 taps; bottom
  `(0,220,0,255)`, 5 taps → `(6*220)/11=120`, `(5*220)/11=100` → `(120,100,0,255)` — matches exactly (line 92
  expectation).

All 5 re-derivations match the file's own stated expected values exactly — this is a genuinely correct,
non-approximated test, not just "renders something and hopes."

### Logic
`DrawOnce(mode, tex, pixelSize, gColor)` (lines 293-336) is a clean, reusable single-draw-and-readback helper called
5 times with different parameters — no duplicated draw logic across the 5 checks, unlike some sibling files in this
batch that inline each pass separately.

### Memory/resource lifetime
`vb_`/`ib_` are `std::unique_ptr<VertexBuffer>`/`std::unique_ptr<IndexBuffer>`, constructed once in `Initialize()`
and reused across all 5 `DrawOnce()` calls — correct, efficient resource reuse (no re-allocation per check).

### C++ correctness
The `mode==4` (`BlurHorizontalSplit`) branch in `kFragSrc` (lines 200-215) divides by `color.w` (line 214:
`FragColor = color / color.w;`) — if no tap satisfies either half of the split-screen condition (structurally
impossible here given the loop always includes the `i=0` tap matching its own half), this would be a division by
zero; not a live bug given the loop's own structure guarantees at least one contributing tap, but worth noting this
mode is only statically reviewed, not runtime-exercised (see Missing or Weak Tests) — a divide-by-zero in an
untested code path is a real, if currently theoretical, latent risk class.

### Performance
N/A — single-frame test.

### Architecture
Single GLSL program with a `mode` uniform correctly mirrors the file's own documented adaptation strategy (same
pattern as `PostprocessEffect.Fx`'s techniques, per header line 38-39) for consolidating multiple HLSL techniques
that share one vertex shader — architecturally consistent with the rest of this shard's shader-porting approach.

### Robustness
`fx->Apply()` (line 311) is called unconditionally without checking `IsEffectValid()` first inside `DrawOnce` — the
actual validity check happens once in `Draw()` before any `DrawOnce()` calls (lines 344-349), so this is safe in
practice (all 5 calls only happen after the guard), not a live gap.

### Testing
This file is itself a test; see the `BlurHorizontalSplit` coverage gap below.

## Detailed Findings

No MEDIUM-or-higher findings. One LOW item:

### F1 — `BlurHorizontalSplit` (`mode=4`) is explicitly not runtime-checked, and its `color / color.w` is an
  unguarded potential divide-by-zero in dead-in-test code

- Severity: LOW
- Confidence: HIGH (for the divide-by-zero *shape*); MEDIUM (for whether it's reachable in the real sample's usage)
- Category: test-coverage / robustness
- Location/symbol: `kFragSrc` mode 4 branch, lines 200-215; header's own disclosure at lines 94-99
- Evidence: the file's own header explicitly and honestly states this mode is "ported 1:1 and reviewed line-by-line,
  but not independently runtime-checked here," citing lower risk and shared logic with the already-verified
  `BlurHorizontal` core. `color.w` accumulates only from taps whose `tc.x` falls on the same side of `split=0.499`
  as the base `TexCoord.x` — for any real on-screen quad this is virtually always non-empty (`i=0`'s own tap always
  qualifies), so the divide-by-zero is a theoretical edge (e.g. a `TexCoord.x` sitting exactly on `0.499` combined
  with specific `g_PixelSize` values) rather than a demonstrated live bug.
- Why it matters: this is the *only* one of the 5 ported techniques with zero runtime pixel verification anywhere in
  this batch — a genuine, if low-probability, blind spot for both correctness (a subtle boundary-condition bug in
  the split-gating logic) and robustness (the unguarded division).
- FNA/XNA comparison: N/A (sample-project shader, not FNA).
- Related files: none in this batch exercise `BlurHorizontalSplit`.
- Suggested future action (not implemented by this audit): add a 6th check exercising `mode=4` with a source texture
  whose `TexCoord.x` straddles `split`, verifying both split boundary and the gate-instead-of-accumulate visual
  effect.

## Cross-File Observations

- This file's `SamplerState::PointClamp` override (line 322) and its rationale (needing exact single-texel taps for
  an 11-sample kernel, vs. the `easygl_bloom_gaussianblur_test.cpp` sibling's default linear filtering, which that
  file's own report notes as relying on bilinear blending margins instead) shows two different, both-valid testing
  strategies for verifying array/loop-driven texture sampling in this shard — worth citing together if a shared
  "how to test a multi-tap shader" guideline is ever written for this codebase.
- Confirms (independently, via `GraphicsDevice.cpp:625`/`1592-1604`) that per-slot `SamplerState` overrides set via
  `device.getSamplerStatesProperty()[i] = ...` genuinely reach the backend before every `DrawIndexedPrimitives`
  call, not just at `Apply()` time — relevant context for any other file in this shard's audits that also touches
  `SamplerState`.

## Missing or Weak Tests

- See F1 — `BlurHorizontalSplit` (`mode=4`) has zero runtime verification in this repository as far as this batch
  shows.

## Positive Findings

- Exceptionally rigorous self-verification: the file documents (and this audit independently reproduced) a
  mutation test — removing the premultiply line and confirming Check D fails with the exact alternate predicted
  value (`100` instead of `50`) while Check E is correctly unaffected — genuine evidence the test has real
  discriminating power, not just "compiles and shows a plausible color."
- Honest, well-reasoned documentation of a real measurement artifact (sub-texel `TexCoord` offset from the assumed
  `(0.5,0.5)`) and a principled fix (switching to point sampling + wide flat color regions) rather than papering
  over it with looser tolerances alone.
- All 5 runtime-checked expected values were independently re-derived by this audit from the shader source and
  match exactly — the strongest evidence-to-claim ratio of any file in this batch.

## Final Assessment

The most rigorously self-validated test in this batch: every runtime-checked expected value was independently
confirmed correct, and the file's own mutation-testing narrative demonstrates real discriminating power rather than
being an assertion of it. The sole gap — `BlurHorizontalSplit` left entirely unverified at runtime, with a
theoretical unguarded division — is honestly disclosed by the file itself, not discovered despite the file's claims.
