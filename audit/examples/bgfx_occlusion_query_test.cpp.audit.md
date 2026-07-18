# Audit: examples/bgfx_occlusion_query_test.cpp

## Metadata

- Source file: `examples/bgfx_occlusion_query_test.cpp` (181 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `OcclusionQuery` Begin()/End()-wiring smoke test
  (Task 448)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_occlusionquery ...)`; multi-frame `Draw()`-driven state machine,
  not a single-shot test).
- XNA/FNA relevance: direct — `OcclusionQuery.Begin`/`End`/`IsComplete`/`PixelCount`.
- FNA reference: `src/Graphics/OcclusionQuery.cs` (thin wrapper over
  `FNA3D_QueryBegin`/`FNA3D_QueryEnd`/`FNA3D_QueryComplete`/`FNA3D_QueryPixelCount`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/OcclusionQuery.cpp`,
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`BgfxOcclusionQueryBackend::Begin()`/`End()`, lines 376-384; `SubmitViewProgram()`, lines
  406-412).

## Purpose

Per the file's own header, Task 441's prior audit found `BgfxOcclusionQueryBackend::Begin()`/
`End()` were literal empty no-ops and the created `bgfx::OcclusionQueryHandle` was never attached
to any draw call. Task 448 (this file) fixes that via bgfx's dedicated `submit(id, program,
occlusionQuery, ...)` overload, tracked through a new `activeOcclusionQuery_` member set by
`Begin()`/cleared by `End()`. The file's own header is unusually candid that the fix's
*discriminating power* (does `IsComplete()`/`PixelCount()` actually change behavior) could **not**
be established in this sandbox: sabotaging `Begin()`/`End()` back to no-ops produced identical
`IsComplete()`/`PixelCount()` results, because this sandbox's software Mesa GL 2.1 (llvmpipe)
`bgfx::getResult()` returns a non-`NoResult` value even for a never-submitted query handle. The
test therefore treats `IsComplete()`/`PixelCount()` as informational-only, and asserts real
pass/fail only on "no throw" and "normal 3D rendering still works with the new plumbing attached."

## Executive Verdict

**Healthy** — the file's own limitation disclosure was independently verified against the actual
production code (confirmed `SubmitViewProgram` genuinely uses bgfx's occlusion-query `submit()`
overload, and `Begin()`/`End()` genuinely set/clear `activeOcclusionQuery_`), and the test's
reduced, honest pass/fail bar (no-crash + rendering-still-works) accurately reflects what can
actually be proven in this environment, rather than overclaiming a discriminating occlusion-query
result the sandbox cannot produce.

## Checklist Results

### API / XNA / FNA parity
`OcclusionQuery(dev)`, `query_->Begin()`, `query_->End()`, `query_->getIsCompleteProperty()`,
`query_->getPixelCountProperty()` (lines 127-160) match FNA's `OcclusionQuery` surface (`Begin`,
`End`, `IsComplete`, `PixelCount`) exactly in name and shape (C# property → CNA
`getXProperty()` convention correctly applied).

### Behavioral correctness
Traced `BgfxOcclusionQueryBackend::Begin()`/`End()` (`BgfxGraphicsBackend.cpp` lines 376-384):
`Begin()` sets `owner_->activeOcclusionQuery_ = handle`, `End()` resets it to
`BGFX_INVALID_HANDLE` — matches the file's own description exactly. `SubmitViewProgram()` (lines
406-412):
```cpp
if (bgfx::isValid(activeOcclusionQuery_))
    bgfx::submit(currentViewId_, program, activeOcclusionQuery_);
else
    bgfx::submit(currentViewId_, program);
```
confirms every 3D draw submit site genuinely routes through the occlusion-aware `submit()`
overload while a query is active — the file's central production claim is accurate, not merely
asserted.

### Logic
`Draw()`'s frame-state-machine (lines 112-163): frame 0 wraps a single `DrawQuad` in
`Begin()`/`End()`; subsequent frames redraw the same quad plainly (bgfx needs each view touched
every frame for valid swapchain content, per the file's own comment, line 116-118) while polling
`IsComplete()`/`getPixelCountProperty()` up to `kMaxPollFrames=30`, then asserts once complete or
frame-capped.

### C++ correctness
`query_` is a `unique_ptr<OcclusionQuery>` created once at frame 0 and kept alive for the
remainder of `Draw()`'s polling frames (line 127) — no premature destruction while `Begin()`/
`End()` are still logically "in flight" from the backend's perspective.

### Robustness
The `try { query_->Begin(); DrawQuad(...); query_->End(); } catch (...) { threw_ = true; }`
wrapper (lines 128-137) correctly isolates whether the new Begin/End/submit wiring itself throws,
independent of the informational IsComplete/PixelCount readings taken later.

### Testing
Two real assertions: "Begin()/End() around an attached draw call do not throw" and "the quad
still renders Red at centre" (i.e. the new occlusion-aware submit plumbing does not break ordinary
3D rendering) — both genuine, both meaningfully falsifiable (a broken `submit()` overload call
that silently dropped the draw, or an exception from a malformed `bgfx::OcclusionQueryHandle`,
would fail these). `IsComplete()`/`PixelCount()` are printed but **not** asserted — an honest
reflection of the sandbox limitation the header documents, not a hidden weak assertion dressed up
as a real one (unlike some files elsewhere in this project's history that assert a "close enough"
constant against a known-wrong baseline).

## Detailed Findings

None — no correctness or test-validity defects found. The test's reduced assertion scope is a
correctly-documented, verified-accurate reflection of a genuine sandbox limitation, not an
unexamined weakness.

## Cross-File Observations

- This file, `bgfx_occlusionquery_pixelcount_test.cpp`, and
  `bgfx_occlusionquery_dispose_active_test.cpp` (same batch) together cover Begin/End wiring
  (this file), pixel-count/depth-occlusion discrimination (pixelcount file), and Dispose-while-
  active safety (dispose_active file) — a reasonably complete, non-overlapping three-way split of
  `OcclusionQuery`'s surface for Bgfx.
- The "software renderer can't discriminate occlusion results" limitation this file documents is
  corroborated independently by `bgfx_occlusionquery_pixelcount_test.cpp`'s own, separately-found
  identical limitation (`PixelCount()` returning the same value for genuinely-visible vs.
  genuinely-occluded geometry) — consistent, not a one-off excuse.

## Missing or Weak Tests

The genuine occlusion-discrimination question (does a hidden object actually report a lower/zero
`PixelCount()` than a visible one) remains unverified in *any* sandbox-runnable environment for
Bgfx, per both this file's and the pixelcount file's own disclosed findings — this is a real,
acknowledged gap in what can currently be proven about Bgfx's occlusion-query correctness, not a
defect in either test file specifically.

## Positive Findings

- Rare, exemplary case of a test file being explicit that it could not establish a fix's
  discriminating power, backing that claim with a documented sabotage-and-compare experiment, and
  narrowing its own assertions accordingly rather than asserting something it could not actually
  verify.

## Final Assessment

An honest, correctly-scoped smoke test whose real assertions (no throw, normal rendering
preserved) were independently confirmed to exercise genuinely-wired production code
(`SubmitViewProgram`'s occlusion-aware `submit()` overload). No defects found.
