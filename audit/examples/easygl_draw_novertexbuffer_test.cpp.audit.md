# Audit: examples/easygl_draw_novertexbuffer_test.cpp

## Metadata

- Source file: `examples/easygl_draw_novertexbuffer_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration test
- File type: C++ executable test (`Game` subclass, no gtest)
- Lines: 87
- XNA/FNA relevance: exercises `GraphicsDevice::DrawPrimitives`/`DrawIndexedPrimitives`/`DrawInstancedPrimitives`
- FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/GraphicsDevice.cs`
- Production code under test: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`DrawPrimitives` lines 564-589, `DrawIndexedPrimitives` lines 591-632, `DrawInstancedPrimitives` lines 634-690)

## Purpose

Task 203 regression test: `DrawNoVertexBufferTest` explicitly unbinds the vertex buffer
(`device.SetVertexBuffer(nullptr)`) and asserts that all three draw entry points —
`DrawPrimitives`, `DrawIndexedPrimitives`, `DrawInstancedPrimitives` — throw `std::runtime_error` before touching
the GPU backend. Runs entirely in `Initialize()`; no frame is drawn. Correctly placed as a `GraphicsDevice`-level
contract test.

## Executive Verdict

**Healthy.** Verifies a real guard clause that is present and correctly ordered (first check in all three methods)
in the production code, with an accurate one-line justification per sub-test. Same minor diagnostic weakness as
its sibling file (F1, shared pattern).

## Checklist Results

### API / XNA / FNA parity
`DrawPrimitives(PrimitiveType, int vertexStart, int primitiveCount)` (line 43) matches FNA's `GraphicsDevice.cs`
line 1294 exactly. `DrawIndexedPrimitives`/`DrawInstancedPrimitives` signatures (lines 52, 61) match FNA lines 1232
and 1257. As with the sibling `noindexbuffer` test, FNA itself has no explicit vertex-buffer-null guard at this
layer (it would instead pass a null/garbage `GLDevice` state down to `FNA3D_DrawPrimitives` et al.) — CNA's
`currentVertexBuffer_ == nullptr` guard is a deliberate CNA-added safety net, correctly described by the test's
own header comment as such.

### Behavioral correctness
Traced all three methods in `GraphicsDevice.cpp`: `DrawPrimitives` checks `currentVertexBuffer_` at line 569-570
(first check, before the effect-null check at 572-573) — confirmed reached given `currentEffect_` is also null in
this fixture, so the VB-null throw fires first as intended. Same ordering confirmed for `DrawIndexedPrimitives`
(606-607 before 609-610 before 612-613) and `DrawInstancedPrimitives` (650-652 before 654-656 before 658-660). All
three throw plain `std::runtime_error`, matching the test's `catch (const std::runtime_error&)`.

### Logic
Three independent, symmetric sub-tests (lines 40-65), each isolated to a single draw method with identical
all-zero benign arguments — reasonable, since the point is to isolate the VB-null guard, not exercise argument
validation (that's the dedicated `easygl_draw_range_validation_test.cpp`).

### C++ correctness
Same `catch (const std::runtime_error&) {...} catch (...) {}` pattern as the sibling file — see F1 in
`easygl_draw_noindexbuffer_test.cpp.audit.md` (identical finding, not re-derived here to avoid duplication, but
listed as LOW/HIGH-confidence there and equally applicable here).

### Memory/resource lifetime
No `VertexBuffer` object is constructed in this file at all (the whole point is that none is bound) — no lifetime
concern.

### Robustness
Benign zero arguments used throughout (`0,1`, `0,0,0,0,1`, `0,0,0,0,1,1`) confirm the test isolates the VB-null
guard rather than conflating it with range checks.

### Testing
This file is itself the coverage for the VB-null guard across all three draw entry points — the most complete of
the two null-buffer-guard test files in this shard (its `noindexbuffer` sibling only covers two of the three
methods, since `DrawPrimitives` has no index buffer to omit).

## Detailed Findings

No new findings beyond the diagnostic-granularity observation already logged as F1 in the sibling
`easygl_draw_noindexbuffer_test.cpp` report (identical `catch (...) {}` pattern, same LOW severity, not
re-recorded here as a duplicate to keep this audit's findings list non-redundant).

## Cross-File Observations

- Together with `easygl_draw_noindexbuffer_test.cpp`, gives complete coverage of the null-vertex-buffer and
  null-index-buffer guards across all XNA draw entry points that have them (`DrawPrimitives` has no IB guard by
  definition, since it doesn't use one).
- Neither file's fixture asserts on the specific *message text* of the thrown `std::runtime_error` (e.g. "no
  vertex buffer is bound") — only the exception type. This is a reasonable, low-value thing to skip (message text
  is not part of any documented contract), noted only for completeness.

## Missing or Weak Tests

None beyond the shared observation already logged in the sibling report (no isolated "no effect applied" guard
test exists as its own dedicated case anywhere in this shard).

## Positive Findings

- Full three-method coverage of the VB-null guard in one file, correctly isolated from range-validation concerns.
- Clear, accurate task/header comment that matches what the code actually does (no overstatement).

## Final Assessment

A correctly-targeted, accurate regression test for a real, correctly-ordered CNA-added robustness guard. No
correctness gaps found; only the same low-value diagnostic-granularity note as its sibling file.
