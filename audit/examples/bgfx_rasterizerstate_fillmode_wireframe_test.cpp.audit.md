# Audit: examples/bgfx_rasterizerstate_fillmode_wireframe_test.cpp

## Metadata

- Source file: `examples/bgfx_rasterizerstate_fillmode_wireframe_test.cpp` (167 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `RasterizerState.FillMode = WireFrame` pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_test_bgfx_rasterizerstate_fillmode_wireframe` / `Bgfx_RasterizerState_FillMode_WireFrame`,
  `cmake/Tests/BgfxTests.cmake:700-703`)
- XNA/FNA relevance: direct — `Microsoft.Xna.Framework.Graphics.FillMode`,
  `RasterizerState.FillMode`.
- Related production code: `include/Microsoft/Xna/Framework/Graphics/FillMode.hpp`,
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`ApplyRasterizerState()` line 1791,
  `ExpandWireframeIndices()` lines 1799-1844, consumers in `DrawColoredPrimitives()`/
  `DrawIndexedColoredPrimitives()`/the `Ex` overloads).

## Purpose

Task 766: verifies `RasterizerState.FillMode = FillMode::WireFrame` actually suppresses triangle
interior rasterization on Bgfx. Since bgfx has no native polygon-fill-mode toggle (unlike D3D9/Vulkan,
which the file's header confirms via each backend's native mechanism), the CNA Bgfx backend emulates it
by re-expanding the triangle's vertex/index stream into a line-list of its 3 edges
(`ExpandWireframeIndices`) and drawing with `BGFX_STATE_PT_LINES` instead of the solid-fill topology.
3-check structure: (A) solid baseline must be RED (interior filled — validates the reference geometry
before testing the property itself), (B) `WireFrame` must show BACKGROUND at the sampled centre pixel
(interior not rasterized, only the thin edge lines, which don't cover the centre of this particular
triangle), (C) reset to `Solid` must show RED again (rules out "WireFrame gets stuck permanently").

## Executive Verdict

**Healthy.** Independently traced `ExpandWireframeIndices()`'s edge-list construction and confirmed it
produces exactly the 3 edges per triangle (`a-b`, `b-c`, `c-a`) needed for a correct wireframe outline,
and confirmed the file's comparative claim about D3D9/Vulkan having a native fill-mode toggle
(spot-checked: `VulkanGraphicsBackend.cpp` does set `rs.polygonMode = wireframe ? VK_POLYGON_MODE_LINE :
VK_POLYGON_MODE_FILL` natively, consistent with the comment).

## Checklist Results

### API / XNA / FNA parity
`FillMode::Solid`/`FillMode::WireFrame` map to `ApplyRasterizerState()`'s `fillMode == 1` check
(`wireframe_ = (fillMode == 1);`) — a direct, correct 2-value mapping matching XNA's `FillMode` enum.

### Behavioral correctness
Traced `ExpandWireframeIndices()` line-by-line: for `PrimitiveType::TriangleList`, it reads each
triangle's 3 source indices (`readSrc(3t)`, `readSrc(3t+1)`, `readSrc(3t+2)`) and emits edges `(a,b)`,
`(b,c)`, `(c,a)` into a `bgfx::TransientIndexBuffer`, sized `primitiveCount*3*2` indices (3 edges × 2
indices/edge per triangle) — correct topology for a wireframe outline. The function is guarded
(`if (primitive != TriangleList && primitive != TriangleStrip) return false;`), correctly leaving
line/point primitives alone since "wireframe" is a no-op concept for them, matching the file's own
header comment. Confirmed `useWireframe` (in `DrawColoredPrimitives`, the code path this test's
`DrawPrimitives(TriangleList, 0, 1)` after `SetVertexBuffer` actually reaches) gates both the index
buffer swap and the `BGFX_STATE_PT_LINES` topology flag together — an inconsistent gate (e.g. drawing
lines with the *original* triangle index buffer, or the wireframe index buffer with a triangle
topology flag) would corrupt the geometry; both are gated by the same `useWireframe` boolean.
Independently confirmed the reference triangle's centre-pixel occupancy claim: vertices
`(0,0.8)`,`(1,-0.8)`,`(-1,-0.8)` form a triangle whose interior does contain NDC `(0,0)` (barycentric
check: the centroid is at `(0,-0.267)`, and `(0,0)` lies strictly between the apex and the base — well
inside), while none of the 3 edges pass through the exact centre pixel at 64×64 resolution, consistent
with Check B's BACKGROUND expectation.

### Logic
The 3-check sequence (Solid → WireFrame → Solid) specifically targets a "gets stuck" failure mode that a
2-check (Solid-then-WireFrame-only) test could not — this audit confirms `wireframe_` is a plain
assignment (`wireframe_ = (fillMode == 1);`), not an OR-latch or sticky flag, so reverting is structurally
guaranteed to work if the state machine is even minimally correct, but the explicit 3rd check is still
good defensive test design.

### C++ correctness
`ExpandWireframeIndices` bails out early and returns `false` (falling back to solid rendering rather
than corrupting the draw) if the transient index buffer pool is exhausted
(`bgfx::getAvailTransientIndexBuffer(...) < edgeCount*2`) — a real GPU resource-pressure guard, not just
a happy-path implementation. For a single triangle (`edgeCount=3`) this is never remotely close to
exhaustion in this test, but the guard is correct in principle.

### Testing
The custom `VertexDeclaration`/`GpuVPC` (stride-16, `Vector3` position + `Color`) path exercises a
different vertex-buffer construction route (`VertexBuffer(dev, decl, 3, BufferUsage::None)` +
`SetDataRaw`) than the `VertexPositionColor`-array `DrawUserPrimitives` path used by the sibling
cullmode/depthbias tests in this shard — incidentally broadening this shard's coverage of vertex-buffer
construction styles, not just the `FillMode` property itself.

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM defects found.

## Cross-File Observations

- The file's header notes "no EasyGL/shared source exists for this property... confirmed via search" —
  this audit did not find an EasyGL wireframe pixel test either, consistent with that claim; the
  Bgfx/Vulkan implementations of `FillMode::WireFrame` are structurally different from each other
  (index-list re-expansion vs. a native `VkPolygonMode`), so this is a case where the Bgfx-specific
  `ExpandWireframeIndices` approach genuinely needs its own dedicated test rather than reusing another
  backend's.
- `ExpandWireframeIndices` is shared across every 3D draw path in the file (`DrawColoredPrimitives`,
  `DrawIndexedColoredPrimitives`, and the `Ex` overloads at lines 2394, 2901, 3414) — this test only
  exercises the non-indexed `DrawColoredPrimitives` path (via `SetVertexBuffer`+`DrawPrimitives`, not
  `DrawUserPrimitives`); the indexed and `Ex`-overload wireframe paths are not directly exercised by this
  specific file, though they share the same `ExpandWireframeIndices` implementation.

## Missing or Weak Tests

- No coverage of `FillMode::WireFrame` combined with an indexed draw (`DrawIndexedPrimitives`) in this
  shard, which would exercise `ExpandWireframeIndices`'s `ib != nullptr` branch (reading real index data
  via `readSrc`, including its 16-bit vs. 32-bit index width handling) rather than the `nullptr`
  (sequential-vertex) branch this file exercises.

## Positive Findings

- Correct, minimal emulation of a missing native GPU feature (bgfx has no polygon-fill-mode toggle) via
  index-buffer re-expansion, with a real resource-exhaustion guard rather than an unchecked allocation.
- The 3-check "does it come back" design is good defensive practice beyond just "does it work once".

## Final Assessment

A correctly-designed, correctly-implemented wireframe test. The one real gap (indexed-draw wireframe
coverage) is a coverage gap in this shard, not a defect in this file.
