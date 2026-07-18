# Audit: examples/easygl_alphatest_comparefunction_sweep_test.cpp

## Metadata

- Source file: `examples/easygl_alphatest_comparefunction_sweep_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `AlphaTestEffect` × EasyGL backend pixel integration test
- File type: standalone `Game`-subclass executable (not GTest), CTest-registered
- CMake registration: `cna_easygl_test(cna_test_easygl_alphatest_comparefunction_sweep …)` /
  `cna_register_backend_test(NAME EasyGL_AlphaTest_CompareFunctionSweep …)` —
  `cmake/Tests/EasyGLTests.cmake:1120-1122` — confirmed wired into the build, not orphaned.
- XNA/FNA relevance: direct — exercises `AlphaTestEffect.AlphaFunction`/`ReferenceAlpha`, matching
  FNA's `Graphics/Effect/StockEffects/AlphaTestEffect.cs` `OnApply()` switch statement.
- FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/AlphaTestEffect.cs`
  (the `AlphaTest` switch, lines 343-403).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp`
  (`OnApply()`/`FillGpuDrawParams()`), `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (the `uAlphaTest`/`discard` fragment-shader logic shared by all per-stride programs, e.g.
  `EnsureColoredTextured3DProgram()` around line 2749).
- Sibling files: complements `easygl_alphatest_modes_test.cpp` (Task 190, single boundary point
  only) and is itself reused by `easygl_alphatesteffect_golden_test.cpp` (Task 469, one sub-case).

## Purpose

Sweeps all 8 `CompareFunction` values against 3 alpha values (`64/255` below, `128/255` at,
`192/255` above a fixed `reference=128`) — 24 assertions total — to fully characterize each
function's pass/fail *region*, not just the single boundary point Task 190's
`easygl_alphatest_modes_test.cpp` tests. `kCases[]` (lines 62-71) hard-codes the expected
below/at/above triple per function, matching the file's own header-comment table (lines 16-23).

## Executive Verdict

**Healthy.** Independently re-derived the entire 24-case expected table directly from FNA's
`AlphaTestEffect.cs` `OnApply()` switch and from EasyGL's actual fragment-shader discard logic —
every one of the 24 expectations is provably correct (see Detailed Findings F1 for the full
derivation), and the shader code that implements it is unchanged from what the test exercises.
One real, evidence-based inconsistency (F2, a missing blank-frame-retry guard present in three
sibling test files but absent here) is flagged as a latent flakiness risk, not a correctness bug.

## Checklist Results

### API / XNA / FNA parity
Exercises `AlphaTestEffect::setAlphaFunctionProperty`/`setReferenceAlphaProperty`/`setAlphaProperty`
(all present, matching FNA's `AlphaFunction`/`ReferenceAlpha`/`Alpha` properties — verified against
`include/.../AlphaTestEffect.hpp` lines 208-234). `CompareFunction` enum use (`Always`, `Never`,
`Less`, `LessEqual`, `Equal`, `NotEqual`, `GreaterEqual`, `Greater`) covers the full FNA enum with
no gaps.

### Behavioral correctness
Re-derived all 24 cells of `kCases[]` from `AlphaTestEffect.cpp`'s `OnApply()`/`FillGpuDrawParams()`
(lines 243-298 / 339-379) and EasyGL's shared discard formula (`EasyGLGraphicsBackend.cpp:2749`,
`float _at=(uAlphaTest.y>0.0)?(...):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w); if(_at<0.0)discard;`).
Worked through all 8 functions by hand:
- `Less`: `X=ref-threshold=127.5/255`, `Z=1,W=-1` → drawn iff `a<127.5/255` → below=drawn,
  at=discard, above=discard. Matches `kCases[2]` exactly.
- `LessEqual`: `X=ref+threshold=128.5/255` → drawn iff `a<128.5/255` → below=drawn, at=drawn,
  above=discard. Matches.
- `Equal`/`NotEqual` (eqNe branch, `Y=threshold`): `Equal` drawn iff `|a-ref|<threshold` → only
  `at` (exact 128/255) passes; `NotEqual` is the exact complement. Both match.
- `GreaterEqual`/`Greater`: mirror `Less`/`LessEqual` with `Z=-1,W=1` (drawn iff `a>=x`/`a>x`).
  Both match.
- `Never`/`Always`: unconditional discard/draw regardless of `a`. Both match.
- All 8 rows independently reproduce the file's own header-comment table (lines 16-23) and the
  `docs/alphatesteffect-support.md` Phase 43 write-up ("24/24 PASS, zero bugs — EasyGL's comparison
  logic was already fully correct").

`runOne()`'s pass/fail proxy (`got.getRProperty() > 50`, line 101) is sound given the fixture: the
texture is opaque white and `BlendState::Opaque` is active, so a drawn pixel's R channel equals
`alpha*255` for whichever of the three test alphas (64, 128, 192) was used — all three are `>50` —
while a discarded pixel leaves the black clear (`R=0`, `<=50`). The threshold cleanly separates
"drawn" from "discarded" for this fixture; it would not generalize to an alpha value `<=50/255`,
but none of the three fixed test values ever falls there.

### Logic
`RasterizerState::CullNone` is applied before every draw (line 93, "Task 896 finding") — consistent
with the sibling files in this shard, a documented, deliberate workaround for the quad's winding
being back-facing under CNA's real default `RasterizerState`, not a workaround for a bug in the
effect under test.

### C++ correctness
`FuncCase`/`kCases` are plain aggregates with no lifetime concerns; `label[128]` (line 140) is
sized generously for the longest possible `"%s: alpha=xxx/255 (xxx reference)"` format string — no
overflow risk found (`std::snprintf` is also bounds-safe regardless).

### Performance
Constructs a fresh `AlphaTestEffect fx(dev)` (line 84) inside `runOne()`, called 24 times per test
run — negligible for a one-shot CTest executable; not a hot path.

### Testing
This *is* a test file — see Behavioral correctness above for the semantic verification. It
subsumes Task 190's coverage for the boundary case but Task 190's file remains as a still-useful,
narrower regression check per the sibling-file docs.

## Detailed Findings

### F1 — (informational, not a defect) Full 24-case table independently re-verified against FNA + EasyGL shader source

- Severity: INFO
- Confidence: HIGH
- Category: correctness (verification note)
- Location/symbol: `kCases[]` (lines 62-71), `AlphaTestEffect.cpp::OnApply()`/`FillGpuDrawParams()`,
  `EasyGLGraphicsBackend.cpp:2749`
- Evidence: see Behavioral correctness section above for the full derivation.
- Why it matters: recorded so a future re-audit doesn't have to redo this derivation from scratch;
  no discrepancy was found between the test's expectations, FNA's reference switch, and the actual
  shader discard logic currently in the tree.

### F2 — No blank-frame-retry guard, unlike three newer sibling tests in the same effect family

- Severity: LOW
- Confidence: MEDIUM
- Category: testing / maintainability (potential flakiness)
- Location/symbol: `runOne()` (lines 79-102) — single `Clear`+`Draw`+`GetBackBufferData` per case,
  no retry loop
- Evidence: the three newer sibling tests in this exact shard —
  `easygl_alphatest_fog_test.cpp::renderAtZ()` (lines 143-155),
  `easygl_alphatest_null_texture_test.cpp::renderWith()` (lines 119-131), and
  `easygl_alphatest_vertexcolor_diffuse_test.cpp::renderWith()` (lines 133-145) — all wrap their
  `Clear`+`Draw`+`GetBackBufferData` sequence in a `for (i < 20)` loop that re-clears and re-draws
  until a non-black pixel is observed, each commented `// skip blank/black frames`. This file (an
  older task, 373, predating those) has no such guard — a single `Clear`/`Draw`/`GetBackBufferData`
  per one of its 24 cases.
- Why it matters: if the underlying "first read after a fresh Clear/Draw can occasionally observe a
  not-yet-settled frame" condition the three newer tests were written to guard against is real (the
  guard was clearly added deliberately, not incidentally, across three separate later tasks), this
  file has no defense against it and could intermittently misclassify a `drawn` case as
  `discarded` (both read as `R<=50` only in the all-black no-guard failure mode, which would
  register as a spurious `[FAIL]`, not a silent pass) — a source of rare CI flakiness rather than a
  logic bug. Not confirmed to actually manifest in this file (no failing CI run found in this
  audit), hence `MEDIUM` confidence rather than `HIGH`.
- FNA/XNA comparison: N/A (CNA test-infrastructure concern, not an XNA behavior question).
- Related files: the three sibling files named above; also affects
  `easygl_alphatest_modes_test.cpp` (see that file's own report for the same finding).
- Suggested future action (not implemented by this audit): if this file is touched again, consider
  adopting the same retry-loop pattern for consistency, or confirming (e.g. via a stress-run) that
  the older tests are genuinely immune to whatever prompted the newer guard.

## Cross-File Observations

- `kCases[]`'s table is a strict superset of `easygl_alphatest_modes_test.cpp`'s single-point
  coverage (both files independently encode the same `Equal`/`NotEqual`/etc. expected outcomes at
  `alpha==reference`) — a deliberate, documented relationship (this file's own header comment lines
  6-10), not accidental duplication.
- `easygl_alphatesteffect_golden_test.cpp` reuses this file's `Greater`/`above` sub-case verbatim
  (see that file's own header comment) — worth keeping the two numerically in sync if either is
  ever revised.

## Missing or Weak Tests

None specific to this file beyond F2 above — the 24-case sweep is already a strong, non-trivial
characterization of the compare-function boundary behavior.

## Positive Findings

- The 3-point-per-function sweep design is genuinely more rigorous than a single boundary check:
  it independently confirms which *side* of the boundary passes for every inequality function, not
  just that the boundary discriminates something.
- Header comment's own 8-row expected table (lines 16-23) is accurate and was independently
  re-derived and confirmed correct against both the FNA reference and the live EasyGL shader source
  during this audit.

## Final Assessment

A correct, well-designed sweep test whose 24 expectations all check out against both the FNA
reference and the current EasyGL shader implementation. The only actionable note is a minor,
unconfirmed test-robustness gap (F2) relative to newer sibling tests in the same family.
