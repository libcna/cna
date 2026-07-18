# Audit: examples/bgfx_depthstencilstate_compare_function_test.cpp

## Metadata

- Source file: `examples/bgfx_depthstencilstate_compare_function_test.cpp` (175 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `DepthStencilState.DepthBufferFunction` (all 8
  `CompareFunction` values) pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_depthstencilstate_compare_function …)` /
  `cna_register_backend_test(NAME Bgfx_DepthStencilState_CompareFunction …)`,
  `cmake/Tests/BgfxTests.cmake:596-598`).
- XNA/FNA relevance: direct — `Microsoft.Xna.Framework.Graphics.CompareFunction`,
  `DepthStencilState.DepthBufferFunction`.
- FNA reference: `src/Graphics/CompareFunction.cs` (8-value enum: `Always, Never, Less, LessEqual,
  Equal, GreaterEqual, Greater, NotEqual`).
- Related production code: `include/Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp`,
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`ApplyDepthStencilState`,
  lines 1674-1721, the `depthFunc` switch mapping XNA ordinals 1-7 to
  `BGFX_STATE_DEPTH_TEST_*`).

## Purpose

Draws a reference quad A (red) at depth 0.5 with `DepthStencilState::Default` (writes depth), then
a quad B (green) at a per-check depth chosen to give an unambiguous outcome, using a custom
`DepthStencilState` whose `DepthBufferFunction` is the value under test. One `RunCheck()` pass per
`CompareFunction` value (8 total), each in its own cleared/rendered frame, sampling only the
viewport centre — explicitly restructured (per the file's own header comment) away from the
"multiple spatially-separate columns in one frame" technique used by
`examples/easygl_depthstencilstate_compare_function_test.cpp` (Task 314) because Bgfx's
`GetBackBufferData` only reliably reflects the first read per rendered frame.

## Executive Verdict

**Healthy** — all 8 `CompareFunction` checks were independently re-derived by this audit against
FNA's own `CompareFunction.cs` semantics ("passes when the *new* pixel value is `[op]` the
*current* pixel value") and all match the file's expected outcomes exactly; no test-authoring or
production defect found.

## Checklist Results

### API / XNA / FNA parity
`CompareFunction::{Always,Never,Less,LessEqual,Equal,GreaterEqual,Greater,NotEqual}` (lines 130-137)
is a 1:1, same-ordinal match against
`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/CompareFunction.cs` (confirmed by direct
read of both files) — all 8 values are exercised, unlike the EasyGL/Vulkan predecessor this file's
own comment says only covered 5 (`Always/Never/Less/LessEqual/Greater`, missing
`Equal/GreaterEqual/NotEqual`); this is a genuine coverage improvement, not just a restructuring.
`setDepthBufferFunctionProperty`/`setDepthBufferWriteEnableProperty`/`setDepthBufferEnableProperty`
(lines 75-77) match `DepthStencilState.hpp` signatures exactly.

### Behavioral correctness
Re-derived all 8 outcomes by hand against FNA's stated semantics (A always at depth 0.5, B at the
check's `bDepth`):
- `Always` (B@0.9, worse): ignores the comparison → passes → GREEN. Matches.
- `Never` (B@0.1, better): unconditionally rejects → RED (A retained). Matches.
- `Less` (B@0.3 < 0.5): `0.3<0.5`=true → GREEN. Matches.
- `LessEqual` (B@0.5 == 0.5): `0.5<=0.5`=true → GREEN — this is the one case that actually
  discriminates `LessEqual` from strict `Less` (a `Less`-only implementation would reject it).
  Matches.
- `Equal` (B@0.5==0.5): true → GREEN. Matches.
- `GreaterEqual` (B@0.5==0.5): `0.5>=0.5`=true → GREEN. Matches.
- `Greater` (B@0.7>0.5): true → GREEN — opposite depth direction from `Less`, catching a possible
  `Less`/`Greater` swap bug. Matches.
- `NotEqual` (B@0.3!=0.5): true → GREEN. Matches.
All 8 derivations are internally consistent with the single stored-depth model (A's depth 0.5 is the
"current" value each `CompareFunction` is tested against), and every check that could expose a
common off-by-one/direction bug (`LessEqual` vs `Less`, `Greater` vs `Less` swap) is present.

### Logic
`RunCheck()` (lines 88-118) issues a fresh `Clear()` + fresh `BasicEffect` + `Apply()` each of up to
20 retry iterations, breaking on the first non-`(0,0,0)` centre read — a correct application of the
project's established Bgfx retry idiom (Task 406) since the background clear here is pure black
`(0,0,0,255)` and both A (red) and B (green outcomes) are non-zero, so the break condition can never
misfire on a legitimate settled frame.

### C++ correctness
`RunCheck` returns `Color` by value; no dangling references, no raw-pointer lifetime issues. The
`Check` array (lines 128-138) is a local aggregate of `const char*`/enum/`float`/`bool` — trivially
safe.

### Robustness
Pass/fail thresholds (`sawGreen`: G≥200,R≤60,B≤60; `sawRed`: R≥200,G≤60,B≤60, lines 144-145) have a
140-unit dead zone between the two classifications and an even wider gap from the `(0,0,0)`
retry-sentinel, so no plausible antialiasing/rounding noise at the exact centre of a full-screen
quad could misclassify a result.

### Testing
This file is itself a test; there is no "production file under test" narrower than
`DepthStencilState`/`BgfxGraphicsBackend::ApplyDepthStencilState` — both are otherwise covered by
sibling stencil-focused tests in this same shard. Coverage here is specifically and only the depth
`CompareFunction` mapping, and it is now complete (8/8 XNA values) where the ported EasyGL ancestor
was not.

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM findings.

## Cross-File Observations

- Shares the `RasterizerState::CullNone` workaround (line 68, "Task 364/896 finding") with every
  other file in this batch — independently confirmed real: `RasterizerState`'s constructor default
  is `CullMode::CullCounterClockwiseFace` (`RasterizerState.cpp:11`), which matches FNA's actual XNA
  default; git history (`736d3b95`, "fix(Task 364)") confirms this was discovered as a genuine
  cross-backend behavioral difference (Bgfx is the only one of the 3 backends whose default
  actually culls per real XNA semantics), tracked onward as Task 884 in that same commit message.
- Shares the `GetBackBufferData` first-read-only Bgfx limitation (Task 406) with every other file in
  this batch; verified against `f18bfdc0`'s actual commit body, which does describe exactly this
  finding (the commit's one-line subject names a different feature — `SkinnedEffect` — but its body
  documents the `GetBackBufferData` multi-read issue as a discovered-along-the-way test-harness
  pitfall), so the citation is accurate, not fabricated.

## Missing or Weak Tests

None identified for this specific file's stated scope (all 8 `CompareFunction` values, one
discriminating depth per value).

## Positive Findings

- Genuinely closes a real coverage gap versus the EasyGL/Vulkan ancestor (5/8 → 8/8 `CompareFunction`
  values), and the 3 newly-added checks (`Equal`, `GreaterEqual`, `NotEqual`) are not redundant with
  the existing 5 — each tests a materially different comparison outcome.
- The `LessEqual`/`Equal`/`GreaterEqual` triple all deliberately reuse `bDepth=0.5` (equal to A's
  depth) specifically to prove the inclusive-equality behavior of each, which a naive
  strict-inequality implementation would fail.

## Final Assessment

A clean, correctly-derived, and genuinely more complete pixel test than its predecessor. No defects
found in the test or in the underlying `DepthStencilState`/Bgfx depth-comparison mapping it
exercises.
