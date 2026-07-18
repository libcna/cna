# Audit: examples/easygl_disposed_buffer_test.cpp

## Metadata

- Source file: `examples/easygl_disposed_buffer_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration test
- File type: C++ example/integration-test executable (`DisposedBufferTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::VertexBuffer` (`VertexBuffer.cpp`/`.hpp`),
  `IndexBuffer` (`IndexBuffer.cpp`/`.hpp`), `DynamicVertexBuffer`/`DynamicIndexBuffer` (header-only, delegate to the
  base classes), `GraphicsDevice::SetVertexBuffer`/`SetIndexBuffer` (`GraphicsDevice.cpp`)
- XNA/FNA relevance: exercises real XNA 4.0 `VertexBuffer.SetData`/`IndexBuffer.SetData`/`GraphicsDevice.Vertices`/
  `Indices` surface, plus the `IDisposable`/`ObjectDisposedException` contract every XNA `GraphicsResource` follows.
- FNA reference: FNA's own `GraphicsResource`-derived buffer types (`FNA/src/Graphics/VertexBuffers.cs`,
  `IndexBuffer.cs`) call `AssertNotDisposed()` at the top of every `SetData` overload, throwing
  `ObjectDisposedException` — the same contract this file is proving for CNA.
- Main related tests: this file (Task 240); the "positive" (non-disposed) `SetData` path is covered separately by
  `examples/easygl_vertexbuffer_setdata_test.cpp` (registered `cna_test_easygl_vertexbuffer_setdata`,
  `EasyGLTests.cmake:1032`) — an intentional split of concerns rather than a gap.
- Registered as `cna_test_easygl_disposed_buffer` / `EasyGL_DisposedBuffer` (`EasyGLTests.cmake:1047-1050`,
  TIMEOUT 60s).

## Purpose

Verifies that once a `VertexBuffer`/`DynamicVertexBuffer`/`IndexBuffer`/`DynamicIndexBuffer` has been `Dispose()`d,
every subsequent mutating call on it (`SetData` in each of its overloads, `SetDataRaw`) throws
`System::ObjectDisposedException` rather than silently writing into a freed/invalid GPU buffer, and that
`GraphicsDevice::SetVertexBuffer`/`SetIndexBuffer` reject a disposed buffer the same way while still tolerating
`nullptr` (the "unbind" case) without throwing. Twelve checks total, run once from `Initialize()` before `Exit()`.

## Executive Verdict

**Healthy** — every assertion in this file was independently verified against the real disposed-guard code in
`VertexBuffer.cpp`, `IndexBuffer.cpp`, and `GraphicsDevice.cpp`, and all twelve match production behavior exactly.
One minor, non-blocking cross-file naming inconsistency is worth tracking (F1).

## Checklist Results

### API / XNA / FNA parity
`VertexBuffer::SetData(T*, count)` / `SetData(T*, startIndex, count)`, `IndexBuffer::SetData(uint16_t*/uint32_t*,
count/startIndex,count)`, `DynamicVertexBuffer::SetData(T*, startIndex, count, SetDataOptions)`,
`DynamicIndexBuffer::SetData(...)`, and `GraphicsDevice::SetVertexBuffer`/`SetIndexBuffer` are all named and shaped
per XNA 4.0 (`Microsoft.Xna.Framework.Graphics`). `VertexBuffer::SetDataRaw` (used at line 78) is explicitly a
`NOXNA` CNA extension (verified: `VertexBuffer.hpp` documents it as such) used here purely as an additional
disposed-guard check, not as an XNA-parity claim — correctly not conflated with the XNA-named overloads in this
test's own check labels ("SetDataRaw throws" vs. "SetData(VPC*, count) throws").

### Behavioral correctness
All twelve `check()` calls were cross-verified against production source, not assumed:
- `VertexBuffer::SetData(verts,3)` / `SetData(verts,0,3)` / `SetDataRaw(verts,3,16)` (lines 74-79) — each of
  `VertexBuffer.cpp`'s corresponding methods opens with `if (getIsDisposedProperty()) throw
  System::ObjectDisposedException("VertexBuffer");` (confirmed at lines 56-57, 98-99, 380-383) before touching
  `backend_`.
- `DynamicVertexBuffer::SetData(..., SetDataOptions::{None,Discard,NoOverwrite})` (lines 88-93) — all three forward
  to `VertexBuffer::SetDataWithOptions`, which itself disposed-guards (verified `VertexBuffer.cpp:392-396` for the
  `VertexPositionColor` overload actually invoked here).
- `dev.SetVertexBuffer(&vb)` on a disposed `vb` (line 102) — `GraphicsDevice::SetVertexBuffer`
  (`GraphicsDevice.cpp:514-519`): `if (vertexBuffer && vertexBuffer->getIsDisposedProperty()) throw
  System::ObjectDisposedException(vertexBuffer->getNameProperty());` — matches.
- `IndexBuffer::SetData` (16-bit and 32-bit, 2-arg and 3-arg, lines 111-125) — `IndexBuffer.cpp` has the identical
  guard pattern at lines 55-56, 76-77 (verified for both element-size code paths).
- `DynamicIndexBuffer::SetData(..., options)` (lines 133-138) — forwards to `IndexBuffer::SetDataWithOptions`,
  guarded (`IndexBuffer.cpp:92-93` region).
- `dev.SetIndexBuffer(&ib)` on disposed `ib` (line 146) — `GraphicsDevice::SetIndexBuffer`
  (`GraphicsDevice.cpp:521-525`) mirrors the vertex-buffer guard exactly.
- `dev.SetVertexBuffer(nullptr)` / `dev.SetIndexBuffer(nullptr)` (lines 151-164) — both guards above are written as
  `if (vertexBuffer && ...)` / `if (indexBuffer && ...)`, i.e. a null pointer short-circuits past the disposed check
  entirely and falls through to the plain assignment — confirmed this cannot throw for a null argument.

Every check in this file is therefore evidence-backed, not merely plausible.

### Logic
Straight-line sequence of nine independent `{ }`-scoped blocks in `Initialize()`, each constructing a fresh
resource, disposing it, and probing one or more post-dispose calls via the local `throws<Fn>()` helper (lines
42-48), which returns `true` only when the thrown exception is specifically `System::ObjectDisposedException` (a
bare `catch (...)` returns `false`, so a wrong-exception-type regression would correctly flip the check to FAIL, not
a false pass). `posColorDecl()` (lines 50-56) is reused across the VB/DVB blocks — matches `VertexPositionColor`'s
actual field layout (`Position` `Vector3` at offset 0, `Color` `Color` at offset 12, stride 16 — verified against
`VertexPositionColor.hpp`).

### Memory/resource lifetime
Each resource is constructed, disposed, and probed within its own `{ }` block and goes out of scope immediately
after — no lifetime overlap between blocks, no reuse-after-dispose beyond the deliberate probes under test. `gdm_`
is a `unique_ptr` owned by the test class for the whole run, standard pattern for this test population.

### C++ correctness
The `throws<Fn>` template (lines 42-48) correctly distinguishes "no throw" (`return false`), "threw the expected
type" (`return true`), and "threw something else" (`return false`) — no ambiguity between these three outcomes.
`idx16`/`idx32`/`verts` are stack arrays sized exactly to the counts passed (3), no out-of-bounds read risk in the
`SetData` calls under test.

### Robustness
The two "must NOT throw" checks (nullptr unbinding, lines 150-164) are exactly the right shape to catch a
regression that made the disposed-guard unconditional (`if (vertexBuffer->getIsDisposedProperty())` without the
null-check) — a mistake that would be easy to introduce and would crash on the extremely common
`SetVertexBuffer(nullptr)` unbind idiom. Good defensive test design.

### Testing
This file only tests the *type* of exception thrown (`catch (const System::ObjectDisposedException&)`), never its
message content — see Finding F1 for a related, low-severity production-code naming inconsistency this leaves
unexercised. The non-disposed ("happy path") `SetData` behavior is intentionally out of scope here and is covered
by the sibling `easygl_vertexbuffer_setdata_test.cpp`.

## Detailed Findings

### F1 — Inconsistent exception-message identity between VertexBuffer/IndexBuffer's own guards and GraphicsDevice's guards

- Severity: LOW
- Confidence: HIGH
- Category: cross-file-consistency
- Location/symbol: `VertexBuffer.cpp` / `IndexBuffer.cpp` internal `SetData*`/`SetDataRaw` guards (hardcode the
  literal string `"VertexBuffer"` / `"IndexBuffer"` as the exception's object name) vs.
  `GraphicsDevice::SetVertexBuffer`/`SetIndexBuffer` (`GraphicsDevice.cpp:514-525`, use
  `vertexBuffer->getNameProperty()`/`indexBuffer->getNameProperty()` — the resource's actual, possibly-empty,
  user-assigned `Name`).
- Evidence: both of the buffers constructed in this test are never given a `Name` (no `setNameProperty()` call
  anywhere in the file), so in practice `getNameProperty()` returns an empty string for the `GraphicsDevice`-path
  exceptions, while the buffer's own internal guards always report a fixed, non-empty class-name string. This test
  cannot detect the difference since it only checks exception *type*.
- Why it matters: a caller catching `ObjectDisposedException` and logging/displaying its message would see
  `"VertexBuffer"` from a direct `SetData()` call but an empty (or, if named, the user's own) string from a
  `GraphicsDevice::SetVertexBuffer()` call on the exact same object — a minor but real inconsistency in what
  information reaches the user, and a small missed opportunity to use `getNameProperty()` (falling back to the
  class name when empty) consistently in both places.
- FNA/XNA comparison: FNA's own `AssertNotDisposed()` helper (used identically across `VertexBuffer`, `IndexBuffer`,
  etc.) always passes `GetType().Name` — a compile-time-fixed per-type name — so FNA's own behavior more closely
  matches CNA's buffer-internal guards, not `GraphicsDevice`'s use of the mutable per-instance `Name` property.
  Arguably `GraphicsDevice`'s use of the instance name is the actual outlier here, not the other way around.
- Related files: `VertexBuffer.cpp`, `IndexBuffer.cpp`, `GraphicsDevice.cpp`.
- Suggested future action (not implemented by this audit): standardize on one convention (most likely: always pass
  the type name, matching FNA and matching the majority of existing call sites) across all three files, if either
  is touched again for other reasons. Not a test-file defect — this file's checks remain valid as written.

## Cross-File Observations

- `VertexBuffer::SetDataRaw` (a `NOXNA` extension) shares the exact same disposed-guard shape as every XNA-named
  `SetData` overload in the same file (`VertexBuffer.cpp:380-383`) — consistent internal convention, correctly
  distinguished by this test's own check labels from the XNA-named overloads.
- `DynamicVertexBuffer`/`DynamicIndexBuffer` (header-only, no `.cpp`) add no disposed-guard logic of their own —
  they rely entirely on delegating to the base class's already-guarded `SetDataWithOptions`, which this test
  correctly exercises via the subclass API rather than the base-class API directly.

## Missing or Weak Tests

- Exception *message* content is never asserted (see F1) — a `getMessage()`/`what()`-string check for at least one
  case would have caught the naming inconsistency described above.
- No check exists for the `NOXNA VertexBuffer(GraphicsDevice&, int vertexCount)` constructor variant specifically
  (this file only uses the full `VertexDeclaration`-taking constructor) — low priority, since the disposed-guard
  logic under test lives in the shared `SetData*` methods, not in either constructor.

## Positive Findings

- All twelve disposed/non-disposed checks are accurate, evidence-backed reproductions of real production guard
  logic — no boilerplate or unverified assumption found anywhere in this file.
- The explicit `nullptr`-does-not-throw checks (lines 150-164) are a genuinely valuable regression guard against a
  very plausible future mistake (tightening the guard to remove the null check).

## Final Assessment

A tight, accurate, and well-targeted disposed-object regression test for the buffer-resource family — every check
was traced to and confirmed against the real guard code it claims to exercise. The only finding is a pre-existing,
low-severity production-code naming inconsistency (F1) that this test's type-only exception check cannot and does
not claim to catch.
