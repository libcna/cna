# Audit: examples/d3d9_instanced_test.cpp

## Metadata

- Source file: `examples/d3d9_instanced_test.cpp` (241 lines)
- Audit status: AUDITED (static/source-reading only — see Environment Note below)
- Subsystem: `examples-tests-d3d9` shard — hardware instancing via `SetStreamSourceFreq`
- File type: standalone `Game`-subclass executable, CTest-registered as `D3D9_Instanced`
  (`cna_test_d3d9_instanced`, `cmake/Tests/D3D9Tests.cmake:66-69`, `TIMEOUT 60 LABELS "D3D9"`).
- XNA/FNA relevance: NOXNA — real XNA 4.0 has no per-instance-aware Stock Effect vertex shader at
  all (confirmed: none of `BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/
  `SkinnedEffect`'s vendored `VSInput*` structs declare a per-instance semantic). This file exercises
  CNA's own `Instanced3D` shader and `D3D9GraphicsBackend::DrawInstancedPrimitivesEx`, a NOXNA
  extension exposed only at the internal backend-interface level (`GpuDrawParams::instanceVb`),
  not through the public `Microsoft::Xna` API surface.
- Related production code: `src/CNA/Internal/Backends/D3D9/D3D9InstancedDraw.cpp` (141 lines, the
  entire feature), `shaders/cna/Instanced3D.hlsl` (CNA's own custom shader, not vendored),
  `D3D9EffectDraw.cpp` (`DrawPrimitivesExImpl`/`DrawBasicEffectEXT`, the fallback target for Check C).

### Environment Note (per D-P4)

D3D9 is Windows-only and cannot be built or run in this Linux sandbox. This report is static/
source-reading only: every pixel-value/dispatch claim below was verified by reading the full
production call chain (`D3D9InstancedDraw.cpp`, `Instanced3D.hlsl`, `D3D9EffectDraw.cpp`) and by
hand-deriving the HLSL matrix math the shader performs, not by executing the test.

## Purpose

4-check proof of real GPU hardware instancing: (A) instance 0's own per-instance world translation
paints at its own distinct screen location with the shared `DiffuseColor`; (B) instance 1, same draw
call (`instanceCount=2`), paints at a *different* location with the *same* color, proving two
genuinely distinct instances were drawn (not one draw silently repeated); (C) `instanceVb==nullptr`
falls back to a real non-instanced draw path rather than throwing "not implemented"; (D) a normal
(non-instanced) draw issued immediately after the instanced one still renders correctly, proving
`SetStreamSourceFreq` state does not leak and corrupt every subsequent draw (D3D9 stream-frequency
state is persistent device state, and stream 0 is reused by every other draw path in this backend).

## Executive Verdict

**Healthy** — the shader math, vertex declaration, and stream-frequency reset were all independently
re-derived and traced end-to-end; every check's expected pixel value follows correctly from the
production code as written. One MEDIUM test-design observation (F1) about Check C's actual code path.

## Checklist Results

### Behavioral correctness

- Check A/B: `DrawInstancedPrimitivesEx` (`D3D9InstancedDraw.cpp:71-140`) uploads
  `Matrix::Transpose(view*projection)` in column-major form to `c0-c3` and `diffuseColor` to `c4`
  (lines 113-117), sets the 2-stream vertex declaration
  (`GetOrCreateInstancedVertexDeclarationEXT`, lines 52-69: stream 0 = `POSITION0` FLOAT3 offset 0;
  stream 1 = 4×`TEXCOORD1..4` FLOAT4 at offsets 0/16/32/48), and issues
  `SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | instanceCount)` /
  `SetStreamSourceFreq(1, D3DSTREAMSOURCE_INSTANCEDATA | 1)` — the MSDN-documented convention for
  D3D9 hardware instancing. Hand-traced `Instanced3D.hlsl`'s `VSInstanced3D` (lines 51-63): builds
  `world` from the 4 per-instance `TEXCOORD` rows exactly as the test's own `InstanceRow{row0..row3}`
  layout supplies them (`row3=(Tx,Ty,Tz,1)`), computes `worldPos=mul(float4(Position,1), world)` —
  confirmed by hand (row-vector convention: `result[j] = Σ_i Position[i]*row_i[j] + 1*row3[j]`) that
  this correctly adds the translation row to the transformed position for both the X and Y axes
  independently, i.e. instance 0's `-0.5` and instance 1's `+0.5` genuinely displace the two triangle
  copies to different NDC/screen locations while the shared `DiffuseColor` register (`c4`) is
  identical for both — exactly what Check A/B assert. The registers `ViewProj0..3` are then rebuilt
  into a `float4x4` and `mul(worldPos, viewProj)` applied, which (via the same
  transpose+column-major-write convention independently confirmed correct in the PBR draw path, see
  `d3d9_pbr_test.cpp.audit.md`) reconstructs `Position * (view*projection)` in the row-vector
  convention XNA itself uses.
- The test's own comment about sampling `(14,34)`/`(46,34)` rather than the exact translated
  fill-region interior (avoiding the triangle's 45°-diagonal rasterization boundary at `(16,32)`/
  `(48,32)`) is a sound, deliberate anti-flakiness choice, not an accidental pass.
- Check C: confirmed `DrawInstancedPrimitivesEx`'s very first statement
  (`D3D9InstancedDraw.cpp:78-82`) is `if (params.instanceVb == nullptr) { DrawIndexedPrimitivesEx(...); return; }`
  — a real fallback to the ordinary effect-dispatch cascade, not a stub. See F1 for what that cascade
  then does with this specific test's parameters.
- Check D: `SetStreamSourceFreq(0, 1)`/`SetStreamSourceFreq(1, 1)` reset (lines 138-139) run
  unconditionally after `DrawIndexedPrimitive`, before the function returns — confirmed this is not
  behind any early-return path that could skip it (the function has only the one early return, at
  line 81, which is *before* any `SetStreamSourceFreq` call is issued at all, so the reset always
  pairs with the frequency-setting calls that preceded it).

### Logic

`GetOrCreateInstancedVertexDeclarationEXT` caches `instancedVertexDecl_` (line 54: `if
(instancedVertexDecl_) return ...`), and `instancedVS_`/`instancedPS_` are similarly lazily created
and cached on the backend instance (lines 92-105) — correct one-time-creation guards, consistent with
this backend's established shader/vertex-declaration caching idiom.

### C++ correctness

`d3dVb.GetStrideEXT() > 0 ? ... : 16` (line 89) is a defensive fallback for an unset stride; not
exercised by this test (the per-vertex VB here always has an explicit stride via `SetData`), but a
reasonable guard, not a hazard.

### Memory/resource lifetime

`instancedVS_`/`instancedPS_`/`instancedVertexDecl_` are backend-lifetime `ComPtr` members (implied
by the `if (!instancedVS_)` lazy-init pattern) — standard COM RAII, released automatically on backend
destruction; no leak or dangling-pointer risk visible in this file's own usage.

### Architecture

Confirmed via `D3D9InstancedDraw.cpp`'s own header comment and by reading `D3D9EffectDraw.cpp` that
this file deliberately does **not** route through `DrawPrimitivesExImpl`'s stock-effect cascade —
consistent with D3D11's own `DrawInstancedPrimitivesEx` precedent cited in both files' comments. This
is an architecturally sound choice (real XNA has no stock instanced effect to dispatch to), not a
layering violation.

### Robustness

Check D specifically targets a real, D3D9-specific state-leak risk (`SetStreamSourceFreq` is
persistent device state, unlike most per-draw D3D9 calls) that a naive port forgetting the reset at
lines 138-139 would silently corrupt every subsequent draw on stream 0 — a well-targeted regression
test for a real, D3D9-idiom-specific hazard.

### Testing

Good coverage of the instancing-specific surface (2-instance proof, null-fallback, state-leak
regression). Not tested: `instanceCount` values other than exactly 2, or an `instanceCount` that
would require MSDN's frequency-encoding overflow behavior at very large counts — reasonable to treat
as out of scope for this file (no evidence any other backend's own instancing test covers this
either).

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — Check C's "proves the fallback is real" framing is accurate, but the specific failure it demonstrates is an unrelated pre-existing BasicEffect vertex-layout gap, not evidence the instancing fallback itself dispatches correctly for a supported combination

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-coverage / correctness-of-test
- Location/symbol: Check C block (lines 164-195); `D3D9GraphicsBackend::DrawBasicEffectEXT`
  (`D3D9EffectDraw.cpp:623-645`, the `comboOk` switch)
- Evidence: Check C's vertex buffer (`VPT{x,y,z,u,v}`, stride 20) is set up with UV coordinates
  present, but `GpuDrawParams params` is left at its class defaults except for explicitly setting
  `vertexColorEnabled=false`/`lightingEnabled=false` — `textureEnabled` (default `false`,
  `IGraphicsBackend.hpp:446`) is never set `true`, and `texture0` is never bound. `DrawBasicEffectEXT`'s
  `comboOk` switch for `stride==20` requires `!lightingEnabled && !vertexColorEnabled &&
  textureEnabled` (`D3D9EffectDraw.cpp:632`) — since `textureEnabled` stays `false`, this specific
  combination is *always* going to be `comboOk=false` and throw, for any stride-20 vertex buffer,
  regardless of whether the instancing fallback correctly routed the call. The test's own comment
  ("A textureEnabled=false/vertexColorEnabled=false/lightingEnabled=false BasicEffect combo has no
  matching CNA vertex layout") is factually accurate about *why* it throws, and the check does
  genuinely prove the fallback reaches live `DrawBasicEffectEXT` dispatch code (not a stub) rather
  than silently swallowing the call — so the test's own stated goal is met. However, this specific
  combination was never going to succeed even if every other stride/flag combination were passed
  through correctly; the check does not independently confirm that the fallback path can also
  successfully draw a *supported* BasicEffect combination via the instancing entry point (only that
  Check D's later, deliberately-different, correctly-flagged draw succeeds, which uses
  `DrawIndexedColoredPrimitives` directly, not `DrawInstancedPrimitivesEx`'s own null-`instanceVb`
  fallback branch).
- Why it matters: a regression that broke the null-`instanceVb` fallback's ability to draw a
  *supported* BasicEffect combination correctly (e.g. wrong stride threaded through, wrong world
  matrix passed) would not be caught by this test — Check C would still pass (still throws, just
  for the pre-existing unrelated reason), and Check D exercises a different draw entry point
  entirely (`DrawIndexedColoredPrimitives`, not `DrawInstancedPrimitivesEx`).
- FNA/XNA comparison: N/A — NOXNA feature, no FNA equivalent.
- Suggested future action (not implemented by this audit): add a fifth check that calls
  `DrawInstancedPrimitivesEx` with `instanceVb==nullptr` and a *supported* BasicEffect combination
  (e.g. `vertexColorEnabled=true`, stride 16, matching `kTri`'s own layout in Check D) and asserts a
  successful, correctly-colored readback — proving the fallback path draws, not just that it
  reaches real dispatch code and correctly rejects an unsupported layout.

## Cross-File Observations

- The vertex-declaration layout in `GetOrCreateInstancedVertexDeclarationEXT`
  (`D3D9InstancedDraw.cpp:56-63`) and `Instanced3D.hlsl`'s own `VSInput` (`TEXCOORD1..4`) agree
  exactly on stream/offset/semantic-index assignment — no drift between the C++ declaration and the
  HLSL source it feeds.
- `git log --oneline -- examples/d3d9_instanced_test.cpp` shows a single authoring commit
  (`4636f435 feat(plans/plan_dx9.md): close D9-83 -- real D3D9 hardware instancing via SetStreamSourceFreq`),
  consistent with the file's own single-task scope.
- This file and `d3d9_pbr_test.cpp` independently establish the same `Matrix::Transpose(...)`+
  `ToColumnMajor` "register=column, row-vector-convention" upload idiom for view/projection and
  world matrices respectively — consistent across both custom (non-Stock-Effect) CNA shaders in this
  backend.

## Missing or Weak Tests

See F1 (Check C does not prove the fallback can successfully complete a supported draw).

## Positive Findings

- Check D is a well-targeted, D3D9-idiom-specific regression test for a real hazard
  (`SetStreamSourceFreq`'s persistent device-state semantics) that a less careful port could easily
  get wrong; the production code's own reset (lines 138-139) is unconditional and correctly placed.
- The pixel-sampling coordinates in Check A/B were deliberately chosen to avoid the triangle's own
  rasterization-boundary diagonal, a genuine anti-flakiness detail rather than an arbitrary choice.

## Final Assessment

Solid instancing test with correctly-traced shader math and a real, well-targeted state-leak
regression check. The one finding (F1) is about what Check C's specific parameter combination can
and cannot prove, not a defect in the production code itself — the fallback dispatch, the shader
math, and the stream-frequency reset were all independently confirmed correct by this audit's own
static tracing.
