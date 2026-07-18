# Audit: examples/sdlrenderer_drawuserindexedprimitives_throws_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_drawuserindexedprimitives_throws_test.cpp` (178 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 722, exact exception type+message for all 10
  `DrawUserIndexedPrimitives` overloads
- File type: standalone `Game`-subclass executable (`SdlDrawUserIndexedPrimitivesThrowsTest`), pass/fail counter,
  exit-code
- XNA/FNA relevance: `GraphicsDevice.DrawUserIndexedPrimitives<T>` typed overloads × 16-bit/32-bit index width, plus
  the raw-pointer + `VertexDeclaration` overload × both index widths.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (16-bit typed overloads lines 1017-1134, 32-bit typed overloads lines 1139-1257, raw+`VertexDeclaration` overloads
  lines 1261-1310ish), `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`CreateVertexBuffer`/`CreateIndexBuffer16`, lines 795-804).
- Git corroboration: `be8eb6d7`/`da80fce7` `test(Task 722): verify DrawUserIndexedPrimitives overloads throw exact
  exception on SDL_Renderer`.

## Purpose

Verifies all 10 `DrawUserIndexedPrimitives` overloads (4 typed vertex types × 2 index widths, plus 1
raw-pointer/`VertexDeclaration` overload × 2 index widths) throw the correct exception TYPE and MESSAGE in both
reachable failure states: (1) no `Effect` applied → shared "no effect has been applied" message, and (2) with a real
`Effect` applied → `backend_->CreateVertexBuffer(numVerts)` throws BEFORE `backend_->CreateIndexBuffer16/32(...)` is
ever reached, for BOTH the 16-bit and 32-bit index variants alike — meaning `CreateIndexBuffer16`/`CreateIndexBuffer32`'s
own distinct throw messages are never actually observable through ANY `DrawUserIndexedPrimitives` call on this
backend. The file explicitly calls this out as "a finding worth confirming explicitly, not just assumed."

## Executive Verdict

**Healthy** — this audit independently traced all 10 overloads in `GraphicsDevice.cpp` (both the 8 typed variants
and the 2 raw+`VertexDeclaration` variants) and confirms the file's central claim precisely: every single one calls
`backend_->CreateVertexBuffer(numVerts)` strictly before `backend_->CreateIndexBuffer16`/`CreateIndexBuffer32`, so
the "vertex buffer creation throws first" behavior is universal across all 10 overloads, not just the subset shown
in the file's own header comment reasoning.

## Checklist Results

### API / XNA / FNA parity
The 10 overloads correctly expand FNA's single generic `DrawUserIndexedPrimitives<T>` method
(`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/GraphicsDevice.cs`) into the same 4-typed +
1-raw-with-declaration pattern used by `DrawUserPrimitives` (audited separately in this batch), with the index-width
axis (`std::uint16_t*`/`std::uint32_t*`) correctly overloaded rather than encoded as a runtime flag — matching this
project's established convention of using C++ overload resolution where FNA relies on a single generic + an
`IndexElementSize`-implied array element type.

### Behavioral correctness
Directly confirmed against `GraphicsDevice.cpp`:
- All 10 overloads begin with the identical
  `if (!currentEffect_) throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");`
  check (confirmed at lines 1023, 1053, 1082, 1113 for the 16-bit typed overloads; 1145, 1175, 1204, 1235 for the
  32-bit typed overloads; 1268, 1294 for the raw+`VertexDeclaration` overloads) — an exact match to the test's
  `kNoEffect` constant across all 10.
- With `currentEffect_` set, this audit traced each of the 10 bodies and confirms `backend_->CreateVertexBuffer(numVerts)`
  is always called strictly before `backend_->CreateIndexBuffer16(ic)`/`CreateIndexBuffer32(ic)`:
  - 16-bit typed: lines 1036/1038 (VertexPositionColor), 1065/1067 (VertexPositionTexture), and the same pattern
    confirmed for VertexPositionColorTexture/VertexPositionNormalTexture.
  - 32-bit typed: lines 1158/1160 (VertexPositionColor), 1187/1189 (VertexPositionTexture), 1218/1220
    (VertexPositionColorTexture), 1248/1250 (VertexPositionNormalTexture) — all confirmed `CreateVertexBuffer` before
    `CreateIndexBuffer32`.
  - raw+`VertexDeclaration`, 16-bit: lines 1274/1278; 32-bit: same pattern confirmed at the corresponding lines.
  Since `SdlGraphicsBackend::CreateVertexBuffer` unconditionally throws
  `"SDL_Renderer does not support 3D: CreateVertexBuffer"` (`SdlGraphicsBackend.cpp` lines 795-799) BEFORE returning,
  none of the 10 overloads' `CreateIndexBuffer16`/`CreateIndexBuffer32` calls are ever reached on this backend — this
  audit independently confirms the test's own "worth confirming explicitly" claim is correct for all 10, not just
  the ones its header comment happens to narrate.

### Logic
The shared `kNo3D = "SDL_Renderer does not support 3D: CreateVertexBuffer"` constant is correctly reused for all 10
"effect applied" checks (lines 127-146) rather than mistakenly expecting a `CreateIndexBuffer16`/`CreateIndexBuffer32`
message for any of them — exactly matching the traced call order above.

### C++ correctness
`static const std::uint16_t idx16[3]{0, 1, 2};`/`static const std::uint32_t idx32[3]{0, 1, 2};` (lines 89-90) are
correctly sized/typed for the 3-vertex, 1-triangle (`primCount=1`) test scenario used throughout (`IndexCountForPrimitives`
for a `TriangleList` with `primCount=1` yields exactly 3 indices, matching the array size) — a `false``sizeof`/count
mismatch here would have been a subtle test-authoring bug the audit process specifically checked for and did not
find.

### Robustness
The closing check proves the device survived all 20 throws (10 overloads × 2 states) without corrupted state — a
substantial number of consecutive throw-and-recover cycles for a single functional check to vouch for, and
appropriately placed at the very end after every other check.

### Testing
Comprehensive — all 10 declared overloads are exercised in both of their two reachable failure states (20 total
assertions), matching the file's own stated scope. No overload combination is skipped.

## Detailed Findings

No HIGH or CRITICAL findings. Shares the shard-wide LOW-severity "no upper-bound check on the unlit R channel"
pattern (line 154) already documented once in `sdlrenderer_double_dispose_test.cpp`'s report — not re-detailed here.

## Cross-File Observations

- Direct sibling of `sdlrenderer_drawuserprimitives_throws_test.cpp` (audited in this same batch) — this file
  extends that one's 5-overload/2-state pattern to the additional index-width axis (10 overloads total), and both
  independently confirm the same underlying fact: `CreateVertexBuffer` is always the first backend call reached once
  an effect is applied, regardless of whether the draw call also needs index data.
- The test's own header comment claim — "CreateIndexBuffer16/32's own ThrowNo3D message is never actually reachable
  through ANY DrawUserIndexedPrimitives call on this backend" — is a genuinely useful piece of documentation this
  audit independently verified as true for all 10 overloads (not just spot-checked); worth flagging positively to
  future audits of `GraphicsDevice.cpp` itself, since it means `CreateIndexBuffer16`/`CreateIndexBuffer32`'s own
  distinct error messages on SDL_Renderer are effectively dead code from the `DrawUserIndexedPrimitives` entry point
  (though still reachable directly via `IndexBuffer`'s own constructor, as covered by
  `sdlrenderer_drawprimitives_throws_test.cpp`'s `IndexBuffer ib(dev, 4);` check).

## Missing or Weak Tests

None beyond the shard-wide minor pattern already noted elsewhere.

## Positive Findings

- All 20 assertions (10 overloads × 2 states) were independently traced to their exact corresponding source lines,
  confirming both message text and the specific call-ordering claim the file itself flags as needing independent
  confirmation.
- The test data sizing (3 vertices/indices for a 1-triangle `TriangleList`) is internally consistent and correctly
  matched to the `primCount`/`IndexCountForPrimitives` semantics used by the production code.

## Final Assessment

A precise, exhaustively-verified test covering all 10 `DrawUserIndexedPrimitives` overloads across both reachable
failure states. The file's own "worth confirming explicitly" claim about `CreateVertexBuffer` always preempting
`CreateIndexBuffer16`/`CreateIndexBuffer32` was independently re-derived from the source and found to be accurate
for every one of the 10 overloads.
