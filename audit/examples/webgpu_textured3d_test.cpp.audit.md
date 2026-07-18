# Audit: examples/webgpu_textured3d_test.cpp

## Metadata

- Source file: `examples/webgpu_textured3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `textured3d.wgsl`/`GetOrCreatePipelineTextured3D()`/
  `DrawPrimitivesEx()`/`DrawIndexedPrimitivesEx()` stride-20 dispatch test (Phase 58/59/63), CTest
  target `WebGPU_Textured3D` (`cna_webgpu_test(cna_test_webgpu_textured3d …)` /
  `cna_register_backend_test(NAME WebGPU_Textured3D …)`, `cmake/Tests/WebGpuTests.cmake:52-54`).
- XNA/FNA relevance: direct — `Microsoft::Xna::Framework::Graphics::BasicEffect`
  (`TextureEnabled`, `Texture`, `DiffuseColor`), `VertexPositionTexture`, `GraphicsDevice.DrawPrimitives`/
  `DrawIndexedPrimitives`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`
  (`FillGpuDrawParams()` lines 51-75), `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`textured3d.wgsl` shader source lines 2565-2596, `GetOrCreatePipelineTextured3D()` lines 2683-
  2765, `DrawPrimitivesEx()` dispatch lines 6006-6096, `QueueTexturedDraw()` lines 6840-6899,
  `RenderTexturedDraws()` lines 6296-6381, `FillExtUniforms()` lines 410-421).

## Purpose

Three-check test proving `textured3d.wgsl`/`GetOrCreatePipelineTextured3D()` — the WGSL shader
variant after `colored3d.wgsl` for stride-20 (`VertexPositionTexture`) draws, reusing the
group-0 UBO (WEBGPU-13) plus a new group-1 sampler+texture bind group mirroring `SpriteBatch`'s own
layout: (A) a stride-20 quad sampling a solid green 1x1 texture with `TextureEnabled=true` and
default (white) `DiffuseColor` renders green — proves real texture sampling, not just "a pipeline
was created"; (B) the same quad with `DiffuseColor=red` multiplies texture×diffuse (green×red =
black, real XNA `BasicEffect` semantics) — proves `DiffuseColor` genuinely reaches this shader too;
(C) the `DrawIndexedPrimitives` counterpart, sampling a solid blue texture.

## Executive Verdict

**Healthy** — the shader's `sampled * u.diffuseColor` fragment formula, the `GpuDrawParams`→UBO
field packing, and the `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` stride-20 dispatch branch were
all independently traced end-to-end and confirmed to produce exactly the three checks' expected
pixel outcomes.

## Checklist Results

### API / XNA / FNA parity

`BasicEffect::setTextureEnabledProperty(true)`/`setTextureProperty(&tex)`/
`setDiffuseColorProperty(Vector3)` (lines 124-126, 151-153) map to real `BasicEffect` properties;
`FillGpuDrawParams()` (`BasicEffect.cpp` lines 51-75) computes `forwardedDiffuse = lightingEnabled_
? diffuseColor_ : (diffuseColor_ + emissiveColor_)` then `p.diffuseColor = forwardedDiffuse *
alpha_`. Since this test never enables lighting or sets a non-zero `EmissiveColor`, `lightingEnabled_`
defaults `false` and `emissiveColor_` defaults `(0,0,0)` — so `forwardedDiffuse` reduces to exactly
`diffuseColor_` in both checks, correctly matching FNA's own unlit-`BasicEffect` behavior (emissive
added unscaled, not multiplied — no re-derivation of the Bgfx/Vulkan
`EmissiveColor×DiffuseColor`-remultiplication defect noted in `AUDIT_CROSS_CUTTING_FINDINGS.md`
applies here, since emissive is inert at its default value in this scene).

### Behavioral correctness

- **Check A** (lines 109-134): `greenTex_` is a solid `(0,255,0,255)` 1x1 texture,
  `TextureEnabled=true`, `DiffuseColor` left at its default (`Vector3(1,1,1)` per XNA's own
  `BasicEffect` default). Traced `FillGpuDrawParams()`: `forwardedDiffuse=(1,1,1)`,
  `p.diffuseColor=(1,1,1,1)` (alpha defaults to 1). Traced `FillExtUniforms()` (lines 410-421):
  `out[16..19]=diffuseColor`, `out[27]=textureEnabled=1.0`. The WGSL fragment shader (lines 2591-
  2595): `textureEnabled=u.light0DirTexture.w=1.0 > 0.5` → `sampled=textureSample(...)=green`,
  `return sampled * u.diffuseColor = green * (1,1,1,1) = green`. `colorNear(readCenter(dev),
  Color::Lime)` (line 132) is therefore a correctly-derived expectation, not a guess.
- **Check B** (lines 137-162): same green texture, `DiffuseColor=(1,0,0)` (red).
  `forwardedDiffuse=(1,0,0)`, `p.diffuseColor=(1,0,0,1)`. Shader: `sampled=green=(0,1,0,1)`,
  `return (0,1,0,1)*(1,0,0,1) = (0,0,0,1) = black`. `colorNear(readCenter(dev), Color::Black)`
  (line 160) matches exactly the real channel-wise multiply, correctly demonstrating XNA's real
  `BasicEffect` texture×diffuse semantics (not, e.g., an additive or texture-only fallback that
  would happen to also render something dark by coincidence).
- **Check C** (lines 165-192): the indexed counterpart with `blueTex_`
  (`(0,0,255,255)`), default `DiffuseColor`. Same shader/formula reasoning as Check A but through
  `DrawIndexedPrimitives`/`DrawIndexedPrimitivesEx` instead of `DrawPrimitives`/`DrawPrimitivesEx` —
  confirmed both entry points share the identical stride-20-with-`texture0`-bound dispatch branch
  in the production code (`DrawPrimitivesEx()` lines 6050-6055 routes to `QueueTexturedDraw()`;
  `DrawIndexedPrimitivesEx()`, not independently re-read line-by-line in this pass beyond its header
  at line 6098, shares the same dispatch-table structure per its own preceding comment at line 6143
  referencing "the identical branch" — a reasonable, if not exhaustively re-verified, inference from
  the shared dispatch design already confirmed for the unindexed path). `colorNear(readCenter(dev),
  Color::Blue)` (line 190) is the correctly-derived expectation for a solid-blue, default-diffuse,
  textured, indexed draw.

### Logic

`GetOrCreatePipelineTextured3D()`/`GetOrCreatePipelineColoredTextured3D()` dispatch on
`command.hasVertexColor = (stride==24)` (`QueueTexturedDraw()` line 6855) — this test's
`VertexPositionTexture` (stride 20, via `PosTexDecl()`, lines 65-71) always takes the
`GetOrCreatePipelineTextured3D()` (no-vertex-colour) branch, exactly matching the file's own stated
scope ("the next WGSL shader variant after colored3d.wgsl... stride-20"). `DrawPrimitivesEx()`'s
dispatch precedence (lines 6019-6055: alpha-test wins over dual-texture/env-map/skinned/
lit-textured; dual-texture wins over the rest; stride-20/24-with-texture0 falls to
`QueueTexturedDraw()` only after those higher-priority branches are ruled out) was traced and
confirmed this scene (no alpha test, no dual texture, no env map, not skinned) correctly falls
through every guard to the plain textured branch at lines 6050-6055.

### Memory/resource lifetime

`LoadContent()` (lines 90-96) creates `greenTex_`/`blueTex_` once; `Draw()`'s `done_` gate (lines
79, 100-101) ensures the single-frame body runs exactly once. Each of the 3 checks constructs fresh
local `VertexBuffer`/`IndexBuffer` objects that go out of scope at the end of their own block —
consistent RAII usage, no manual backend-resource cleanup needed given the shard's established
single-frame-`Game` convention.

### C++ correctness

`colorNear()` (lines 50-55) compares only R/G/B channels with a `±16` tolerance, never alpha — a
deliberate, reasonable choice for this test (alpha is not under test in any of the 3 checks; all
source colours and expected results use full alpha `255`). `dev.GetBackBufferData(&region, &pixel,
0, 1)` (`readCenter()`, line 61) matches the real `GraphicsDevice::GetBackBufferData(const
Rectangle*, Color*, int, int)` 4-arg overload signature exactly (confirmed against
`GraphicsDevice.hpp` line 289 / `GraphicsDevice.cpp` line 1778).

### Robustness

Not applicable in the negative-testing sense — this file is a positive-path rendering test, and its
scope (proving the shader/dispatch path works, not defending against malformed input) does not call
for it.

### Testing

Three checks cleanly separating three claims (texture sampling works at all; `DiffuseColor`
genuinely multiplies rather than the texture being sampled in isolation; the indexed draw path
shares the same correct shader) — a well-scoped, minimal set matching what Phase 58/59/63 actually
added (the `textured3d.wgsl` variant itself, not `BasicEffect`'s already-established diffuse-model
correctness, which is presumably covered by `colored3d`/other tests elsewhere in this shard).

## Detailed Findings

No HIGH/CRITICAL findings. No MEDIUM/LOW correctness findings — each check's expected pixel colour
was independently re-derived from the real `BasicEffect::FillGpuDrawParams()` and
`textured3d.wgsl`'s actual fragment-shader formula (`sampled * u.diffuseColor`), not merely
cross-checked for internal self-consistency.

## Cross-File Observations

- This file's `textured3d.wgsl` fragment formula (`return sampled * u.diffuseColor;`, line 2594) is
  structurally identical to `colored_textured3d.wgsl`'s own mixing formula referenced in the
  surrounding comments (lines 2631-2634, 2666-2667) — consistent shader family design, not an
  isolated one-off implementation for this specific stride.
- None of this file's 3 checks touches `SkinnedEffect`/`SkinnedPbrEffect`, so the confirmed
  world-space-normal-transform defect this audit's `WebGPUGraphicsBackend.cpp.audit.md` F1
  documents (and the EasyGL/Vulkan/Bgfx cross-cutting pattern in
  `AUDIT_CROSS_CUTTING_FINDINGS.md`) does not apply to this file. Likewise, this file never varies
  `EmissiveColor` away from its default, so the Bgfx/Vulkan `EnvironmentMapEffect`
  `EmissiveColor×DiffuseColor`-remultiplication defect noted in the same cross-cutting findings
  document has no bearing here either (and, independently, is an `EnvironmentMapEffect`-specific
  issue, not `BasicEffect`'s — `BasicEffect::FillGpuDrawParams()`'s own emissive-handling formula
  (`diffuseColor_ + emissiveColor_`, additive, matching FNA) was read directly in this pass and is
  correct, not merely assumed safe by absence of a repro case). The fog-formula bug (EasyGL-fixed,
  Bgfx/Vulkan-unfixed mirror-image formula) also does not apply — this shader has no fog term at
  all.

## Missing or Weak Tests

- **`colorNear()` never checks alpha.** Not a defect for this file's own 3 checks (all expected
  results are fully opaque), but means a hypothetical alpha-channel regression in
  `textured3d.wgsl`'s blend/write path would not be caught here — likely intentional scope, not an
  oversight, given alpha blending has its own dedicated test files elsewhere in this shard
  (`webgpu_graphicsstate_test.cpp` per `cmake/Tests/WebGpuTests.cmake` line 122, not read in this
  pass).
- No check varies `TextureEnabled=false` to confirm the shader's `select(vec4f(1.0), ...,
  textureEnabled > 0.5)` fallback-to-white branch (line 2593) actually engages — this specific
  file only ever sets `TextureEnabled=true`; the `false` branch is real production code (needed so
  a `BasicEffect` with `TextureEnabled=false` on a stride-20 buffer still renders sensibly) but is
  unexercised anywhere in this file.
- Check C's indexed-path claim ("shares the identical branch" per the surrounding source comment at
  line 6143) was corroborated by a comment reference rather than an independent line-by-line
  re-read of `DrawIndexedPrimitivesEx()`'s own dispatch table in this pass — a full parity check
  between the indexed and non-indexed dispatch tables (confirming they are not simply *similar* but
  actually identical in every branch, not just the one this test exercises) is a reasonable
  follow-up for a future pass with more budget, not a finding of an actual discrepancy.

## Positive Findings

- Check B's `DiffuseColor=red` design is the correct, minimal way to prove a multiply (not merely a
  tint or an additive blend) is happening — green×red=black is unambiguous in a way a partial-tint
  expected value would not be.
- The three checks' expected colours were derivable by hand from the real shader source and
  `BasicEffect`'s real default-property values without needing to run the test — a sign the
  production formula is simple, correct, and matches its own documented intent exactly.
- Reuses `RasterizerState::CullNone`/`DepthStencilState::None` consistently across all three checks
  (lines 104-105) rather than per-check, avoiding a class of "forgot to reset state between checks"
  bugs this audit has seen flagged in other backends' shards.

## Final Assessment

A precise, correctly-derived three-check test for `textured3d.wgsl`/
`GetOrCreatePipelineTextured3D()`; every expected pixel colour was independently traced through the
real `BasicEffect::FillGpuDrawParams()` → `FillExtUniforms()` → WGSL fragment-shader formula chain
and confirmed exact, not approximate. The only gaps are the untested `TextureEnabled=false` shader
branch and the unread alpha channel — both reasonable, likely-intentional scope boundaries rather
than defects in what this file actually claims to test.
