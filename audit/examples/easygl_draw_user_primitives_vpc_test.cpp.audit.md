# Audit: examples/easygl_draw_user_primitives_vpc_test.cpp

## Metadata

- Source file: `examples/easygl_draw_user_primitives_vpc_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration/pixel-readback test
- File type: C++ executable test (`Game` subclass, no gtest)
- Lines: 155
- XNA/FNA relevance: exercises `GraphicsDevice::DrawUserPrimitives(PrimitiveType, const VertexPositionColor*, int,
  int)` — a real XNA 4.0 typed generic overload
- FNA reference: `GraphicsDevice.cs` `DrawUserPrimitives<T>(primitiveType, T[], vertexOffset, primitiveCount)`
  (line 1501) instantiated with `T = VertexPositionColor`
- Production code under test: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` lines 868-892 (typed VPC
  overload) and `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EasyGLVertexBufferBackend::ApplyLayout` `case 16:`, lines 2231-2237)

## Purpose

Task 255 pixel-readback test: draws a full-NDC red quad via the typed `VertexPositionColor` overload of
`DrawUserPrimitives` and reads back the centre pixel. Two sub-tests: `testBasic` (6-vertex array, offset 0, 2
triangles) and `testVertexOffset` (7-vertex array with a dummy green vertex at index 0, offset 1).

## Executive Verdict

**Healthy.** Unlike its `VertexDeclaration`-based sibling (`easygl_draw_user_primitives_custom_test.cpp`), this
overload's code path was traced end-to-end and found to genuinely, non-coincidentally match the layout the EasyGL
backend hardcodes — there is no declaration-propagation gap here because this overload never claims to support an
arbitrary layout in the first place; it always packs into the fixed `GpuVPC` struct.

## Checklist Results

### API / XNA / FNA parity
`DrawUserPrimitives(PrimitiveType, const VertexPositionColor*, int, int)` (test lines 93, 119) matches
`GraphicsDevice.hpp:425-426` and FNA's `DrawUserPrimitives<T>(...)` instantiated for `VertexPositionColor`
(`GraphicsDevice.cs:1501`). Confirmed exact-type overload resolution: the array decays to `const
VertexPositionColor*`, an identity match against this typed overload, not the competing `const void*` 4-arg
overload (`GraphicsDevice.hpp:398-399`) or the `VertexDeclaration`-taking overload (different arity) — no
ambiguity.

### Behavioral correctness
Traced `GraphicsDevice::DrawUserPrimitives(PrimitiveType, const VertexPositionColor*, int offset, int count)`
(`GraphicsDevice.cpp:869-892`): computes `n = VertexCountForUserPrimitives(type, count)`, packs `data[offset+i]`
for `i∈[0,n)` into a `GpuVPC{x,y,z,r,g,b,a}` scratch buffer (fixed 16-byte layout, always position-then-color, no
declaration involved anywhere in this path), creates a vertex buffer via `backend_->CreateVertexBuffer(n)`, and
`vb->SetData(packed, n, sizeof(GpuVPC))` (stride 16). Since `EasyGLVertexBufferBackend::ApplyLayout`'s `case 16:`
branch (`EasyGLGraphicsBackend.cpp:2231-2237`) hardcodes exactly this same layout (position float3 @ 0, ubyte4
color @ 12) — and this overload's packing code is the *origin* of that convention (both sides of the
contract are maintained by the same author/module, not an independent generic mechanism aliasing by luck) — this
is a correct, intentional, tightly-coupled design, not a coincidence like the `VertexDeclaration` overload's case.
- `testBasic`: `offset=0, count=2` → packs `verts[0..6)` (2 triangles: `{TL,BL,BR}` + `{TL,BR,TR}`), both CCW in
  NDC (consistent with the sibling files' winding analysis).
- `testVertexOffset`: `offset=1, count=2` → packs `verts[1..7)` from the 7-element array, correctly skipping the
  dummy green vertex at index 0 — confirmed the loop `data[offset + i]` for `i∈[0,6)` yields exactly `verts[1..7)`.

### Logic
`isRed()`/`readCenter()` identical in structure/tolerance to the other pixel-readback files in this batch —
same reasoning applies.

### Memory/resource lifetime
Same function-local `BasicEffect fx` pattern (constructed inside `testBasic`/`testVertexOffset`, lines 87, 112) as
the other multi-sub-test files in this shard — not a live issue for the reasons already documented in
`easygl_draw_user_indexed_primitives_32_test.cpp.audit.md`'s F1.

### C++ correctness
`VertexPositionColor verts[6]`/`verts[7]` arrays are correctly sized for what's read (`n=6` against a 6- or
7-element array read from `offset`) — verified no out-of-bounds read in either sub-test.

### Robustness
Same `RasterizerState::CullNone` workaround (lines 92, 118), same "Task 896" attribution — independently
re-verified the winding for this file's own triangle order (`{kTL,kBL,kBR}` then `{kTL,kBR,kTR}`) and confirms the
same CCW-under-default-cull conclusion as every other file in this batch; correctly necessary here too.

### Testing
Both sub-tests exercise genuinely different code (different loop start offset, different source array), giving
real (not duplicate) coverage of the typed-VPC overload's offset arithmetic.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings — this overload's implementation was confirmed correct and internally consistent
(the packing code and the backend's fixed-layout assumption are two sides of the same intentional design, not an
accidental alignment).

### F1 — Effect-pointer dangling window between sub-tests

- Severity: LOW
- Confidence: MEDIUM
- Category: maintainability / lifetime-fragility
- Location/symbol: `testBasic`/`testVertexOffset`, local `BasicEffect fx` (lines 87, 112)
- Evidence/why it matters: identical to the finding already logged in
  `easygl_draw_user_indexed_primitives_32_test.cpp.audit.md` (F1) — shared shard-wide pattern, not re-derived here.

## Cross-File Observations

- Contrast with `easygl_draw_user_primitives_custom_test.cpp`: that file's overload *claims* to support arbitrary
  declarations but doesn't propagate them to the backend (a confirmed defect, F1 in that report); this file's
  overload makes no such claim — it always packs to a fixed `GpuVPC` layout by design, and the backend's
  stride-16 fallback exists specifically to serve this exact code path. No architectural mismatch here.
- Shares the local-effect-lifetime observation with every other multi-sub-test file in this batch.

## Missing or Weak Tests

- No sub-test exercises a primitive count other than 2, or a non-`TriangleList` topology, through this specific
  typed overload (same narrow-topology-coverage observation as the indexed-primitives siblings).

## Positive Findings

- This overload's fixed-layout design and the backend's hardcoded stride-16 case are correctly, non-coincidentally
  aligned — confirmed by tracing both sides rather than assuming consistency.
- Both sub-tests give genuinely distinct, verified coverage of the offset-arithmetic path.

## Final Assessment

A correct, evidence-verified test of the typed `VertexPositionColor` `DrawUserPrimitives` overload. Unlike its
`VertexDeclaration`-based sibling in this same batch, there is no hidden coupling gap here — the fixed-layout
design this overload relies on is intentional and consistently implemented on both the `GraphicsDevice` and
EasyGL-backend sides.
