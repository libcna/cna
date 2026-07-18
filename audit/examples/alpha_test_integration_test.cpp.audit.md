# Audit: examples/alpha_test_integration_test.cpp

## Metadata

- Source file: `examples/alpha_test_integration_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard — `AlphaTestEffect` GPU alpha-cutout + pixel
  readback integration test (Task 122).
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_easygl_test(cna_test_alpha_test_integration examples/alpha_test_integration_test.cpp)` /
  `cna_register_backend_test(NAME EasyGL_AlphaTestEffect_AlphaCutout …)`,
  `cmake/Tests/EasyGLTests.cmake:223-227`). Registered **only** for EasyGL — no other
  backend's CMake file references this filename.
- XNA/FNA relevance: direct — exercises `AlphaTestEffect.AlphaFunction` /
  `AlphaTestEffect.ReferenceAlpha`'s actual per-pixel discard behavior via real rendering,
  not just property storage (complement to `alpha_test_effect_test.cpp` in this same batch).
- FNA reference: `StockEffects/AlphaTestEffect.cs`'s `OnApply()` (the `alphaTest` vec4
  encoding of comparison function + reference value + tolerance), consumed by the vendored
  `AlphaTestEffect.fx`'s `clip()` call.
- Related production code:
  `src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp` (`FillGpuDrawParams()` lines
  313-380, alpha-test vec4 derivation lines 339-379), `src/CNA/Internal/Backends/EasyGL/
  EasyGLGraphicsBackend.cpp` (fragment shader alpha-discard logic, e.g. lines 2679-2687).

## Purpose

Renders a full-screen quad split into two textured halves through a real `AlphaTestEffect`
(`CompareFunction::Greater`, `ReferenceAlpha=128`) against a 2×1 texture whose left texel is
opaque red (`alpha=255`) and right texel is fully transparent (`alpha=0`), then reads back one
pixel from each half via `GetBackBufferData` and checks the left renders the texture's red
(alpha test passes, fragment kept) while the right shows the green clear color through
(alpha test fails, fragment discarded). A real, GPU-executed proof that the alpha-test
pipeline's per-pixel `discard` actually fires — not achievable by a property test alone.

## Checklist Results

### API / XNA / FNA parity
`CompareFunction::Greater`/`ReferenceAlpha=128` map onto the correct FNA properties. The
scenario deliberately keeps `alpha=1.0` (never changed) so the only variable feeding the
alpha test is the *texture's own* per-texel alpha, isolating `AlphaFunction`/`ReferenceAlpha`
from the effect's separate `Alpha` (opacity) property — a clean, well-isolated test design.

### Behavioral correctness
Independently re-derived the alpha-test vec4 this scenario produces by hand from
`AlphaTestEffect::OnApply()`/`FillGpuDrawParams()` (`AlphaTestEffect.cpp` lines 361-364 /
347-350): `reference = 128/255 = 0.50196`, `threshold = 0.5/255 ≈ 0.00196`, and for
`CompareFunction::Greater`: `alphaTest.X = reference + threshold ≈ 0.50392`, `Z = -1`,
`W = 1`. Traced this straight into EasyGL's actual fragment-shader discard expression
(confirmed identical across every textured program variant, e.g. `EasyGLGraphicsBackend.cpp`
line 2684): `_at = (uAlphaTest.y>0.0) ? (...) : (FragColor.a < uAlphaTest.x ? uAlphaTest.z :
uAlphaTest.w); if (_at<0.0) discard;`. Since `uAlphaTest.y` (tolerance, only used for
Equal/NotEqual) is `0` here, the `else` branch applies: for the left pixel, `FragColor.a =
1.0` (opaque red texel × `diffuseColor.a=1.0`), `1.0 < 0.50392` is **false** → `_at = W = 1`
→ not discarded → renders the texture's red, matching `kExpectedRed`-style assertion (line
118: `leftPx.R≥200 && leftPx.G≤50`). For the right pixel, `FragColor.a = 0.0`,
`0.0 < 0.50392` is **true** → `_at = Z = -1` → discarded → the green clear color shows
through unmodified, matching line 119 (`rightPx.G≥200 && rightPx.R≤50`). **Both branches of
this test's own pass condition are independently confirmed correct against the actual current
shader source**, not merely plausible.

### Logic
The `RasterizerState::CullNone` workaround (line 106, with an explicit "Task 896 finding"
comment) was checked against the vertex winding of both half-quads: each is defined
`TL, BL, BR, TL, BR, TR` in NDC space. For the left half (`x: -1..0`), that ordering is
clockwise in screen space (Y-down after NDC-to-screen conversion) — back-facing under a
standard CCW-front-face convention, consistent with the comment's claim that this scene would
be invisible under the engine's real default `RasterizerState` without the explicit
`CullNone` override. This is a correct, deliberate compensating setting rather than a masked
bug — it does not affect what the alpha-test-specific assertions are checking.

### Memory/resource lifetime
`tex_` is a `Texture2D` member kept alive for the object's lifetime (constructed once in
`Initialize()`, read every frame from `Draw()` until `done_` gates further work) — no
lifetime issue; `AlphaTestEffect fx(device)` is function-local to `Draw()`, destroyed at the
end of that single invocated frame, well before device teardown.

### C++ correctness
`done_`/`result_` gating (checked at the top of `Draw()`) correctly ensures the GPU
readback/scene-construction logic runs exactly once even though `Draw()` may be invoked
multiple times before `Exit()` actually stops the game loop (a real, previously-seen defect
class in this style of single-shot pixel test elsewhere in this project — this file guards
against it correctly).

### Robustness
`leftPass`/`rightPass` use inequality bands (`≥200`/`≤50`) rather than exact equality —
appropriately tolerant of ordinary rasterization/blend rounding while still discriminating
the pass/fail cases by a wide margin (a `Greater`/discard failure would show far outside
either band, not just marginally).

### Testing
Strong, narrowly-scoped GPU integration test: exercises exactly one `AlphaTestEffect`
scenario (`Greater`/128) with both a passing and failing pixel in the same draw call, which
is sufficient to prove the discard mechanism works end-to-end but does not by itself cover
every `CompareFunction` value (see Missing or Weak Tests — appropriately deferred, since
`alpha_test_effect_test.cpp` already property-tests the setter for the other three values,
and this file's job is specifically to prove *one* real discard path renders correctly).

### Cross-file consistency
Consistent with `AlphaTestEffect.cpp`'s actual current implementation in every respect
checked; consistent with `alpha_test_effect_test.cpp`'s coverage of the same class (that file
covers property storage, this one covers the derived GPU behavior — no overlap, no gap
between the two beyond what's noted below).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings — this file's own pass/fail logic was independently
re-derived against the current production alpha-test formula and confirmed correct on both
branches.

### F1 — Only one `CompareFunction`/`ReferenceAlpha` combination is exercised via real rendering

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: whole file (single scenario: `Greater`, `ReferenceAlpha=128`)
- Why it matters: `AlphaTestEffect::OnApply()`'s `switch (alphaFunction_)` has 8 distinct
  branches (`Less`/`LessEqual`/`GreaterEqual`/`Greater`/`Equal`/`NotEqual`/`Never`/`Always`),
  each computing a different `(X,Z,W)` triple, and the `Equal`/`NotEqual` pair additionally
  exercises the otherwise-untouched `alphaTest.Y` (tolerance) branch of the fragment shader's
  `_at` expression (`uAlphaTest.y>0.0` branch) — none of that is proven correct by real
  rendering anywhere in this pair of files; only `Greater` is. A regression that swapped the
  `Z`/`W` sign convention for, say, `Less` or `Equal` specifically would not be caught by
  either file in this batch.
- FNA/XNA comparison: N/A (test-coverage observation; the `Greater` case itself was
  independently confirmed correct above).
- Suggested future action (not implemented by this audit): a parametrized sweep (or a small
  handful of additional scenarios) covering at least one of each sign-convention family
  (`Less*` vs. `Greater*` vs. `Equal`/`NotEqual` vs. `Always`/`Never`) with real pixel
  readback, mirroring the kind of "CompareFunction sweep" tests already present for other
  backends (e.g. `bgfx_alphatest_comparefunction_sweep_test.cpp`, confirmed to exist via
  `AUDIT_CROSS_CUTTING_FINDINGS.md`'s file listing) — this generic/EasyGL file currently has
  no equivalent.

## Cross-File Observations

- Complementary pair with `alpha_test_effect_test.cpp` (this batch): together they cover
  property storage + one real discard path; F1 notes the one gap neither closes.
- The `RasterizerState::CullNone` workaround comment ("Task 896 finding… missed by Task 896's
  own file audit" — used verbatim in the three `avatar_*_integration_test.cpp` files in this
  batch too) recurs across at least 4 files in this small sample, suggesting the underlying
  default-winding mismatch this workaround compensates for is a genuine, systemic property of
  this project's coordinate/winding conventions for NDC-space quads, not a one-off — worth
  the `xna-graphics`/backend audits confirming whether `RasterizerState`'s real default
  (`CullCounterClockwise` per FNA) is itself correctly implemented, or whether these tests are
  all compensating for a bug in the default.

## Missing or Weak Tests

- See F1 — no real-rendering coverage of `Less`/`LessEqual`/`GreaterEqual`/`Equal`/
  `NotEqual`/`Never`/`Always` in this generic/EasyGL-registered test; only `Greater` is
  proven via actual GPU discard.
- `VertexColorEnabled`/`FogEnabled` interaction with the alpha-test shader index selection
  (`AlphaTestEffect::OnApply()`'s `shaderIndex` computation, lines 301-310) has no rendering
  coverage in this file — both remain at their default (`false`) throughout.

## Positive Findings

- The alpha-test vec4 math and its consumption in EasyGL's actual fragment shader were both
  independently traced and hand-verified against this exact scenario's numbers — both the
  "keep" and "discard" outcomes check out precisely, not just approximately.
- Good test design: isolating `AlphaFunction`/`ReferenceAlpha` from `Alpha` (material opacity)
  by holding `Alpha=1.0` throughout and varying only the *texture's* per-texel alpha is the
  correct way to prove the alpha-test path specifically, rather than conflating it with
  ordinary blend-alpha behavior.
- Correctly documents (and works around) the `RasterizerState` winding gotcha rather than
  silently guessing at a fix.

## Final Assessment

Healthy. A precise, single-scenario GPU integration test whose pass/fail arithmetic was
independently confirmed correct against the current production alpha-test formula and shader;
its only real gap is breadth of `CompareFunction` coverage (F1), which is a reasonable,
explicitly-scoped omission rather than an authoring defect.
