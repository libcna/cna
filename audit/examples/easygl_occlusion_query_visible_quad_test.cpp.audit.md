# Audit: examples/easygl_occlusion_query_visible_quad_test.cpp

## Metadata

- Source file: `examples/easygl_occlusion_query_visible_quad_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `OcclusionQuery` correctness test
- File type: C++ example/integration-test executable (`OcclusionQueryVisibleQuadTest : Microsoft::Xna::Framework::Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::OcclusionQuery` (`OcclusionQuery.hpp`/`.cpp`),
  `CNA::Internal::Backends::EasyGLOcclusionQueryBackend` (`EasyGLGraphicsBackend.cpp` lines 381-428)
- XNA/FNA relevance: `OcclusionQuery` is a real XNA 4.0 type (`Microsoft.Xna.Framework.Graphics.OcclusionQuery`,
  `Begin`/`End`/`IsComplete`/`PixelCount`) — confirmed against `FNA/src/Graphics/OcclusionQuery.cs`.
- Main related tests: this file itself (Task 445); superseding, per its own header comment, the older
  `examples/occlusion_query_test.cpp` (Tasks 35/442-444) which only checked Begin/End/IsComplete/PixelCount
  "don't crash", not that a visible object is actually reported visible.

## Purpose

Draws a large, fully-screen-covering red quad wrapped in `OcclusionQuery::Begin()`/`End()`, then verifies three
independent things in the same frame: (1) the quad genuinely rendered red at the viewport centre (via
`GetBackBufferData`, a real pixel readback — not just "the draw call didn't throw"), (2) the query becomes
`IsComplete()` after a bounded retry loop, and (3) `PixelCount() > 0`. Placement under `examples/` with the
`easygl_` prefix and single-executable-per-test shape matches this shard's established convention.

## Executive Verdict

**Healthy.** This is a genuine correctness test, not a "compiles and doesn't crash" placebo — it independently
confirms both the query result and the underlying pixel data agree, and its documentation of the GLES3
`GL_ANY_SAMPLES_PASSED` 0/1 semantic (as opposed to real XNA/D3D's literal per-pixel count) is verified accurate
against both `OcclusionQuery.hpp`'s own Doxygen and the actual `EasyGLOcclusionQueryBackend::PixelCount()`
implementation. No HIGH/CRITICAL findings.

## Checklist Results

### API / XNA / FNA parity
`OcclusionQuery(device)`, `Begin()`, `End()`, `getIsCompleteProperty()`, `getPixelCountProperty()` all map correctly
to FNA's `OcclusionQuery(graphicsDevice)`/`Begin()`/`End()`/`IsComplete`/`PixelCount` (`FNA/src/Graphics/
OcclusionQuery.cs` lines 21-41, 80-88) under this project's established C# `Property` → `getXProperty()` convention.
One genuine, already-documented platform deviation (not a bug in this file, but worth recording precisely since it
affects how "PixelCount() > 0" must be interpreted): FNA's `PixelCount` returns `FNA3D_QueryPixelCount`, which on
desktop GL/D3D backends is typically a real sample tally (`GL_SAMPLES_PASSED`); `EasyGLOcclusionQueryBackend`
(GLES3-only) uses `GL_ANY_SAMPLES_PASSED` (confirmed at `EasyGLGraphicsBackend.cpp:398,404`,
`QueryTarget::AnySamplesPassed`), which is a boolean-like 0/1 result — `PixelCount()` (line 413-418) returns
`static_cast<int>(query_.result())` directly, so "PixelCount() > 0" here really means "any samples passed", not "N
pixels visible". The test's own header comment (lines 16-17) states this precisely and correctly, and
`OcclusionQuery.hpp`'s own Doxygen (line 41, "On OpenGL ES 3.0 this returns 0... or 1...") independently confirms
it — this is a genuine, correctly-documented XNA behavioral narrowing for the GLES3-based backend, not an
undocumented bug.

### Behavioral correctness
Verified the winding/culling comment (line 104-106, "NDC quad winding is CCW/back-facing under CNA's real default
RasterizerState — needs CullNone") is acted on correctly: `device.setRasterizerStateProperty(RasterizerState::CullNone)`
is set before the draw (line 106), and the quad's own vertex order `(-1,1)→(-1,-1)→(1,-1)→(1,1)` with indices
`{0,1,2,0,2,3}` is the same order used by every other NDC-quad test in this batch that also needs `CullNone`
(cross-checked against the PBR/particle/postprocess tests' quad-winding conventions) — internally consistent.
`OcclusionQuery(device)` (line 111) is constructed once per frame inside `Draw()`, matching FNA's own per-use
constructor-then-`Begin()`/`End()` pattern; no reuse-across-frames concern since this is a single-shot test.

### Logic
`sample()` (lambda, lines 127-132) performs a 1×1 `GetBackBufferData` readback whose real purpose (per the file's
own header, lines 12-14, and the inline comment at line 124-126) is to force the GL driver to flush/synchronize
pending commands so the occlusion query's result becomes available — verified this claim against
`EasyGLGraphicsBackend::ReadBackbuffer` (line 1502 onward), which does call `glReadPixels` (a genuinely
synchronizing GL call), supporting the stated mechanism. The retry loop (lines 137-141, up to 30 iterations) calls
`sample()` again purely for its synchronization side effect and discards the returned pixel — a slightly unusual
but intentional and correctly-commented pattern, not a bug.

### Memory/resource lifetime
`vb_`/`ib_` are `std::unique_ptr` members created once in `Initialize()`, matching the file's own single-frame
lifecycle; `OcclusionQuery query(device)` (line 111) is a plain stack object whose destructor runs normally at the
end of `Draw()` — no leak or dangling-pointer risk.

### Performance
The up-to-30-iteration retry loop (lines 137-141) has no sleep/backoff between `glReadPixels` calls — each
iteration is itself a blocking, driver-synchronizing call, so this is a busy-poll rather than a true spin-loop, but
it could still issue up to 30 readback round-trips in the worst case on a slow/loaded CI runner. `LOW` severity,
bounded by the loop count, no correctness risk — acceptable for a single-shot integration test.

### Robustness
`check()` (lines 64-68) records failures without aborting immediately, so all three assertions
(colour/`IsComplete`/`PixelCount`) are always evaluated and reported even if an earlier one fails — good diagnostic
design (a query that never completes still reports its own `PixelCount() > 0` failure separately, rather than the
test short-circuiting on the first failure and hiding the rest).

### Testing
This file is itself a test. Its three-pronged check (colour readback + query completion + query pixel count) is
exactly the kind of cross-validation the anti-boilerplate rule in this audit's own checklist looks for: it does
not merely assert "no exception was thrown", it independently confirms the query's *result* against ground-truth
pixel data from the same frame.

## Detailed Findings

No HIGH/CRITICAL findings. The one item worth flagging as context (not a defect) is recorded under "API / XNA /
FNA parity" above: `PixelCount()`'s GLES3 boolean-like semantic vs. real XNA/D3D's literal count, already
correctly documented in both this file and `OcclusionQuery.hpp`.

## Cross-File Observations

- Reuses `BasicEffect`+`VertexPositionColor` rather than a hand-rolled GLSL shader (unlike the other 7 files in
  this batch) — appropriate, since this test is about the occlusion-query API surface, not shader-conversion
  fidelity, so there is no reason to introduce a custom `.cnj`/GLSL pair here.
- The `RasterizerState::CullNone` requirement for NDC-space quads (this file's own Task 896 finding, line 104-106)
  recurs verbatim across every NDC/clip-space quad test in this batch (particle, PBR skips it since its quads are
  screen-space triangle lists built with an explicit winding already correct for the default cull state — worth a
  note for whichever shard audit catalogues winding conventions across this project's ~570 backend example tests).

## Missing or Weak Tests

- Only tests the "fully visible" half of occlusion-query correctness (a query on something visible reports
  visible). There is no companion test in this batch verifying the complementary case (something fully occluded
  reports `PixelCount() == 0`) — the file's own header only claims to fix the "positive" side of Task 441's finding;
  a `..._occluded_quad_test.cpp` sibling would close the loop, but is out of this file's own stated scope.

## Positive Findings

- Directly implements Task 441's own audit finding (that occlusion-query examples never verified real
  visible/occluded correctness, only "doesn't crash") — a good example of an audit finding being converted into a
  concrete regression test rather than left as a documentation note.
- Precisely and correctly documents a real, narrow XNA-behavioral deviation (GLES3's `GL_ANY_SAMPLES_PASSED`
  boolean vs. real XNA's literal pixel count) instead of silently treating `PixelCount()` as a literal count.

## Final Assessment

A well-targeted, genuinely discriminating correctness test for `OcclusionQuery` on the EasyGL backend. Its claims
about GL synchronization via `GetBackBufferData`/`glReadPixels` and about the GLES3 `GL_ANY_SAMPLES_PASSED`
boolean semantic were both independently verified against the actual backend implementation and check out.
