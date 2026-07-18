# Audit: examples/easygl_draw_user_indexed_primitives_32_test.cpp

## Metadata

- Source file: `examples/easygl_draw_user_indexed_primitives_32_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration/pixel-readback test
- File type: C++ executable test (`Game` subclass, no gtest)
- Lines: 159
- XNA/FNA relevance: exercises `GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType, const
  VertexPositionColor*, int, int, const std::uint32_t*, int, int)` — a real XNA 4.0 32-bit-index generic overload
- FNA reference: `GraphicsDevice.cs` `DrawUserIndexedPrimitives<T>(..., int[] indexData, ...)` (32-bit overload,
  around line 1451 in the FNA source, sibling to the 16-bit overloads read at lines 1315-1450)
- Production code under test: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` lines 1139-1167 (typed
  32-bit `DrawUserIndexedPrimitives(VertexPositionColor*, uint32_t*)` overload) and
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (`DrawIndexedPrimitivesEx`, lines 4564-4624)

## Purpose

Task 258 pixel-readback test: draws a full-NDC red quad via the 32-bit-index `DrawUserIndexedPrimitives` overload
and reads back the exact centre backbuffer pixel to confirm it rendered red against a green-cleared background.
Two sub-tests: `testBasic` (offset 0 for both arrays) and `testOffsets` (vertex and index offset both 1, with a
dummy green vertex/dummy index preceding the real geometry in each array) — the second sub-test specifically
exercises the offset-arithmetic path for both the vertex-offset and index-offset parameters independently.

## Executive Verdict

**Healthy — genuinely validates real rendering behavior, not just "doesn't crash."** Both sub-tests were traced
end-to-end against the actual `GraphicsDevice.cpp` overload implementation and the `EasyGLGraphicsBackend`'s
`DrawIndexedPrimitivesEx`, confirming the offset semantics the test's own comments describe are exactly what the
production code does (see Behavioral correctness below) — this is not a boilerplate "draws something, hopes for
the best" test.

## Checklist Results

### API / XNA / FNA parity
`DrawUserIndexedPrimitives(PrimitiveType, const VertexPositionColor*, int vertexOffset, int numVertices, const
std::uint32_t*, int indexOffset, int primitiveCount)` (test line 96, 123) resolves to
`GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType, const VertexPositionColor*, int, int, const
std::uint32_t*, int, int)` (`GraphicsDevice.cpp:1139-1167`) by exact-type overload resolution — no ambiguity with
the sibling 16-bit (`std::uint16_t*`) overload since the test explicitly declares `const std::uint32_t
indices[...]`. This matches FNA's generic `DrawUserIndexedPrimitives<T>(..., int[] indexData, ...)` 32-bit overload
(`GraphicsDevice.cs`) in parameter role and order.

### Behavioral correctness
Traced the exact data flow for both sub-tests against `GraphicsDevice.cpp:1139-1167`:
- `testBasic`: `vOffset=0, numVerts=4, iOffset=0, primCount=2`. Production code packs `vertices[0..4)` into a
  scratch `GpuVPC` buffer (loop `for i in [0,numVerts)`, reading `vertices[vOffset+i]`), computes
  `ic=IndexCountForPrimitives(TriangleList,2)=6`, copies `indices[iOffset..iOffset+6) = indices[0..6)` verbatim
  into a new 32-bit index buffer, and draws via `DrawIndexedPrimitivesEx` with `startIndex=0, baseVertex=0`
  (default `GpuDrawParams`). Since the freshly-created vertex/index buffers start at position 0 regardless of the
  *caller's* offsets, an index value of `2` in the source array correctly refers to `vertices[vOffset+2]` in the
  packed buffer — exactly matching the 4 vertices `{TL,BL,BR,TR}` and indices `{0,1,2,0,2,3}` (two CCW-wound
  triangles covering the full quad).
- `testOffsets`: `vOffset=1, numVerts=4, iOffset=1, primCount=2`. Vertex packing loop reads `vertices[1+i]` for
  `i∈[0,4)` — i.e. `verts[1..5)`, correctly skipping the dummy green vertex at `verts[0]`. Index copy takes
  `indices[iOffset..iOffset+6) = indices[1..7)` from the 7-element array `{0,0,1,2,0,2,3}` → copies `{0,1,2,0,2,3}`
  — correctly skipping the dummy leading index at `indices[0]`. The copied index values (`0,1,2,0,2,3`) are used
  as-is (no `-vOffset` adjustment) against the *packed* vertex buffer that already starts at `vertices[vOffset]` —
  exactly matching the test's own comment ("Index values are relative to the vertexOffset-shifted window (0 ==
  verts[vertexOffset])"). This is a correct, verified understanding of the actual offset semantics, not a guess.

### Logic
`isRed()` (line 57-60) uses a tolerant threshold (`R>=200, G<=50, B<=50`) appropriate for an 8-bit-quantized
render target with potential blend/gamma rounding — reasonable, not so loose it would pass a wrong-but-similar
color.
`readCenter()` (lines 48-55) reads a single `1×1` region at `(W/2, H/2)` — correct for a full-viewport quad
regardless of viewport parity (worst case the sample point is one pixel off-center, still well inside the quad).

### Memory/resource lifetime
`BasicEffect fx(dev)` is constructed as a **function-local** object inside both `testBasic` and `testOffsets`
(lines 90, 117) and calls `fx.Apply()`, which (per `GraphicsDevice::SetCurrentEffect`) makes `GraphicsDevice`
store a raw, non-owning pointer to it. `fx` is destroyed when each function returns, leaving `currentEffect_`
briefly dangling between the end of `testBasic` and the start of `testOffsets` — traced the call site (`Draw()`,
lines 132-143) and confirmed nothing runs in that window that would dereference `currentEffect_` (the next action
is constructing a *new* `BasicEffect` in `testOffsets` and calling `Apply()` again, which overwrites the pointer
before any draw happens). Not a live bug today, but a fragile pattern shared across every multi-sub-test file in
this shard (see Cross-File Observations) — holding a dangling pointer in a member is not itself UB in C++, only
dereferencing it would be, and no dereference occurs in the dangling window.

### C++ correctness
`VertexPositionColor verts[4]`/`verts[5]` and `std::uint32_t indices[6]`/`indices[7]` are plain stack arrays with
correct, matching element counts for what's read (no over-read: `numVerts=4` against a 4- or 5-element array read
from `vOffset`, `ic=6` against a 6- or 7-element array read from `iOffset` — verified both stay in-bounds for both
sub-tests).

### Robustness
Explicitly sets `RasterizerState::CullNone` (lines 95, 122) with a comment attributing this to "Task 896" — cross-
checked the winding: the quad's first triangle order (`TL→BL→BR`, i.e. `(-1,1)→(-1,-1)→(1,-1)`) has a positive
2D shoelace sum, i.e. counter-clockwise in the math (y-up) convention CNA's clip space uses. `RasterizerState`'s
documented default is `CullCounterClockwiseFace` (confirmed in
`include/Microsoft/Xna/Framework/Graphics/RasterizerState.hpp` line 17, "cull counter-clockwise-wound faces (XNA
default)"), and `EasyGLGraphicsBackend::ApplyRasterizerState` (lines 1976-1991) implements this by enabling
GL face culling for exactly this case — so without the explicit `CullNone` override, this exact quad would indeed
be culled and render nothing (background green, test would legitimately fail). The comment is accurate, not
cargo-culted, and matches the same documented fix pattern already applied to the Bgfx backend's equivalent test
per the comment's own cross-reference.

### Testing
This file's two sub-tests give real, distinct coverage: `testBasic` validates the base draw path, `testOffsets`
validates that *both* `vertexOffset` and `indexOffset` are threaded correctly and independently through the typed
32-bit overload — a meaningfully different code path (different loop start indices, different copy source ranges)
from `testBasic`, not a trivial repeat.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings — the test's assertions were verified to correspond to real, correctly-implemented
production behavior in both the `GraphicsDevice` layer and the `EasyGL` backend.

### F1 — Effect-pointer dangling window between sub-tests (see Memory/resource lifetime)

- Severity: LOW
- Confidence: MEDIUM (pattern confirmed safe today; depends on no future code inserting a draw/effect-touching
  call between `testBasic()` and `testOffsets()`)
- Category: maintainability / lifetime-fragility
- Location/symbol: `testBasic`/`testOffsets`, local `BasicEffect fx` (lines 90, 117); `GraphicsDevice::currentEffect_`
- Evidence: see Memory/resource lifetime above.
- Why it matters: purely a latent-fragility note, not a live defect — flagged because it recurs identically across
  four files in this shard (this one, the VPC 16-bit sibling, the custom-vertex-declaration test, and the plain
  VPC test), so it's a shard-wide pattern worth a single cross-cutting note rather than four separate deep dives.
- Suggested action (not implemented by this audit): none required; noting for awareness only.

## Cross-File Observations

- Identical structure, sub-test split, and offset-testing strategy as `easygl_draw_user_indexed_primitives_vpc_test.cpp`
  (16-bit sibling) — the two files differ only in index width (`uint32_t` vs `uint16_t`), confirming both the
  16-bit and 32-bit typed overloads get equivalent, independently-verified coverage.
- Shares the local-effect-lifetime pattern (F1) with `easygl_draw_user_indexed_primitives_vpc_test.cpp`,
  `easygl_draw_user_primitives_custom_test.cpp`, and `easygl_draw_user_primitives_vpc_test.cpp`.

## Missing or Weak Tests

- Neither sub-test exercises a primitive count other than 2 (i.e. no test with `primitiveCount=1` for a single
  triangle, or a `TriangleStrip`/`LineList` topology) — the offset arithmetic is only proven correct for
  `TriangleList`. Not a defect, just a narrower-than-ideal topology/size coverage for this specific overload.

## Positive Findings

- The offset semantics claimed by the test's comments were independently re-derived from the production code and
  confirmed correct, not just trusted — a genuinely evidence-based pixel-level regression test.
- Correctly and verifiably justifies its `CullNone` workaround against the real default `RasterizerState` and the
  real EasyGL culling implementation, rather than blindly copying a pattern from a sibling test.

## Final Assessment

A well-constructed, evidence-verified pixel-readback test that actually proves the 32-bit
`DrawUserIndexedPrimitives` overload's vertex- and index-offset arithmetic work independently and correctly in
both `GraphicsDevice` and the EasyGL backend — exactly the kind of test that validates real behavior rather than
just execution-without-crash.
