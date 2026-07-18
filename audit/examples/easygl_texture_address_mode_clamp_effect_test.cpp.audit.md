# Audit: examples/easygl_texture_address_mode_clamp_effect_test.cpp

## Metadata

- Source file: `examples/easygl_texture_address_mode_clamp_effect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `DualTextureEffect`/`DrawUserPrimitives`
  `TextureAddressMode::Clamp` pixel test
- File type: C++ example/integration-test executable (`TextureAddressModeClampEffectTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::DualTextureEffect`
  (`DualTextureEffect.cpp`), `GraphicsDevice::applySamplerStatesToBackend()`
  (`GraphicsDevice.cpp:1592-1604`, called from every `Draw*` entry point including
  `DrawUserPrimitives`), `EasyGLGraphicsBackend`'s dual-texture GLSL program
  (`EnsureDualTextured3DProgram`, `EasyGLGraphicsBackend.cpp:3009-3070`)
- XNA/FNA relevance: `GraphicsDevice.SamplerStates[0]`, `DualTextureEffect` (two-texture modulate-×2 blend),
  `SamplerState.PointClamp`. Judged against `FNA/src/Graphics/GraphicsDevice.cs` (`SamplerStates` applied
  before each draw) and the vendored reference `DualTextureEffect.fx` pixel shader
  (`src/CNA/Internal/Backends/D3D9/shaders/xna/DualTextureEffect.fx`, `EXEMPT`
  `vendored-verbatim-stock-effect`, consulted here only as an XNA-behavior reference, not itself audited).
- Main related tests: this file (Task 294); direct counterpart of `easygl_texture_address_mode_test.cpp`
  (Task 269, `SpriteBatch`-based Clamp/Wrap) applied to the independent `DrawUserPrimitives`+`Effect` code
  path; sibling of `easygl_texture_address_mode_mirror_effect_test.cpp` (Task 296, same code path, Mirror
  instead of Clamp).

## Purpose

Verifies that `GraphicsDevice.SamplerStates[0]` (as opposed to `SpriteBatch`'s own sampler-state plumbing) is
honored when drawing with `DualTextureEffect` via `DrawUserPrimitives` — a genuinely different production code
path from the `SpriteBatch`-focused Task 269 test. Draws a full-screen quad with a 2×1 (Red|Green) `texture0`
and a solid-white 1×1 `texture1` (chosen specifically so the `DualTextureEffect` multiply leaves `texture0`'s
sampled color the dominant signal), assigns `SamplerState::PointClamp` to slot 0, and samples at `U=1.25`
where Clamp and Wrap disagree. Placement matches the shard convention.

## Executive Verdict

**Healthy** — traced the complete `GraphicsDevice.SamplerStates[0]` → `applySamplerStatesToBackend()` →
`ApplySamplerState` → GL sampler-object binding chain and confirmed it is genuinely exercised by
`DrawUserPrimitives` (not a dead/no-op path for this call), confirmed `DualTextureEffect`'s actual GLSL
(`base.rgb*=2.0` before multiplying by `texture1`) matches real XNA's vendored reference shader's `color.rgb *=
2` byte-for-byte semantically, and confirmed the white `texture1`/1×1 choice genuinely isolates the test to
slot 0's sampler as claimed, including correctly accounting for the ×2 multiply's effect on clamping behavior.
No correctness defect found.

## Checklist Results

### API / XNA / FNA parity
`DualTextureEffect(device)`, `setTextureProperty`/`setTexture2Property`/`setWorldProperty`/`setViewProperty`/
`setProjectionProperty`/`Apply()` (lines 78-84) are real XNA members with correct C++-property-getter/setter
naming. `device.getSamplerStatesProperty()[0] = SamplerState::PointClamp` (line 76) uses the real
`GraphicsDevice.SamplerStates` indexer analogue. `RasterizerState::CullNone` (line 97) and
`DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2)` (line 98) are real XNA members.

### Behavioral correctness — independently traced the full sampler-state call chain
Confirmed `GraphicsDevice::applySamplerStatesToBackend()` (`GraphicsDevice.cpp:1592-1604`) iterates all
`SamplerStateCollection::MaxSamplers` slots and calls `backend_->ApplySamplerState(i, ...)` for each — and that
this method is called from `DrawUserPrimitives`'s implementation (confirmed via `grep` across
`GraphicsDevice.cpp`: `applySamplerStatesToBackend()` appears at every one of the device's `Draw*` entry
points, including the `DrawUserPrimitives` overload family). This means slot 0's `PointClamp` assignment (line
76) is genuinely applied immediately before this test's draw call, not merely stored inertly.

Confirmed `EasyGLGraphicsBackend::ApplySamplerState`'s `TextureAddressMode → GL wrap` mapping
(`Clamp=1→ClampToEdge`, lines 2127-2134) is the same mapping already verified for the `SpriteBatch`-based
sibling tests — this file exercises the identical GL-level behavior through the alternate
`GraphicsDevice.SamplerStates[]` entry point rather than `SpriteBatch`'s own `Set*` calls, which is genuinely
non-redundant coverage of a different code path reaching the same backend method.

Confirmed texture-unit binding: `EasyGLGraphicsBackend`'s dual-texture draw path binds `texture0` to GL texture
unit 0 and `texture1` to unit 1 (`glActiveTexture(Texture1)` then bind `texture1`, then `glActiveTexture(Texture0)`
then bind `texture0`, `EasyGLGraphicsBackend.cpp:4172-4177`) — confirming slot 0's `ApplySamplerState` (bound
to sampler unit 0) genuinely governs `texture0`'s (i.e., `patternTex_`'s) sampling, matching the test's
intent that `SamplerState::PointClamp` on slot 0 controls the pattern texture specifically.

Traced `DualTextureEffect`'s actual GLSL fragment shader (`EnsureDualTextured3DProgram`,
`EasyGLGraphicsBackend.cpp:3050-3052`): `base=texture(uTexture,vUV); base.rgb*=2.0; FragColor=base*
texture(uTexture2,vUV)*uDiffuseColor;` — and cross-checked this against the vendored real-XNA reference
`DualTextureEffect.fx`'s `PSDualTexture` (`color.rgb *= 2; color *= overlay * pin.Diffuse;`,
`DualTextureEffect.fx:97-101`, consulted as a reference only, not itself in this audit's scope) — confirmed
algebraically identical: sample, ×2 the first texture's RGB, multiply by the second texture and diffuse. This
means the test's header comment ("texture1 is white so the multiply leaves tex0's colour unchanged") is a
reasonable simplification of the real formula (the actual computation transiently produces `(2,0,0)`/`(0,2,0)`
before the GPU implicitly saturates on write to the 8-bit backbuffer) but is **functionally accurate for this
test's purpose**: since the non-dominant channels of `patternTex_`'s red/green texels are exactly `0`, doubling
followed by clamp-to-`[0,1]` reproduces the same pure Red/Green qualitative result the test's `isGreen`/`isRed`
checks require, so the ×2 factor does not change which of the two threshold checks (line 106-107) fires.

Re-derived the `U=1.25` clamp-vs-wrap texel result from the same texel-center analysis already verified in the
`SpriteBatch`-based sibling: raw `u=1.25` under `PointClamp` clamps to `1.0`, resolving to texel 1 (Green,
distance `0.25` vs. texel 0's distance `0.75` from the clamped coordinate) — matching this test's expected
**GREEN** result (`isGreen` check, line 106, `result_=0` on match, line 113).

### Logic
`device.setRasterizerStateProperty(RasterizerState::CullNone)` (line 97) is preceded by the comment "Task 896
finding: this quad's winding is CCW/back-facing under CNA's real default RasterizerState — needs CullNone" —
independently recomputed the quad's winding: vertices `(-1,1)→(-1,-1)→(1,-1)` yield a cross-product `z=+4`
(mathematically counter-clockwise in NDC space), and since the device's real default is
`RasterizerState::CullCounterClockwise` (`GraphicsDevice.cpp:162`), a quad that is CCW in NDC-math orientation
but rendered under a viewport/rasterizer convention that treats it as back-facing would indeed require
`CullNone` — consistent with the documented finding shared across this batch's full-screen-quad tests, not a
new or unexplained workaround.

### Memory/resource lifetime
`patternTex_`/`whiteTex_` are `Texture2D` value members constructed once in `Initialize()`; `done_` correctly
guards `Draw()` against re-executing the test body on subsequent frames. No lifetime concern.

### C++ correctness
Threshold checks (`isGreen`/`isRed`, lines 106-107) use `>=200`/`<=50` bounds — reasonable, generous tolerance
for point-filtered (no blending) sampling; independently confirmed these thresholds correctly discriminate
pure Red `(255,0,0)` from pure Green `(0,255,0)` with no overlap.

### Performance
N/A — single-frame draw, no hot path.

### Robustness
No invalid-input path exercised; correct scope for a positive-path sampler-state test.

### Testing
This file is itself a test; see Missing or Weak Tests.

## Detailed Findings

No HIGH/CRITICAL findings.

## Cross-File Observations

- Shares the V-axis-never-exercised gap already recorded for this test family (see
  `easygl_texture_address_mode_test.cpp.audit.md` F1): the quad's V coordinates never leave `[0,1]`
  (`Vector2(0,1)`/`Vector2(0,0)`/`Vector2(2,0)`/`Vector2(2,1)`, lines 88-93 — only U varies beyond `[0,1]`), so
  `SamplerState::PointClamp`'s `AddressV=Clamp` setting is inert in this test. Not re-scored as a new finding
  here.
- Shares the "Task 896 finding" `RasterizerState::CullNone` requirement/comment with every other full-screen-quad
  test in this batch and elsewhere in the shard (already a recognized, tracked pattern per other audited files
  in this shard, e.g. `easygl_basiceffect_fog_test.cpp.audit.md`).
- Genuinely non-redundant relative to `easygl_texture_address_mode_test.cpp` (Task 269): exercises
  `GraphicsDevice.SamplerStates[]` → `applySamplerStatesToBackend()`, a different production entry point than
  `SpriteBatch`'s own `SetSamplerFilter`/`SetSamplerAddressMode` → `FlushBatch`'s `ApplySamplerState` call —
  confirmed both ultimately reach the same `EasyGLGraphicsBackend::ApplySamplerState` method, but via
  independent call chains that could each regress separately.
- Directly paired with `easygl_texture_address_mode_mirror_effect_test.cpp` (Task 296) — same code path, quad
  geometry, and texture setup, differing only in the sampler mode under test and the resulting expected color.

## Missing or Weak Tests

- V-axis addressing is never exercised (shared gap).
- No case exercises `DualTextureEffect` with a `VertexPositionColorTexture` (stride-24, vertex-color-aware
  variant, `EnsureDualTexturedColored3DProgram`) under a non-default sampler state — this test only uses the
  stride-20 `VertexPositionTexture` variant; the two shader variants share the same texture-sampling logic, so
  this is a low-priority gap, but the two program variants are technically independent GLSL compilations that
  could diverge.
- No case verifies `texture1`'s own sampler slot (slot 1) is unaffected by slot 0's `SamplerState` assignment
  (i.e., that `ApplySamplerState` is genuinely per-slot and not accidentally global) — the white/1×1 `texture1`
  choice makes this untestable by pixel output in this file by design, but no other file in this batch closes
  this specific cross-slot-isolation gap either.

## Positive Findings

- Independently traced and confirmed the entire `GraphicsDevice.SamplerStates[0]` → `ApplySamplerState` → GL
  sampler-binding call chain is genuinely exercised by this test's `DrawUserPrimitives` call, not a dead
  parameter.
- Cross-checked `DualTextureEffect`'s actual GLSL implementation against the vendored real-XNA reference
  shader (`DualTextureEffect.fx`) and confirmed the `×2` modulate-multiply behavior is a faithful, intentional
  match to real XNA semantics, not an unexplained deviation — and confirmed this detail does not undermine the
  test's own simplified claim about `texture1`'s isolating effect.
- The white/1×1 `texture1` design genuinely and correctly isolates this test to slot 0's sampler state, as
  claimed, verified via the texture-unit-binding trace.

## Final Assessment

A well-targeted, thoroughly cross-checked test for `GraphicsDevice.SamplerStates[0]`'s effect on a
`DrawUserPrimitives`+`DualTextureEffect` draw — the sampler-state call chain, the GL address-mode mapping, and
the `DualTextureEffect` shader's fidelity to real XNA's reference implementation were all independently traced
and confirmed correct. The only gap is the V-axis blind spot shared across this test family.
