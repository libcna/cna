# Audit: examples/easygl_draw_user_primitives_custom_test.cpp

## Metadata

- Source file: `examples/easygl_draw_user_primitives_custom_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration/pixel-readback test
- File type: C++ executable test (`Game` subclass, no gtest)
- Lines: 180
- XNA/FNA relevance: exercises `GraphicsDevice::DrawUserPrimitives(PrimitiveType, const void*, int, int, const
  VertexDeclaration&)` — the explicit-vertex-declaration generic overload, matching FNA's
  `DrawUserPrimitives<T>(primitiveType, T[], vertexOffset, primitiveCount, VertexDeclaration)` where `T` doesn't
  implement `IVertexType`
- FNA reference: `GraphicsDevice.cs` line 1530 (`DrawUserPrimitives<T>(..., VertexDeclaration vertexDeclaration)`)
- Production code under test: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` lines 972-994,
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (`EasyGLVertexBufferBackend::ApplyLayout`, lines
  2195-2353; `SetVertexDeclaration`, line 2190)

## Purpose

Task 256 pixel-readback test: defines a custom, non-`IVertexType` 16-byte packed vertex struct (`MyVertex`: 3
floats + 4 `uint8_t`), builds a matching `VertexDeclaration` via `makeVD()` (`Position` at offset 0,
`Color` at offset 12), and draws a full-NDC red quad through the explicit-declaration
`DrawUserPrimitives(..., VertexDeclaration&)` overload. Two sub-tests: `testBasic` (vertexOffset=0) and
`testVertexOffset` (vertexOffset=1, dummy green vertex at slot 0).

## Executive Verdict

**Needs attention — the test passes for the wrong reason.** Tracing the exact call path this test exercises
(`GraphicsDevice::DrawUserPrimitives(..., VertexDeclaration&)` → `EasyGLVertexBufferBackend::ApplyLayout`) found a
confirmed, concrete gap: `GraphicsDevice.cpp`'s explicit-declaration overload never calls
`IVertexBufferBackend::SetVertexDeclaration()` on the transient buffer it creates, so the EasyGL backend never
actually receives this test's `VertexElement` list — it falls back to a hardcoded stride-keyed guess table that
happens, by the test's own admission in its source comment, to be byte-identical to what `MyVertex` uses. See F1.

## Checklist Results

### API / XNA / FNA parity
`DrawUserPrimitives(PrimitiveType, const void*, int, int, const VertexDeclaration&)` (test lines 114, 144) matches
`GraphicsDevice.hpp` lines 415-417 and FNA's `DrawUserPrimitives<T>(..., VertexDeclaration)` generic overload in
role: "use this when the vertex type does not implement `IVertexType`." The Doxygen comment at
`GraphicsDevice.hpp:401-407` states "The raw bytes are uploaded to a transient vertex buffer using the stride from
`vertexDeclaration`" — literally true (see F1) but silent on the fact that only the *stride number*, not the
element layout itself, survives past `GraphicsDevice.cpp`.

### Behavioral correctness
Traced `GraphicsDevice::DrawUserPrimitives(PrimitiveType, const void*, int vertexOffset, int primitiveCount, const
VertexDeclaration&)` (`GraphicsDevice.cpp:972-994`): computes `stride =
vertexDeclaration.getVertexStrideProperty()` (16, confirmed via `VertexDeclaration`'s
initializer-list constructor auto-computing `maxEnd` across elements — `VertexDeclaration.cpp:33-41`), advances
`src` by `vertexOffset*stride` bytes, calls `backend_->CreateVertexBuffer(n)` (generic factory, **no**
declaration parameter) then `vb->SetData(src, n, stride)` — **never** `vb->SetVertexDeclaration(...)`. Confirmed
by reading `EasyGLVertexBufferBackend::ApplyLayout` (`EasyGLGraphicsBackend.cpp:2195-2227`): it *would* honor a
generic, arbitrary `VertexElement` list via `declarationElements_` — but only if `SetVertexDeclaration()` had
been called first to populate that member; since it never is for this specific call path,
`declarationElements_` stays empty and `ApplyLayout` falls through to the hardcoded `switch(stride)` table
(lines 2229-2351), whose `case 16:` branch hardcodes "position float3 @ offset 0, ubyte4 color @ offset 12" —
which is exactly `MyVertex`'s actual layout, so this specific test renders correctly by coincidence, not because
the declaration was honored.
Grepped the whole `Microsoft::Xna::Framework::Graphics` tree for other callers of `SetVertexDeclaration`: only
`VertexBuffer.cpp:388` calls it (for a persistent, explicitly-declared `VertexBuffer`, a different code path from
the one this test exercises).

### Logic
`makeVD()` (lines 83-89) constructs `VertexElement(0, Vector3, Position, 0)` and `VertexElement(12, Color, Color,
0)` — correct per-field description of `MyVertex`'s actual memory layout, and `VertexDeclaration`'s own stride
auto-computation correctly yields 16. The declaration is *correct*; it's simply discarded before reaching the
backend (see F1), which the test's assertions cannot detect.

### Memory/resource lifetime
Same function-local `BasicEffect fx` pattern as the other multi-sub-test files in this shard (constructed inside
`testBasic`/`testVertexOffset`, destroyed on return) — not a live issue for the same reason already documented in
`easygl_draw_user_indexed_primitives_32_test.cpp.audit.md`'s F1 (no dereference occurs in the dangling window).

### C++ correctness
`static_assert(sizeof(MyVertex) == 16, ...)` (line 41) is a good defensive check tying the struct's actual size to
the test's assumption — correctly present.

### Robustness
Same `RasterizerState::CullNone` workaround (lines 113, 143), same "Task 896" justification — re-verified the
winding computation independently for this file's own quad corners (`(-1,1),(-1,-1),(1,-1)` for the first
triangle) and confirms the same CCW-under-XNA-default-cull conclusion as the sibling files; the override is
necessary and correctly justified here too.

### Testing
This is the crux issue: the test's own header comment (lines 2-6) frames its purpose as validating that "a custom
packed vertex struct... supplies its layout via VertexDeclaration" — i.e. proving the *generic*
declaration-driven path works for a genuinely custom (non-`IVertexType`) layout. Because `MyVertex` is
deliberately built to alias a recognized magic stride (the test's own inline comment at line 35 says "same layout
as VertexPositionColor GpuVertex"), the test cannot and does not distinguish "the declaration was honored" from
"the declaration was silently ignored and a hardcoded fallback happened to match." See F1.

## Detailed Findings

### F1 — `DrawUserPrimitives(..., VertexDeclaration&)` never propagates the declaration to the backend; this test cannot detect it because its vertex layout aliases a hardcoded fallback

- Severity: HIGH
- Confidence: HIGH (traced the full call chain with no assumption: `GraphicsDevice.cpp:972-994` →
  `IGraphicsBackend::CreateVertexBuffer` (no declaration param, `IGraphicsBackend.hpp:744`) →
  `EasyGLVertexBufferBackend::SetData` (no declaration involved, `EasyGLGraphicsBackend.cpp:2410-2424`) →
  `ApplyLayout` (`2195-2227`) uses `declarationElements_`, populated only by `SetVertexDeclaration`
  (`2190`), which is called from exactly one place in the whole `Microsoft::Xna::Framework::Graphics` tree —
  `VertexBuffer.cpp:388` — a different, persistent-`VertexBuffer` code path, not this one)
- Category: correctness / architecture (production defect, exposed here as a test-coverage gap)
- Location/symbol: `GraphicsDevice::DrawUserPrimitives(PrimitiveType, const void*, int, int, const
  VertexDeclaration&)` (`GraphicsDevice.cpp:972-994`); this test's `testBasic`/`testVertexOffset`
  (lines 92-148)
- Evidence: see Behavioral correctness above. Concrete failing scenario: if `MyVertex`'s fields were reordered to
  `{r,g,b,a, x,y,z}` (color first) while `makeVD()` were correctly updated to `VertexElement(0, Color, Color, 0)`
  + `VertexElement(4, Vector3, Position, 0)` (still stride 16, still a *correct* declaration of the new layout),
  this exact test would silently render **wrong colors** (or nothing recognizable), because
  `EasyGLVertexBufferBackend::ApplyLayout`'s `case 16:` branch unconditionally binds position at byte offset 0
  and color at byte offset 12 regardless of what the declaration actually says — the declaration is never
  consulted. The test as currently written cannot exercise this because `MyVertex` was deliberately shaped to
  match that hardcoded assumption (confirmed by the test's own comment admitting this).
- Why it matters: the entire purpose of the `VertexDeclaration`-taking overload — per its own Doxygen comment
  ("Use this when the vertex type does not implement `IVertexType` and the layout must be supplied explicitly")
  and per the EasyGL backend's own `ApplyLayout` comment ("Task 1080: generic layout binding driven by the
  caller's own `VertexDeclaration`... covers genuinely custom layouts... that don't match any of the built-in
  strides") — is to support arbitrary, non-standard vertex layouts. For any real custom layout that either (a)
  doesn't total to one of the seven recognized magic strides (16/20/24/32/48/52/56/68), or (b) matches a
  recognized stride but with a different field order/format than that stride's hardcoded assumption, this XNA API
  entry point silently produces wrong rendering with no exception, no log-level error visible to a caller, and no
  test in this repository currently catches it.
- FNA/XNA comparison: FNA's equivalent overload genuinely supports arbitrary declarations (it pins the raw bytes
  and hands the actual `VertexDeclaration` to `FNA3D_ApplyVertexBufferBinding`/vertex-attribute setup — there is
  no stride-guessing in FNA at all). This is a real compatibility gap for any user code that relies on this
  overload for a layout other than the seven hardcoded shapes.
- Related files: `GraphicsDevice.cpp` (the fix belongs there — call `vb->SetVertexDeclaration(vertexDeclaration.GetVertexElements())`
  before `SetData`, mirroring `VertexBuffer.cpp:388`), `IGraphicsBackend.hpp` (interface already supports this,
  no interface change needed), and the equivalent `DrawUserIndexedPrimitives(..., VertexDeclaration&)` overloads
  (`GraphicsDevice.cpp:1261-1310`), which very likely have the identical gap (not verified line-by-line here since
  out of this file's direct scope, but worth checking in the `xna-graphics` shard's `GraphicsDevice.cpp` audit).
- Suggested action (not implemented by this audit — audit-only task): wire `SetVertexDeclaration` through in
  `GraphicsDevice.cpp`'s `DrawUserPrimitives`/`DrawUserIndexedPrimitives` explicit-declaration overloads; add a
  regression test with a genuinely non-magic-stride or reordered-field custom layout to close the coverage gap
  this audit found.

## Cross-File Observations

- This is the one file in this shard's 8-file batch where opening the production source materially changed the
  audit's read of the test's value — every other file in this batch was confirmed to genuinely validate its
  claimed behavior; this one does not, despite looking structurally identical to its siblings.
- `EasyGLGraphicsBackend.cpp`'s own comments (Task 1080, lines 2203-2209) show the backend author was fully aware
  of and had built the generic path specifically for this scenario — the gap is entirely on the `GraphicsDevice`
  caller side failing to invoke it, not a missing backend capability.

## Missing or Weak Tests

- No test anywhere in this shard exercises `DrawUserPrimitives(..., VertexDeclaration&)` with a vertex layout that
  is genuinely novel (not aliasing strides 16/20/24/32/48/52/56/68, or reordering fields within one of those
  strides) — this is the exact coverage gap that let F1 go undetected.
- Same gap likely applies to `DrawUserIndexedPrimitives(..., VertexDeclaration&)` (`GraphicsDevice.cpp:1261-1310`),
  untested by any file in this batch.

## Positive Findings

- `MyVertex`'s `static_assert(sizeof(MyVertex) == 16, ...)` and `makeVD()`'s element offsets are themselves
  correctly written — the bug is not in this test's own code, it's in the production call path it invokes.
- Both sub-tests correctly and independently re-verify the `RasterizerState::CullNone` requirement for this file's
  specific quad geometry rather than assuming it from a sibling file.

## Final Assessment

A structurally well-written test that, on close inspection against the actual `GraphicsDevice.cpp` and
`EasyGLGraphicsBackend.cpp` implementations, does not validate what its own name and header comment claim: it
cannot distinguish "the explicit `VertexDeclaration` was honored" from "the declaration was silently discarded
and a hardcoded stride-16 fallback happened to match this specific struct's layout." A concrete, traced defect
(F1) in `GraphicsDevice.cpp`'s explicit-declaration overloads sits directly underneath this test, undetected by
it precisely because of how the test's own vertex struct was shaped.
