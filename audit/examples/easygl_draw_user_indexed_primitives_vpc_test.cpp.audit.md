# Audit: examples/easygl_draw_user_indexed_primitives_vpc_test.cpp

## Metadata

- Source file: `examples/easygl_draw_user_indexed_primitives_vpc_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration/pixel-readback test
- File type: C++ executable test (`Game` subclass, no gtest)
- Lines: 159
- XNA/FNA relevance: exercises `GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType, const
  VertexPositionColor*, int, int, const std::uint16_t*, int, int)` — a real XNA 4.0 16-bit-index generic overload
- FNA reference: `GraphicsDevice.cs` `DrawUserIndexedPrimitives<T>(..., short[] indexData, ...)` (16-bit overload,
  line 1315)
- Production code under test: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` lines 1017-1045 (typed
  16-bit overload) and `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`DrawIndexedPrimitivesEx`, lines 4564-4624)

## Purpose

Task 257 pixel-readback test — the 16-bit-index sibling of `easygl_draw_user_indexed_primitives_32_test.cpp`.
Same structure: `testBasic` (offset 0) and `testOffsets` (vertex- and index-offset both 1, with a dummy
green vertex/dummy index preceding the real geometry), reading back the centre backbuffer pixel to confirm a red
quad rendered correctly.

## Executive Verdict

**Healthy — genuinely validates real rendering behavior.** Traced end-to-end against
`GraphicsDevice::DrawUserIndexedPrimitives`'s 16-bit typed overload and the EasyGL backend's
`DrawIndexedPrimitivesEx`; the offset semantics the test's comments claim match what the code actually does.

## Checklist Results

### API / XNA / FNA parity
`DrawUserIndexedPrimitives(PrimitiveType, const VertexPositionColor*, int, int, const std::uint16_t*, int, int)`
(test lines 96, 123) resolves by exact-type match to `GraphicsDevice.cpp:1017-1045`. Matches FNA's
`DrawUserIndexedPrimitives<T>(..., short[] indexData, ...)` generic overload (`GraphicsDevice.cs:1315`) in
parameter role and order (FNA's C# `short` maps to this project's `std::uint16_t` per the project's own
`IndexElementSize::SixteenBits` convention — verified consistent with the sibling
`easygl_draw_user_indexed_primitives_32_test.cpp`'s `uint32_t`/`IndexElementSize::ThirtyTwoBits` pairing).

### Behavioral correctness
Traced both sub-tests against `GraphicsDevice.cpp:1017-1045` (`DrawUserIndexedPrimitives(VertexPositionColor*,
uint16_t*)`):
- `testBasic` (`vOffset=0, numVerts=4, iOffset=0, primCount=2`): packs `vertices[0..4)` into `GpuVPC`, computes
  `ic=IndexCountForPrimitives(TriangleList,2)=6`, copies `indices[0..6)` into a fresh 16-bit index buffer
  (`CreateIndexBuffer16`/`SetData16`), draws via `DrawIndexedPrimitivesEx` with default `startIndex=0,
  baseVertex=0`. Correct: the quad's two triangles (`{0,1,2,0,2,3}`) index into the freshly-packed 4-vertex buffer
  exactly as intended.
- `testOffsets` (`vOffset=1, numVerts=4, iOffset=1, primCount=2`): vertex-pack loop reads `vertices[1+i]` for
  `i∈[0,4)`, correctly skipping the dummy green vertex at index 0; index copy takes `indices[1..7)` from the
  7-element `{0,0,1,2,0,2,3}` array, correctly skipping the leading dummy index and yielding `{0,1,2,0,2,3}` —
  used unmodified against the vOffset-shifted packed vertex buffer, matching the test's own comment about
  "relative to the vertexOffset-shifted window."

### Logic
`isRed()`/`readCenter()` are identical in structure and tolerance to the 32-bit sibling file — same reasoning
applies (tolerant-but-discriminating threshold, correctly-centred single-pixel readback).

### Memory/resource lifetime
Same function-local `BasicEffect fx` pattern as the 32-bit sibling (constructed inside `testBasic`/`testOffsets`,
destroyed on return, briefly leaving `GraphicsDevice::currentEffect_` dangling between sub-test calls with no
intervening dereference) — see F1 in the 32-bit sibling's report for the full analysis; identical, not a live bug,
not re-derived here.

### C++ correctness
`VertexPositionColor verts[4]`/`verts[5]` and `std::uint16_t indices[6]`/`indices[7]` stay in-bounds for both
read ranges in both sub-tests (same verification method as the 32-bit sibling, confirmed independently for the
16-bit array sizes here).

### Robustness
Same `RasterizerState::CullNone` workaround (lines 95, 122) with the identical "Task 896" justification —
independently re-verified the winding computation for this file's own quad-corner constants (`kTL, kBL, kBR, kTR`,
identical values to the 32-bit sibling) confirms the same CCW-under-XNA's-default-cull conclusion: without the
override this quad would be culled and the test would legitimately fail (green background, not red centre).

### Testing
Gives real, distinct-from-`testBasic` coverage in `testOffsets` for the 16-bit overload's vertex/index offset
arithmetic — same quality bar as the 32-bit sibling.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Effect-pointer dangling window between sub-tests

- Severity: LOW
- Confidence: MEDIUM
- Category: maintainability / lifetime-fragility
- Location/symbol: `testBasic`/`testOffsets`, local `BasicEffect fx` (lines 90, 117)
- Evidence/why it matters: identical to the finding already logged in
  `easygl_draw_user_indexed_primitives_32_test.cpp.audit.md` (F1) — not re-derived in full here to avoid
  duplication; the same shard-wide pattern applies unchanged to this file's two sub-test functions.

## Cross-File Observations

- Structurally identical (near line-for-line) to `easygl_draw_user_indexed_primitives_32_test.cpp` except for
  index width — both the 16-bit and 32-bit typed `DrawUserIndexedPrimitives` overloads now have equivalent,
  independently-traced, genuine pixel-level coverage rather than one file being a copy-paste with unverified
  claims.

## Missing or Weak Tests

- Same gap as the 32-bit sibling: only `TriangleList` with `primitiveCount=2` is exercised; no coverage of a
  single-triangle draw, a `TriangleStrip`, or a `LineList` through this specific overload.

## Positive Findings

- Independently re-verified (not just assumed identical to its sibling) that the 16-bit index copy range and the
  `CullNone` justification both hold for this file's own constants and code paths.

## Final Assessment

A correct, evidence-verified sibling to the 32-bit indexed-primitives test — both the 16-bit overload's offset
arithmetic and the culling workaround are genuinely exercised and confirmed against the real production code, not
assumed by analogy.
