# Audit: examples/easygl_draw_range_validation_test.cpp

## Metadata

- Source file: `examples/easygl_draw_range_validation_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration test
- File type: C++ executable test (`Game` subclass, no gtest)
- Lines: 116
- XNA/FNA relevance: exercises `GraphicsDevice::DrawPrimitives`/`DrawIndexedPrimitives`/`DrawInstancedPrimitives`/
  `DrawUserPrimitives` argument-range guards, all real XNA 4.0 API entry points
- FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/GraphicsDevice.cs`
- Production code under test: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (guard clauses at lines
  575-576, 615-617, 662-665, 875) and `sharp-runtime`'s
  `include/System/ArgumentOutOfRangeException.hpp` (`ThrowIfNegative`/`ThrowIfNegativeOrZero` templates)

## Purpose

Task 205 test: `DrawRangeValidationTest` binds a real vertex buffer, index buffer, and applied `BasicEffect` (so
every guard *before* the range checks is satisfied), then calls each of `DrawPrimitives`, `DrawIndexedPrimitives`,
`DrawInstancedPrimitives`, and `DrawUserPrimitives` with one deliberately invalid argument at a time, asserting
each throws `System::ArgumentOutOfRangeException` via the `throwsAOOR<F>` helper (lines 31-37). The file's own
header comment correctly labels these guards `NOXNA` in spirit ("FNA does not validate at this level") — confirmed
below.

## Executive Verdict

**Healthy.** Every one of the 8 assertions is traceable to a real, correctly-ordered guard clause in
`GraphicsDevice.cpp`, and the chosen argument combinations correctly isolate the guard under test from the guards
checked earlier in the same method (verified precisely below, not just spot-checked).

## Checklist Results

### API / XNA / FNA parity
Confirmed against `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/GraphicsDevice.cs`: none of
`DrawPrimitives`/`DrawIndexedPrimitives`/`DrawInstancedPrimitives`/`DrawUserPrimitives<T>` perform explicit
argument-range validation in FNA itself (FNA's bodies go straight to `ApplyState()` +
`FNA3D_Draw*Primitives`/`PrepareUserVertexBuffer`/`PrepareUserIndexBuffer` with no C#-side guard clause). The test
file's header comment ("Guards added (NOXNA — FNA does not validate at this level)") is therefore an accurate,
verified statement, not an assumption — this is a genuine, intentional CNA robustness addition layered on top of
XNA-named methods, correctly understood by whoever wrote the test.

### Behavioral correctness
Traced every one of the 8 sub-tests against the exact guard ordering in `GraphicsDevice.cpp`:
- `DrawPrimitives(TriangleList, 0, 0)` → `primitiveCount=0` hits `ThrowIfNegativeOrZero(primitiveCount)` at line 575
  (checked before `vertexStart` at 576) — correct isolation.
- `DrawPrimitives(TriangleList, 0, -1)` → `primitiveCount=-1`, same guard (575), `<=0` → throws.
- `DrawPrimitives(TriangleList, -1, 1)` → `primitiveCount=1` passes 575, `vertexStart=-1` hits
  `ThrowIfNegative(vertexStart)` at 576 — correctly isolates the *second* guard by giving a valid `primitiveCount`.
- `DrawIndexedPrimitives(..., 0,0,0,0,0)` → `primitiveCount=0` hits line 615 (checked before `startIndex`/616 and
  `baseVertex`/617).
- `DrawIndexedPrimitives(..., 0,0,0,-1,1)` → `primitiveCount=1` passes 615, `startIndex=-1` hits 616.
- `DrawIndexedPrimitives(..., -1,0,0,0,1)` → `primitiveCount=1` passes 615, `startIndex=0` passes 616,
  `baseVertex=-1` hits 617 — correctly the *last* of the three guards, isolated by giving valid values for the
  first two.
- `DrawInstancedPrimitives(..., 0,0,0,0,0,1)` → `primitiveCount=0` hits line 662 (checked before `startIndex`/663,
  `baseVertex`/664, `instanceCount`/665).
- `DrawInstancedPrimitives(..., 0,0,0,0,1,0)` → `primitiveCount=1`, `startIndex=0`, `baseVertex=0` all pass,
  `instanceCount=0` hits `ThrowIfNegativeOrZero(instanceCount)` at 665 — correctly isolates the last guard, and
  confirms `DrawInstancedPrimitives`'s prerequisite index-buffer guard (line 654-656) is also satisfied by the
  test's setup (an `IndexBuffer` is bound at line 61), otherwise this call would throw the wrong exception type
  (`std::runtime_error`, not `ArgumentOutOfRangeException`) and the `throwsAOOR` helper would report it as `false`
  (a false negative that the test would correctly still fail on, just for a different underlying reason than the
  label claims — no live bug here since the fixture is correctly set up, but see F1).
- `DrawUserPrimitives(TriangleList, dummy, 0, 0)` → matches the typed `DrawUserPrimitives(PrimitiveType, const
  VertexPositionColor*, int, int)` overload (`GraphicsDevice.cpp:869-892`) by exact-type overload resolution
  (`dummy` is `VertexPositionColor[3]`, decaying to `const VertexPositionColor*` — a better match than the
  competing `const void*` overload) — `count=0` hits `ThrowIfNegativeOrZero(count)` at line 875, before any
  `VertexCountForUserPrimitives`/packing work touches the (uninitialized-content but correctly-sized) `dummy`
  array.

### Logic
`throwsAOOR<F>` (lines 31-37) is a clean generic helper: returns `false` on no throw, `true` only on
`ArgumentOutOfRangeException`, `false` on any other exception type — correct semantics for "did exactly the
expected exception fire."

### C++ correctness
`throwsAOOR` takes `F&&` and calls `fn()` — no dangling-reference risk since each call site constructs and invokes
the lambda immediately (line 68 etc.), all captures are by reference to locals (`device`) that outlive the call.

### Robustness
The fixture correctly satisfies every guard that runs *before* the range checks for every method under test
(vertex buffer, index buffer, and an `Apply()`-ed `BasicEffect`, lines 57-65) — this is what allows each sub-test
to isolate exactly one range guard rather than incidentally exercising an earlier null-buffer/null-effect guard.
This was verified by cross-referencing the guard order in `GraphicsDevice.cpp`, not assumed.

### Testing
This file is the sole dedicated coverage for range-guard behavior across four draw entry points. It does not cover
the `DrawUserIndexedPrimitives` family's own `primitiveCount` guard (see Missing/Weak Tests) even though that
family has the identical `ThrowIfNegativeOrZero(primCount)` pattern (confirmed at `GraphicsDevice.cpp:1024`,
`1146`, etc. for every typed overload) — a real, findable gap given how systematically the other four methods are
covered here.

## Detailed Findings

### F1 — `DrawInstancedPrimitives`'s `instanceCount=0` sub-test silently depends on the earlier index-buffer guard, not called out

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage / maintainability
- Location/symbol: line 86-87 (`DrawInstancedPrimitives: instanceCount=0 ...`), cross-referenced against
  `GraphicsDevice.cpp:654-656` (`currentIndexBuffer_ == nullptr` throws `std::runtime_error`, not AOOR)
- Evidence: `DrawInstancedPrimitives` requires a non-null index buffer (line 654-656) *before* it reaches the
  `primitiveCount`/`startIndex`/`baseVertex`/`instanceCount` range guards at 662-665. The test's `LoadContent()`
  does bind an `IndexBuffer` (line 61) so this is satisfied today, but nothing in the test or its comments states
  that this shared fixture setup is *load-bearing* for the `DrawInstancedPrimitives` sub-tests specifically (only
  `DrawIndexedPrimitives` obviously needs it) — a future edit that, say, moved the IB binding to be conditional or
  scoped differently could silently flip this sub-test's failure mode from "AOOR not thrown" to "wrong exception
  type entirely," and the `throwsAOOR` helper's `catch (...) { return false; }` would mask this as an ordinary
  assertion failure rather than surfacing the real cause.
- Why it matters: purely a test-maintainability/triage concern — today's test is correct and passes for the right
  reason, this is about resilience to future refactors of this same file.
- FNA/XNA comparison: N/A.
- Suggested action (not implemented by this audit): a one-line comment at the `DrawInstancedPrimitives` sub-tests
  noting that the shared index buffer from setup is required for these calls to reach the range guards at all.

## Cross-File Observations

- `easygl_draw_noindexbuffer_test.cpp` and `easygl_draw_novertexbuffer_test.cpp` cover the null-buffer guards that
  run *before* these range guards; this file assumes (correctly, per the fixture) that those all pass. Together
  the three files give layered, non-overlapping coverage of `GraphicsDevice`'s guard-clause ordering.
- The range-guard pattern (`ThrowIfNegativeOrZero`/`ThrowIfNegative` from `sharp-runtime`) is applied consistently
  across `DrawPrimitives`, `DrawIndexedPrimitives`, `DrawInstancedPrimitives`, and every `DrawUserPrimitives`/
  `DrawUserIndexedPrimitives` overload in `GraphicsDevice.cpp` — a genuinely uniform internal convention, not
  ad-hoc per-method validation.

## Missing or Weak Tests

- `DrawUserIndexedPrimitives` (all typed overloads) has the identical `ThrowIfNegativeOrZero(primCount)` guard
  (e.g. `GraphicsDevice.cpp:1024`) but is **not** exercised by this file or, as far as this shard's file list
  shows, by any other range-validation test — a genuine coverage gap for a guard that exists and is real
  production behavior.
- No sub-test covers `DrawUserPrimitives`'s *explicit-`VertexDeclaration`* overload's range guard (line 981 in
  `GraphicsDevice.cpp`) separately from the typed-VPC overload's guard (line 875) — both exist and both could
  regress independently since they're separate method bodies, not shared code.

## Positive Findings

- Every one of the 8 assertions was verified, not assumed, to isolate exactly the guard its label claims — the
  argument choices are deliberately constructed (valid values for earlier-checked parameters, invalid for the one
  under test) rather than all-zero, which is what makes this file's coverage genuinely meaningful rather than
  coincidental.
- Correctly distinguishes `ArgumentOutOfRangeException` (this file) from `std::runtime_error` (the sibling
  null-buffer-guard files) — a real, verified type-level distinction in the production code, not a test artifact.

## Final Assessment

A precise, well-constructed test that earns its "genuinely validates the claimed behavior" status — each of its
8 assertions was traced to a specific guard-clause line and confirmed to isolate that guard from its neighbors.
The only gaps are coverage breadth (the `DrawUserIndexedPrimitives` family's identical guard is untested) rather
than correctness of what is tested.
