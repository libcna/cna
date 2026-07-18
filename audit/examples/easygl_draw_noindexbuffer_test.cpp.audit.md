# Audit: examples/easygl_draw_noindexbuffer_test.cpp

## Metadata

- Source file: `examples/easygl_draw_noindexbuffer_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration test
- File type: C++ executable test (`Game` subclass, no gtest)
- Lines: 87
- XNA/FNA relevance: exercises `GraphicsDevice::DrawIndexedPrimitives`/`DrawInstancedPrimitives`
  (`Microsoft::Xna::Framework::Graphics`), a real XNA 4.0 API surface
- FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/GraphicsDevice.cs` (no equivalent guard —
  see Behavioral correctness)
- Production code under test: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`DrawIndexedPrimitives` lines 591-632, `DrawInstancedPrimitives` lines 634-690)

## Purpose

Task 204 regression test: `DrawNoIndexBufferTest` binds a 6-vertex `VertexBuffer` (so the earlier VB-null guard
is satisfied) but leaves `currentIndexBuffer_` null, then asserts that both `DrawIndexedPrimitives` and
`DrawInstancedPrimitives` throw `std::runtime_error` before ever reaching the GPU backend. All work happens
synchronously inside `Initialize()` (no `Draw()` frame is needed since nothing is rendered), and the process exits
via `Exit()` with a non-zero return code (`getResult()`) if any `check()` failed. Correct placement — a
backend-agnostic `GraphicsDevice` contract test that happens to run under the EasyGL backend by build wiring, not
an EasyGL-specific rendering test.

## Executive Verdict

**Healthy.** The test asserts exactly what its name and header comment claim, the production guard it exercises
is real and correctly ordered, and no rendering happens so backend choice is incidental — but see Finding F1 for a
correctness detail worth tightening (the test currently can't tell "threw the right exception" from "threw the
right exception type for the wrong reason").

## Checklist Results

### API / XNA / FNA parity
`DrawIndexedPrimitives(PrimitiveType, int baseVertex, int minVertexIndex, int numVertices, int startIndex, int
primitiveCount)` and `DrawInstancedPrimitives(..., int instanceCount)` signatures called at lines 49 and 58 match
FNA's `GraphicsDevice.cs` (`DrawIndexedPrimitives` at line 1232, `DrawInstancedPrimitives` at line 1257) exactly in
parameter order and count. FNA itself performs **no** null-buffer guard at this layer — it calls `ApplyState()` and
hands straight to `FNA3D_DrawIndexedPrimitives`/`FNA3D_DrawInstancedPrimitives`, which would fail at the native
layer (undefined/driver-dependent) if `Indices` were null. CNA's `currentIndexBuffer_ == nullptr` guard
(`GraphicsDevice.cpp:609-610`, `654-656`) throwing `std::runtime_error` up-front is a CNA-added robustness
improvement, not a divergence the test mischaracterizes — the test's own header comment correctly frames it as
verifying CNA's own guard, not an XNA-mandated behavior.

### Behavioral correctness
Confirmed against `GraphicsDevice.cpp`: `DrawIndexedPrimitives` checks `currentVertexBuffer_` (line 606-607) before
`currentIndexBuffer_` (609-610) before `currentEffect_` (612-613) before the `ArgumentOutOfRangeException` range
guards (615-617) — the test's setup (bind VB, leave IB null, **no effect bound at all**) means the IB-null throw at
line 610 is reached correctly, since it comes before the effect-null check. Verified the same ordering holds for
`DrawInstancedPrimitives` (650-660). Both throw `std::runtime_error` (not `ArgumentOutOfRangeException` or an XNA
exception type) exactly as the test expects via `catch (const std::runtime_error&)`.

### Logic
`check()` (lines 27-31) is a minimal pass/fail counter with `std::printf` output — no gtest, matching this
manifest's stated non-gtest-example convention. The two sub-tests are independent and order does not matter here
since each starts from the same fixture state (VB bound, IB null, no effect).

### C++ correctness
`catch (const std::runtime_error&) { threw = true; } catch (...) {}` (lines 50-51, 59-60) is a defensible pattern
for "this specific exception type or nothing" but see F1 — it silently absorbs any other exception without
recording that the *wrong* exception type was thrown, which would still register as `threw = false` (fine) but
gives no diagnostic as to *why* in the printed PASS/FAIL log.

### Memory/resource lifetime
`VertexBuffer vb(device, 6)` is a local, unbound at line 65 (`device.SetVertexBuffer(nullptr)`) before
`Initialize()` returns — correct cleanup ordering, avoids leaving a dangling `currentVertexBuffer_` pointer to a
soon-to-be-destroyed stack object once the function returns (a real risk given `GraphicsDevice` stores a raw,
non-owning `VertexBuffer*`).

### Robustness
Both draw calls use benign all-zero arguments (`0,0,0,0,1` / `0,0,0,0,1,1`) — since the guard clauses fire before
argument range validation, this is fine and intentional; it isolates exactly the IB-null guard rather than
conflating it with the separate range-validation guards (which get their own dedicated test,
`easygl_draw_range_validation_test.cpp`, in this same shard).

### Testing
This file itself *is* the test coverage for the IB-null-guard behavior of these two methods. No separate unit test
duplicates it (reasonable — this is the correct place for it, matching the task-per-file convention visible across
this manifest).

## Detailed Findings

### F1 — `catch (...) {}` cannot distinguish "no exception" from "wrong exception type"

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage / diagnostics
- Location/symbol: lines 50-51, 59-60 (`catch (const std::runtime_error&) { threw = true; } catch (...) {}`)
- Evidence: if a future refactor changed the IB-null guard to throw, say, `System::InvalidOperationException`
  instead of `std::runtime_error`, this test would report `[FAIL] ... throws runtime_error when no IB bound` with
  no indication of *what* was actually thrown (or that anything was thrown at all) — `threw` stays `false` either
  way, whether nothing threw or the wrong thing threw.
- Why it matters: purely a debuggability/triage cost, not a false-pass risk — the test still correctly fails on a
  behavior change, it just doesn't say why. Low value to fix but cheap (log `e.what()` for the caught type, or a
  brief note in the catch-all branch).
- FNA/XNA comparison: N/A (test-only concern).
- Suggested action (not implemented by this audit): add a `catch (const std::exception& e) { std::printf("  (unexpected: %s)\n", e.what()); }` branch before the catch-all for easier future triage.

## Cross-File Observations

- Sibling test `easygl_draw_novertexbuffer_test.cpp` (Task 203) covers the VB-null guard for all three draw
  methods (`DrawPrimitives`/`DrawIndexedPrimitives`/`DrawInstancedPrimitives`); together the two files give full
  coverage of the null-buffer guard matrix except `DrawInstancedPrimitives`'s VB-null case is covered by the
  sibling file, not this one — consistent, non-overlapping split.
- Neither file tests the null-*effect* guard in isolation (both leave `currentEffect_` null incidentally, since
  neither test binds one, but that's a side-effect of the fixture, not a targeted assertion) — that guard is
  exercised as a side condition in `easygl_draw_range_validation_test.cpp` (which binds a real `BasicEffect`
  specifically to get past it) but never has its own dedicated "no effect bound" positive test in this shard.

## Missing or Weak Tests

- No test in this shard directly asserts "no effect applied" throws `std::runtime_error` as its own isolated
  case (analogous to this file's IB-null and the sibling's VB-null tests) — it's only ever incidentally true
  before a `BasicEffect` is bound in other tests, never asserted as the reason for the throw.

## Positive Findings

- Correctly isolates one guard clause (IB-null) from the others (VB-null, effect-null, range validation) rather
  than conflating multiple checks in one test, matching the project's per-task test-file convention.
- Cleans up its own state (`SetVertexBuffer(nullptr)`) before returning, avoiding a dangling-pointer risk in
  `GraphicsDevice`.

## Final Assessment

A small, correctly-targeted regression test that verifies a real, correctly-ordered guard clause in
`GraphicsDevice::DrawIndexedPrimitives`/`DrawInstancedPrimitives`. The only gap is diagnostic granularity (F1),
not correctness.
