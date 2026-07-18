# Audit: examples/webgpu_colored3d_test.cpp

## Metadata

- Source file: `examples/webgpu_colored3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `DrawColoredPrimitives()`/`DrawIndexedColoredPrimitives()`
  (stride-16 `VertexPositionColor`) and real depth-test test, WebGPU backend (experimental, per
  `CLAUDE.md`).
- Test executable: `cna_test_webgpu_colored3d`, CTest target `WebGPU_Colored3D`
  (`cmake/Tests/WebGpuTests.cmake:42-43`).
- XNA/FNA relevance: `GraphicsDevice.DrawUserPrimitives<VertexPositionColor>()`,
  `DrawUserIndexedPrimitives<VertexPositionColor>()`, `DepthStencilState.Default`
  (`DepthBufferEnable=true`, `DepthBufferWriteEnable=true`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`DrawUserPrimitives()` lines 692-747 — the untyped overload this file's `DrawUserPrimitives`/
  `DrawUserIndexedPrimitives` calls route through, which dispatches straight to
  `backend_->DrawColoredPrimitives()`/`DrawIndexedColoredPrimitives()`, bypassing
  `GpuDrawParams`/`DrawPrimitivesEx()` entirely), `src/CNA/Internal/Backends/WebGPU/
  WebGPUGraphicsBackend.cpp` (`QueueColoredDraw()` lines 5933-5989, `FillColoredUniforms()`
  hardcoded-white/vertex-colour-always-true path).

## Purpose

Four checks proving this backend's most basic 3D draw path: (A) `DrawUserPrimitives` (non-indexed)
renders a solid-colour quad; (B) `DrawUserIndexedPrimitives` renders the indexed counterpart; (C) a
genuine depth-test proof using two draw orders (far-then-near and near-then-far) that must both
resolve to the same (near) winner, specifically ruling out "last draw wins" painter's-algorithm
behaviour masquerading as depth testing.

## Executive Verdict

**Healthy.** Correctly identified (via direct production-code tracing, not assumption) that this file
exercises a *different* dispatch path from every other file in this batch — the untyped
`DrawUserPrimitives`/`DrawUserIndexedPrimitives` overload that bypasses `GpuDrawParams`/
`DrawPrimitivesEx()` and calls `DrawColoredPrimitives()`/`DrawIndexedColoredPrimitives()` directly —
and confirmed the backend's hardcoded-white-diffuse/always-vertex-colour-enabled fallback for that path
is exactly correct for this specific test's scenario (raw per-vertex colours, no `DiffuseColor`
override).

## Checklist Results

### API / XNA / FNA parity
`ApplyBasicEffect()` (lines 61-70) sets `World=View=Projection=Identity` and
`VertexColorEnabled=true`, matching how a minimal FNA `BasicEffect` is configured for raw
screen-space-ish colour rendering. `dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2)`
(line 85) and `dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, verts, 0, 4, indices, 0, 2)`
(line 129) use FNA's real untyped-array overload signatures.

### Behavioral correctness
Traced `GraphicsDevice::DrawUserPrimitives()` (`GraphicsDevice.cpp` lines 692-747): it packs the raw
`VertexPositionColor` array into a compact 16-byte `GpuVertex {x,y,z,r,g,b,a}` layout, uploads to a
temporary vertex buffer, and calls `backend_->DrawColoredPrimitives(...)` **directly** — it does *not*
call `currentEffect_->FillGpuDrawParams()` the way `DrawPrimitives()` (the `SetVertexBuffer`+
`DrawPrimitives` path used by four of this batch's other five files) does. Confirmed
`WebGPUGraphicsBackend::DrawColoredPrimitives()` (line 5991) calls `QueueColoredDraw(vb, nullptr, ...)`
with `params=nullptr`, which selects `FillColoredUniforms()` (hardcoded `diffuseColor=white`,
`vertexColorEnabled=true`) rather than `FillExtUniforms()` — exactly right for this test, since none
of its checks set a `DiffuseColor` and all rely on the raw per-vertex colour showing through unmodified.
This is a materially different (and *narrower*) code path than `webgpu_drawprimitivesex_test.cpp`
exercises for the same stride-16 vertex shape, and this file's own header comment (lines 2-8)
correctly attributes it to `DrawColoredPrimitives()`/`DrawIndexedColoredPrimitives()` specifically, not
to `DrawPrimitivesEx()`.

### Logic
Check C's two draw-order variants (lines 137-148) are the correct technique to rule out
"last-draw-wins" masquerading as real depth testing: both orders assert the same (near, green) winner,
which only a genuine depth-buffer `LessEqual`/`Less`-style comparison (not draw order) could produce
for both permutations. `DepthStencilState::Default` is applied (line 135) with an explicit
`ClearOptions::Target | ClearOptions::DepthBuffer` clear (line 136, 143) before each sub-case,
including a depth-buffer clear to `1.0f` (far) — correct, since a stale depth buffer from a prior
sub-case's `0.1f` near value would otherwise make every subsequent draw at `z=0.9` incorrectly fail
the depth test regardless of order.

### C++ correctness
`ApplyBasicEffect()`'s `static BasicEffect* fx = nullptr;` (line 63) is a function-local static raw
pointer, constructed once and never destroyed — a deliberate, harmless leak for a single-process
one-shot test executable (the process exits immediately after `Exit()`), consistent with this
codebase's general test-file idiom of not fussing over teardown in throwaway `Game` executables.
Flagged as a LOW/INFO observation, not a defect: if this pattern were copy-pasted into a
longer-running or repeatedly-instantiated context it would become a genuine leak, but nothing in this
file's actual usage triggers that.

### Robustness
Correctly sets `RasterizerState::CullNone` (line 109) before any draw, so triangle winding order
cannot cause a spurious back-face cull that would be misread as a depth-test failure in check C.

### Testing
Solid 4-check design covering both draw-call shapes (indexed/non-indexed) and a genuine two-permutation
depth-test proof. No redundant checks found.

### Architecture / Cross-file consistency
Confirms this backend's `DrawColoredPrimitives()`/`DrawIndexedColoredPrimitives()` fallback path
(reached from the untyped `DrawUserPrimitives` API, and also as `DrawPrimitivesEx()`'s own final
"unmatched stride/effect" fallback, `WebGPUGraphicsBackend.cpp` line 6095) is a single, shared
implementation (`QueueColoredDraw`) — not a duplicated one — parameterised only by whether a
`GpuDrawParams*` is available.

## Detailed Findings

None at HIGH or above.

## Cross-File Observations

- This is the one file in the batch whose primary dispatch target (`DrawColoredPrimitives`/
  `DrawIndexedColoredPrimitives`, reached via the untyped `DrawUserPrimitives` API) is distinct from
  `DrawPrimitivesEx()`'s `GpuDrawParams`-driven path exercised by `webgpu_drawprimitivesex_test.cpp`,
  `webgpu_alphatest3d_test.cpp`, and `webgpu_coloredtextured3d_test.cpp` — worth noting for anyone
  reading this batch's reports together, since the two paths could in principle drift independently
  (e.g. a future `DiffuseColor`-respecting fix to `DrawPrimitivesEx()`'s stride-16 branch would not
  automatically propagate to this file's `DrawUserPrimitives` path, since they are reached via
  different `GraphicsDevice` entry points even though they ultimately call the same
  `QueueColoredDraw()` — this is architecturally fine today because `FillColoredUniforms()`'s
  hardcoded values are exactly what `DrawUserPrimitives`'s FNA semantics call for, but is worth
  re-checking if `DrawUserPrimitives`'s XNA contract is ever extended to honour a bound effect's
  `DiffuseColor`).
- No `SkinnedEffect`/fog/`EnvironmentMapEffect` code paths are exercised by this file — the
  cross-cutting bugs this audit is watching for do not apply here.

## Missing or Weak Tests

None significant — the depth-test proof (check C) is genuinely more rigorous than a typical single-
order "does something show up" check, and this file does not overclaim coverage it doesn't have.

## Positive Findings

- Check C's two-permutation depth-test proof is a notably rigorous test design, not just "draw two
  things and see if the front one shows."
- Correctly and explicitly traced through to a materially different backend entry point
  (`DrawColoredPrimitives` via the untyped `DrawUserPrimitives` API) than most of this batch's other
  files, and confirmed its own hardcoded-uniform fallback is the right behaviour for this specific
  scenario rather than assuming all "stride-16 colored" tests exercise the same dispatch function.

## Final Assessment

A correct, well-constructed test with a genuinely rigorous depth-test proof and no defects found in
either its own logic or the `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` production path it
exercises.
