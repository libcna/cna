# Audit: examples/easygl_alphatest_modes_test.cpp

## Metadata

- Source file: `examples/easygl_alphatest_modes_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `AlphaTestEffect` × EasyGL backend pixel integration test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_alphatest_modes …)` /
  `cna_register_backend_test(NAME EasyGL_AlphaTestModes …)`, `cmake/Tests/EasyGLTests.cmake:1114-1116`).
- XNA/FNA relevance: direct — exercises `AlphaTestEffect.AlphaFunction` at the single
  `pixel.a == reference` boundary point, for all 8 `CompareFunction` values.
- FNA reference: `AlphaTestEffect.cs` `OnApply()`'s `AlphaTest` switch (lines 343-403).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp`
  (`FillGpuDrawParams()`), `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (shared
  `uAlphaTest`/`discard` logic).
- Sibling: superseded in *scope* (not removed) by
  `easygl_alphatest_comparefunction_sweep_test.cpp` (Task 373), which extends this single boundary
  point into a 3-value sweep; this file (Task 190) is the original, narrower test, kept as a
  regression check per this shard's own convention.

## Purpose

Task 190's original EasyGL `AlphaTestEffect` pixel test: draws a full-screen quad with texture
alpha fixed at exactly `128/255` against `ReferenceAlpha=128`, for all 8 `CompareFunction` values,
and checks drawn-vs-discarded against the file's own expected table (lines 50-59).

## Executive Verdict

**Healthy.** All 8 expected outcomes were independently re-derived from FNA's `AlphaTestEffect.cs`
switch and from EasyGL's current shader discard formula and match the file's table exactly (this
is the single `a==reference` column of the larger table verified in the sibling sweep test's own
report). One test-robustness gap shared with the sweep test (missing blank-frame retry guard,
present in three newer sibling files) is flagged as F1, LOW severity.

## Checklist Results

### API / XNA / FNA parity
Exercises `setAlphaFunctionProperty` across the full 8-value `CompareFunction` enum with no gaps
(`Always`, `Never`, `Less`, `LessEqual`, `Equal`, `NotEqual`, `GreaterEqual`, `Greater` — lines
50-59), matching FNA's enum exactly.

### Behavioral correctness
Table re-derivation (all at `a=reference=128/255` exactly, `threshold=0.5/255`):
- `Less`: `a<ref-threshold`? `128<127.5` → false → discard. Table says discard. Matches.
- `LessEqual`: `a<ref+threshold`? `128<128.5` → true → drawn. Table says drawn. Matches.
- `Equal`: `|a-ref|<threshold`? `0<0.5/255` → true → drawn. Matches.
- `NotEqual`: same predicate, inverted branch → discard. Matches.
- `GreaterEqual`: `a<ref-threshold`? false → (branch inverted vs Less: `Z=-1,W=1`) → drawn. Matches.
- `Greater`: `a<ref+threshold`? true → discard (inverted branch). Matches.
- `Never`/`Always`: unconditional discard/drawn regardless of `a`. Matches.
- All 8 rows match the file's own table (lines 7-15) and are the exact same math the sibling sweep
  test (Task 373) independently re-verifies for 3 alpha points instead of 1 — no discrepancy found
  between the two files' shared boundary case.

`isDrawn = got.getRProperty() > 50` (line 119): sound for this fixture — a drawn pixel has
`R=128` (white texture × alpha=128/255 diffuse, `BlendState::Opaque`), a discarded pixel leaves the
black clear (`R=0`); `50` cleanly separates the two for this test's single fixed alpha value.

### Logic
`RasterizerState::CullNone` applied per draw (line 112, "Task 896 finding"), same documented
workaround as the rest of this shard.

### Testing
Confirms real drawn/discarded pixel behavior per `CompareFunction`, not just "doesn't crash" —
meets the anti-boilerplate bar. Its `done_`/early-return guard in `Draw()` (lines 75-76) correctly
ensures the 8-case loop and `Exit()` run exactly once even if `Draw()` were somehow invoked again
before the game loop unwinds.

## Detailed Findings

### F1 — No blank-frame-retry guard, unlike three newer sibling tests in the same effect family

- Severity: LOW
- Confidence: MEDIUM
- Category: testing / maintainability (potential flakiness)
- Location/symbol: `Draw()`'s per-case loop (lines 99-129) — single `Clear`+`Apply`+`Draw`+
  `GetBackBufferData` per `CompareFunction` case, no retry
- Evidence: identical pattern and identical finding as
  `easygl_alphatest_comparefunction_sweep_test.cpp`'s own F2 — see that file's report for the full
  comparison against the three newer sibling tests (`_fog_test.cpp`, `_null_texture_test.cpp`,
  `_vertexcolor_diffuse_test.cpp`) that all added an up-to-20-iteration "skip blank/black frames"
  retry loop this file (the oldest in the family, Task 190) lacks.
- Why it matters: same reasoning as the sibling finding — a spurious `[FAIL]` under a rare
  GPU-readback timing condition, not a logic defect. `MEDIUM` confidence since no actual failing
  run was observed in this audit.
- FNA/XNA comparison: N/A.
- Related files: `easygl_alphatest_comparefunction_sweep_test.cpp` (same finding, its own report),
  the three sibling files with the retry guard.
- Suggested future action (not implemented by this audit): adopt the same retry-loop convention if
  this file is next touched, for shard-wide consistency.

## Cross-File Observations

- This file's 8-case table is exactly the `a==reference` column of
  `easygl_alphatest_comparefunction_sweep_test.cpp`'s 24-case table — both independently re-verified
  in this audit and found mutually consistent, with no drift between the two despite being
  maintained as separate files across two tasks (190 and 373).

## Missing or Weak Tests

None beyond F1 — coverage of this exact boundary point is now doubly redundant with the sweep test,
which is an intentional, documented redundancy (see this shard's docs), not an oversight.

## Positive Findings

- Clean, minimal single-purpose test; the `done_` re-entrancy guard is a small but correct defensive
  detail not present in some of this project's other single-shot `Game`-subclass tests.

## Final Assessment

A correct, still-useful narrower predecessor to the sweep test, with matching expected values
verified independently against both FNA and the live EasyGL shader. Only actionable item is the
shared, unconfirmed flakiness-robustness gap (F1).
