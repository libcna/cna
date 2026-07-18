# Audit: examples/bgfx_occlusionquery_pixelcount_test.cpp

## Metadata

- Source file: `examples/bgfx_occlusionquery_pixelcount_test.cpp` (195 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `OcclusionQuery.PixelCount()` visible-vs-occluded
  discrimination test (Tasks 814/815, combined into one file since both scenarios share the same
  fixture)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_occlusionquery_pixelcount ...)` /
  `cna_register_backend_test(NAME Bgfx_OcclusionQuery_PixelCount ...)`,
  `cmake/Tests/BgfxTests.cmake:862-...`).
- XNA/FNA relevance: direct — `OcclusionQuery.PixelCount`/`IsComplete`, `DepthStencilState`
  depth-test/depth-write behavior.
- FNA reference: `src/Graphics/OcclusionQuery.cs`, `src/Graphics/DepthStencilState.cs`.
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`BgfxOcclusionQueryBackend::PixelCount()`, lines 392-398; `SubmitViewProgram()`).

## Purpose

Combines Task 814 (a fully-visible quad should report a positive `PixelCount()`) and Task 815
(a fully-occluded quad should report zero/lower) into one file, since both share the same
red-target/blue-occluder fixture. The file's own header is candid about a second,
**independently-found** sandbox limitation beyond Task 448's own (`bgfx_occlusion_query_test.cpp`,
same batch): a dedicated scratch probe found `PixelCount()` returns the **identical** value (`-1`)
for a genuinely-fully-visible quad and the exact same quad fully depth-occluded by a nearer opaque
quad, despite `IsComplete()` reporting `true` in both cases — i.e. `PixelCount()`'s actual numeric
value does not discriminate visible from occluded geometry in this sandbox's software Mesa GL 2.1
(llvmpipe) renderer. What the file **does** assert as real pass/fail is the underlying 3D
rendering/depth-test behavior each scenario depends on: Scenario A's quad genuinely renders (Red
at centre), and Scenario B's nearer occluder genuinely depth-occludes the farther target (the
occluder's own Blue still shows at centre, proving true depth rejection, not just draw-then-cover
coincidence). `IsComplete()`/`PixelCount()` are reported informational-only.

## Executive Verdict

**Healthy** — the two real, asserted checks (visible-quad renders, occluder depth-hides the
target) are genuine and correctly discriminate their intended failure modes; the file is
transparent that `PixelCount()`'s numeric value itself cannot be used as a pass/fail signal here,
and this audit's independent reading of `BgfxOcclusionQueryBackend::PixelCount()` confirms the
implementation itself forwards `bgfx::getResult()` correctly — the ceiling is the software
rasterizer's own occlusion-query result reporting, not a CNA defect.

## Checklist Results

### API / XNA / FNA parity
`OcclusionQuery`, `getIsCompleteProperty()`, `getPixelCountProperty()` (lines 132-133) match FNA's
surface. `DepthStencilState` with `DepthBufferEnable=true`/`DepthBufferWriteEnable=true` (lines
108-111) is real, standard XNA depth-test configuration, correctly applied before either scenario
draws.

### Behavioral correctness
Confirmed `BgfxOcclusionQueryBackend::PixelCount()` (`BgfxGraphicsBackend.cpp` lines 392-398):
```cpp
int BgfxOcclusionQueryBackend::PixelCount() const
{
    if (!bgfx::isValid(handle)) return 0;
    int32_t result = 0;
    auto r = bgfx::getResult(handle, &result);
    if (r == bgfx::OcclusionQueryResult::Visible) return result;
    return 0;
}
```
correctly forwards `bgfx::getResult()`'s own reported pixel count when the result is `Visible`,
and returns 0 otherwise — a faithful, correct wrapper. The file's own claim that the *numeric*
discrimination failure is a sandbox/renderer limitation rather than a wrapper bug is therefore
independently corroborated: there is no CNA-side logic between `bgfx::getResult()` and the value
this test observes that could be silently substituting a wrong constant.
`RunScenario`'s depth-occlusion setup (lines 94-141): Scenario B draws a nearer Blue quad
(`z=0.1f`) with no query attached, **then** begins the query around the farther Red quad
(`z=0.9f`) — with real depth test/write enabled, this exercises genuine GPU depth rejection of
the occluded quad, not merely a "drawn but then covered" illusion, and the assertion
(`colourMatch(occluded.centre, kBlue)`, line 164) specifically checks that the **occluder's own
colour** persists at centre — the correct assertion to prove true occlusion (if the target were
incorrectly still visible/blended, centre would show a Red/Blue mix or pure Red, not pure Blue).

### Logic
`RunScenario`'s `while(true)` polling loop (lines 102-138) redraws every frame (both quads,
matching Task-406's per-frame-touch requirement) and polls `IsComplete()`/`PixelCount()` up to
`kMaxPollFrames=30`, breaking on completion or the frame cap — consistent with the sibling
`bgfx_occlusion_query_test.cpp`'s same-batch polling design.

### C++ correctness
`ScenarioResult` (lines 90) is returned by value from `RunScenario`, and `query` (a
`unique_ptr<OcclusionQuery>`) is scoped to the function and destroyed at each call's end —- no
dangling reference into the next scenario's independent `RunScenario(dev, /*occluded=*/true)`
call (line 163), which correctly constructs its own fresh query rather than reusing the visible
scenario's.

### Robustness
Scenario ordering (occluder drawn first, all subsequent redraws include the occluder too, lines
114-127) correctly maintains the same occlusion relationship across every retry-loop iteration —
no risk of the "informational" `PixelCount()` accidentally flipping to a discriminating value on
a later iteration due to inconsistent redraw state.

### Testing
Both counted assertions (visible-quad renders Red, occluder's Blue persists at centre proving
real depth rejection) are genuine, spatially-specific, and would fail under their respective real
regressions (e.g. a broken depth test would let the farther Red quad show through at centre in
Scenario B, correctly failing `colourMatch(occluded.centre, kBlue)`). `IsComplete()`/
`PixelCount()` are correctly demoted to informational printouts rather than asserted, matching the
documented sandbox limitation.

## Detailed Findings

None — no correctness or test-validity defects found. The reduced assertion scope for
`PixelCount()` is a verified-accurate reflection of a real sandbox/renderer limitation
(independently confirmed via the production `PixelCount()` wrapper's correctness), not an
unexamined weakness.

## Cross-File Observations

- This file's `PixelCount()`-discrimination limitation and `bgfx_occlusion_query_test.cpp`'s
  `IsComplete()`-discrimination limitation (same batch) are two independently-found instances of
  the same underlying sandbox ceiling (this software GL implementation's occlusion-query results
  don't vary meaningfully with actual visibility) — consistent, mutually corroborating findings
  rather than one file copying an assumption from the other without re-verifying it.
- Both occlusion files correctly avoid the failure mode of asserting a "close enough" but actually
  wrong numeric constant (the kind of issue found elsewhere in this project's EasyGL batch, e.g.
  `easygl_basiceffect_specular_test.cpp`'s stale check-b constant) — here, the response to "the
  real signal can't be trusted in this sandbox" is to stop asserting on it, not to assert a
  plausible-looking wrong number.

## Missing or Weak Tests

The genuine `PixelCount()` visible-vs-occluded numeric discrimination remains unverified in any
sandbox-runnable environment for Bgfx (shared gap with `bgfx_occlusion_query_test.cpp`, same
batch) — would need a real GPU-backed environment to close, per the file's own header.

## Positive Findings

- Combining Tasks 814/815 into one file sharing a fixture is a sensible scope decision (avoids
  duplicating the occluder/target quad setup across two near-identical files) without weakening
  either scenario's individual assertion.
- The depth-occlusion assertion technique (check the *occluder's own* colour persists, not just
  "target isn't visible") is the more rigorous of two plausible test designs — it positively
  proves genuine occlusion rather than merely the absence of the target colour, which could
  otherwise be satisfied by an unrelated rendering failure (e.g. the target simply failing to draw
  at all for a reason unrelated to depth testing).

## Final Assessment

A well-designed, honestly-scoped pair of occlusion scenarios whose real, asserted pixel checks
(visible-quad renders, occluder genuinely depth-hides the target) are sound and independently
verified against the current `BgfxOcclusionQueryBackend::PixelCount()` implementation. No defects
found; the informational-only treatment of `PixelCount()`'s numeric value is a correctly-earned
concession to a real, corroborated sandbox limitation.
