# Audit: examples/sdlrenderer_drawprimitives_throws_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_drawprimitives_throws_test.cpp` (136 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 720, exact exception type+message for the three
  already-bound-vertex-buffer draw entry points
- File type: standalone `Game`-subclass executable (`SdlDrawPrimitivesThrowsTest`), pass/fail counter, exit-code
- XNA/FNA relevance: `GraphicsDevice.DrawPrimitives`/`DrawIndexedPrimitives`/`DrawInstancedPrimitives`,
  `VertexBuffer`/`IndexBuffer` construction.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`DrawPrimitives` lines 564-589, `DrawIndexedPrimitives` lines 591-631, `DrawInstancedPrimitives` lines 634-690),
  `src/Microsoft/Xna/Framework/Graphics/VertexBuffer.cpp` (line 35),
  `src/Microsoft/Xna/Framework/Graphics/IndexBuffer.cpp` (lines 13-16, 32-34),
  `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`CreateVertexBuffer`/`CreateIndexBuffer16`, lines 795-804, `ThrowNo3D`, lines 779-783).
- Git corroboration: `25c346fc`/`adb951f6` `test(Task 720): verify Draw*Primitives throw exact exception
  type+message on SDL_Renderer`.

## Purpose

Proves an exact chain of causation on this intentionally-2D-only backend: `DrawPrimitives`/`DrawIndexedPrimitives`/
`DrawInstancedPrimitives` all check "is a vertex buffer bound" as a shared, backend-agnostic first step, and since
`VertexBuffer`'s own constructor calls `GraphicsDevice::GetBackend().CreateVertexBuffer(...)` directly in its
member-initializer list — which SDL_Renderer's backend throws unconditionally — no valid `VertexBuffer` can ever be
successfully constructed against an SDL_Renderer-backed device. This test verifies both halves: the exact
exception type/message for all three draw calls with no vertex buffer bound, AND the exact exception type/message
for the `VertexBuffer`/`IndexBuffer` constructors themselves that make that state the *only* reachable one.

## Executive Verdict

**Healthy** — all five distinct exact-message assertions (3 draw entry points + 2 constructors) were independently
confirmed character-for-character against the real production strings, and the `ThrowsExactRuntimeError` helper
correctly narrows to the specific exception type (`std::runtime_error`, not any `std::exception`).

## Checklist Results

### API / XNA / FNA parity
`GraphicsDevice.DrawPrimitives(PrimitiveType, int, int)` / `DrawIndexedPrimitives(...)` /
`DrawInstancedPrimitives(...)` signatures used here match FNA's own `GraphicsDevice` overloads
(`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/GraphicsDevice.cs`). FNA itself throws
`InvalidOperationException` ("Vertex buffer must be bound...") for a missing vertex buffer rather than
`std::runtime_error`; this project's established convention (per `CLAUDE.md`'s "Behavior Fidelity" section,
"Exception behavior where practical") maps this to `std::runtime_error` with a project-specific message format —
this is a documented, acceptable C++-idiom deviation, not an unflagged parity gap, and this test only asserts the
CNA-side contract (not literal FNA exception-type parity), which is the correct scope for a backend-level test.

### Behavioral correctness
Every exact string was independently confirmed against the current source, not assumed from the test's own claims:
- `dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1)` with nothing bound →
  `"GraphicsDevice::DrawPrimitives: no vertex buffer is bound."` — confirmed verbatim at `GraphicsDevice.cpp` line 570.
- `dev.DrawIndexedPrimitives(...)` →
  `"GraphicsDevice::DrawIndexedPrimitives: no vertex buffer is bound."` — confirmed verbatim at line 607.
- `dev.DrawInstancedPrimitives(...)` →
  `"GraphicsDevice::DrawInstancedPrimitives: no vertex buffer is bound."` — confirmed verbatim at lines 651-652.
- `VertexBuffer vb(dev, 4);` → `"SDL_Renderer does not support 3D: CreateVertexBuffer"` — confirmed: the 2-arg
  `VertexBuffer` constructor (`VertexBuffer.cpp` line 35) initializes `backend_` directly from
  `device.GetBackend().CreateVertexBuffer(vertexCount)`, and `SdlGraphicsBackend::CreateVertexBuffer`
  (`SdlGraphicsBackend.cpp` lines 795-799) calls `ThrowNo3D("CreateVertexBuffer")`, which formats exactly
  `"SDL_Renderer does not support 3D: " + methodName` (lines 779-783) — an exact match.
- `IndexBuffer ib(dev, 4);` → `"SDL_Renderer does not support 3D: CreateIndexBuffer16"` — confirmed: the 2-arg
  `IndexBuffer` constructor (`IndexBuffer.cpp` lines 13-16) delegates to the 4-arg constructor with
  `IndexElementSize::SixteenBits` as the default, whose member-initializer (line 32-34) calls
  `device.GetBackend().CreateIndexBuffer16(indexCount)` for that element size, which throws
  `ThrowNo3D("CreateIndexBuffer16")` — an exact match, and correctly proves the constructor's default index width
  (16-bit) is what's actually exercised here, not an assumption.

The test's own reasoning ("a valid VertexBuffer/IndexBuffer object can never exist bound to an SDL_Renderer
GraphicsDevice in the first place") is thus independently confirmed, not merely asserted.

### Logic
`ThrowsExactRuntimeError`'s `catch (const std::runtime_error& e)` branch correctly returns `false` if the caught
exception is a `std::runtime_error` subtype-mismatch on message, and the separate
`catch (const std::exception&) { return false; }` branch correctly fails the check (rather than false-passing) if
some other exception type entirely were thrown — both branches were checked and are logically sound (no ordering
bug: `std::runtime_error`'s handler comes first, so it always wins for that type; the broader handler only catches
non-`runtime_error` exceptions since C++ tries handlers in declaration order).

### Robustness
The closing check (`dev.Clear(Color(0, 255, 255, 255))` + `GetBackBufferData` +
`pixel.getGProperty() >= 240 && pixel.getBProperty() >= 240`) proves the device survived all five throws (three
draw-call throws plus two constructor throws) without corrupted state — meaningful given `VertexBuffer`/
`IndexBuffer`'s partially-constructed state on throw (the member-initializer list throwing means the `VertexBuffer`/
`IndexBuffer` object itself never fully exists — no destructor runs on it, avoiding any double-teardown risk from a
half-constructed `unique_ptr<IVertexBufferBackend>` member, since `CreateVertexBuffer` returns `nullptr` only after
throwing, meaning the member is never assigned a live backend to begin with).

### Testing
The `pass_`/`fail_` counter with the final `"=== %d/%d PASS ==="` summary line is a nice touch for quick regression
triage (visible directly in test output without needing to parse individual PASS/FAIL lines), used consistently by
several files in this shard.

## Detailed Findings

No HIGH or CRITICAL findings. See F1 under "Cross-File Observations" for the shared minor pattern already
documented in the `sdlrenderer_double_dispose_test.cpp` report (closing check doesn't assert an upper bound on the
unlit R channel) — not re-detailed here to avoid duplication.

## Cross-File Observations

- Shares the "asserts only the lit G/B channels, no upper bound on R" pattern with
  `sdlrenderer_double_dispose_test.cpp`, `sdlrenderer_disposed_guards_test.cpp`,
  `sdlrenderer_drawuserprimitives_throws_test.cpp`, `sdlrenderer_drawuserindexedprimitives_throws_test.cpp` (all
  audited in this batch) — see the double_dispose report's F1 for the one canonical write-up of this shard-wide,
  LOW-severity pattern.
- This file is the natural sibling/prerequisite of `sdlrenderer_drawuserprimitives_throws_test.cpp` and
  `sdlrenderer_drawuserindexedprimitives_throws_test.cpp` (both audited in this same batch): together the three
  cover all 3+5+10 = 18 draw-entry-point variants this backend exposes for 3D-unsupported behavior.

## Missing or Weak Tests

- No check that `DrawPrimitives`/`DrawIndexedPrimitives`/`DrawInstancedPrimitives` would reach a *different* message
  (e.g. "no effect has been applied") if a `VertexBuffer` COULD somehow be bound without one existing (impossible on
  this backend, so this is a purely theoretical gap the test's own header comment already explains is unreachable
  here — not a real omission).

## Positive Findings

- All five exact exception messages were independently confirmed character-for-character against the current
  source, not merely trusted from the test's own comment.
- Clear, well-reasoned chain of causation documented and then verified: shared "no vertex buffer" check fires first
  because SDL_Renderer's `CreateVertexBuffer`/`CreateIndexBuffer16` throw before a `VertexBuffer`/`IndexBuffer` can
  ever exist, making the "no effect applied"/"no index buffer bound" branches provably unreachable on this backend.
- Strong `pass_`/`fail_` summary reporting convention.

## Final Assessment

A precise, thoroughly-verified test. Every exact-message assertion checks out against the real source with no
discrepancies found.
