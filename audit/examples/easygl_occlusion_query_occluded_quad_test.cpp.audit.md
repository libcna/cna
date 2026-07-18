# Audit: examples/easygl_occlusion_query_occluded_quad_test.cpp

## Metadata

- Source file: `examples/easygl_occlusion_query_occluded_quad_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `OcclusionQuery` fully-occluded-quad pixel/query test
- File type: `Game`-derived executable, CTest-registered as
  `cna_test_easygl_occlusion_query_occluded_quad` / `EasyGL_OcclusionQuery_OccludedQuad`
  (`cmake/Tests/EasyGLTests.cmake:218-220`)
- XNA/FNA relevance: direct — `Microsoft::Xna::Framework::Graphics::OcclusionQuery`,
  `DepthStencilState`, `BasicEffect`
- Production sources cross-checked: `src/Microsoft/Xna/Framework/Graphics/OcclusionQuery.cpp`,
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EasyGLOcclusionQueryBackend`, lines 381-425), `src/Microsoft/Xna/Framework/Graphics/
  DepthStencilState.cpp` (`Default` preset)
- FNA reference: `Graphics/OcclusionQuery.cs` (`FNA3D_QueryPixelCount`/`FNA3D_QueryComplete`)

## Purpose

Draws a nearer, opaque Blue occluder quad (z=0.3) followed by a farther Red target quad (z=0.7)
wrapped in an `OcclusionQuery.Begin()`/`End()`, with `DepthStencilState::Default` (depth-write on,
`LessEqual` compare) active — since `0.7 <= 0.3` is false, every target-quad fragment should be
depth-rejected, and the query should report `PixelCount() <= 0`. A backbuffer readback additionally
confirms the occluder's own colour (Blue) is still showing, proving genuine occlusion rather than
"nothing was drawn at all."

## Executive Verdict

**Healthy for what this test specifically checks** (a fully-occluded quad reports zero passed
pixels), but this audit found a real, evidence-based XNA/FNA parity gap in the production
`OcclusionQuery` backend that this particular test cannot detect (because it happens not to matter
for a "reports zero" assertion) — flagged here as a cross-file finding for the underlying backend
rather than a defect in this test file's own logic.

## Checklist Results

### API / XNA / FNA parity
`OcclusionQuery(GraphicsDevice&)`, `.Begin()`, `.End()`, `.getIsCompleteProperty()`,
`.getPixelCountProperty()` all match FNA's `OcclusionQuery` surface (`IsComplete`, `PixelCount`,
`Begin()`, `End()`, constructor) exactly in shape (`OcclusionQuery.cpp:8-36`).

**Parity gap found in the backend** (see F1 below): FNA's `OcclusionQuery.PixelCount` forwards to
`FNA3D_QueryPixelCount`, whose real GL-backed implementations use `GL_SAMPLES_PASSED` — an actual
integer count of samples that passed the depth/stencil test, potentially in the thousands for a
large visible quad. `EasyGLOcclusionQueryBackend` instead issues `GL_ANY_SAMPLES_PASSED`
(`EasyGLGraphicsBackend.cpp:398, 404`, `QueryTarget::AnySamplesPassed`) and its
`PixelCount()` returns `static_cast<int>(query_.result())` — a GLES3 `GL_ANY_SAMPLES_PASSED` boolean
result register, which is only ever `0` or `1`, never a real per-pixel count (the backend's own
comment at line 416 acknowledges this: "GLES3 uses GL_ANY_SAMPLES_PASSED — result is 0 (none) or 1
(any)"). This means `OcclusionQuery.PixelCount` on EasyGL cannot report "quad occupies roughly 40,000
screen pixels" the way real XNA/FNA can — only "at least one sample passed" vs. "none passed."

### Behavioral correctness
For **this specific test** (fully occluded → expect the query to report no passed samples), `0` is
the correct result under *both* semantics (`GL_SAMPLES_PASSED` would also report exactly `0` for a
quad where every fragment fails the depth test) — so the parity gap identified above does not cause
this particular test to pass incorrectly or mask a real occlusion-detection bug; the assertion
`query.getPixelCountProperty() <= 0` (line 160) is satisfied identically under either GL query target.
`DepthStencilState::Default`'s actual field values were confirmed against
`DepthStencilState.cpp:10-13`: `depthBufferEnable_=true, depthBufferWriteEnable_=true,
depthBufferFunction_=CompareFunction::LessEqual` — exactly matching the test's own header-comment
description ("writes depth, DepthBufferFunction = LessEqual").

The backbuffer-colour check (`colourMatch(centre, kBlue)`, line 157) is the more discriminating half
of this test in practice: it distinguishes "the target quad was genuinely depth-rejected while the
occluder is visibly in front" from a degenerate case where, e.g., neither quad rendered at all (which
would also make `PixelCount() == 0` but would *not* show Blue at the centre pixel) — the header
comment's own reasoning for including this check (lines 16-19) is sound and independently verified
here.

### Logic
The completion-polling loop (lines 150-156, up to 30 attempts, each re-issuing a `GetBackBufferData`
call to force a GL sync point) is a reasonable, bounded way to wait for `IsComplete()` to become true
without an unbounded spin — matches the header comment's stated technique ("GetBackBufferData... both
forces the driver to flush pending work... and independently confirms genuine occlusion").

### C++ correctness
`colourMatch` casts `getRProperty()` etc. to `int` before subtracting (avoiding unsigned-underflow),
same pattern independently verified correct in the sibling `easygl_model_two_meshes_effects_test.cpp`
(same anonymous-namespace helper, duplicated per-file rather than shared — a minor, low-severity
duplication, not a correctness issue).

### Robustness
`check()`'s side-effect-based failure tracking (`result_ = 1` on any `false`, never reset to `0`) is
the same fail-sticky pattern used across this shard's hand-rolled (non-`PixelTestGame`) tests —
consistent, low-risk.

### Testing
`query.getPixelCountProperty() <= 0` (rather than `== 0`) is slightly over-defensive: nothing in
`EasyGLOcclusionQueryBackend::PixelCount()` (`static_cast<int>(query_.result())`, a GL boolean-style
result) can ever produce a negative value, so the `<=` comparison's "or lower" branch is dead in
practice for this backend — harmless, but worth noting as a minor style point (see F2).

## Detailed Findings

No CRITICAL/HIGH findings in this test file's own logic. One MEDIUM finding surfaced in the
production backend this test exercises.

### F1 — `OcclusionQuery.PixelCount` on EasyGL reports a 0/1 boolean, not a real sample count (FNA/XNA parity gap)

- Severity: MEDIUM
- Confidence: HIGH
- Category: API/XNA/FNA parity (production code, not this test file)
- Location/symbol: `EasyGLOcclusionQueryBackend::Begin`/`End`/`PixelCount`
  (`EasyGLGraphicsBackend.cpp:395-418`, `QueryTarget::AnySamplesPassed`)
- Evidence: real XNA/FNA's `OcclusionQuery.PixelCount` (`OcclusionQuery.cs`, forwarding to
  `FNA3D_QueryPixelCount`) is documented and commonly used by games to get an actual magnitude (e.g.
  to compare visible-pixel counts for occlusion-culling heuristics, or draw thicker/thinner effects
  based on how much of an object is visible) — a real integer sample count, not a boolean. EasyGL's
  implementation is hard-capped to `GL_ANY_SAMPLES_PASSED`, so `PixelCount()` can only ever
  distinguish "fully occluded" (0) from "at least partially visible" (1); it cannot distinguish "1
  pixel visible" from "40,000 pixels visible," which real XNA code relying on the magnitude of
  `PixelCount` would require.
- Why it matters: this is a genuine, unremarked (in production code comments beyond the terse
  "GLES3 uses GL_ANY_SAMPLES_PASSED" note) behavioral divergence from FNA that this specific test
  cannot expose, because a fully-occluded quad reports `0` under both semantics. A sibling
  "fully-visible-quad" counterpart test (referenced in this file's own header, line 3, as "Task 445's
  own... test") would be the place this gap could actually surface — *if* that counterpart test
  checks `PixelCount()` against an expected magnitude (e.g. "≈ the quad's screen-space pixel area")
  rather than just `> 0`; this audit did not have that file in its assigned batch and did not verify
  which check it performs, so this is reported as a scope boundary, not a confirmed defect in that
  other file.
- FNA/XNA comparison: `OcclusionQuery.cs` → `FNA3D_QueryPixelCount`, a real GL `GL_SAMPLES_PASSED`
  query in FNA3D's OpenGL device, vs. CNA/EasyGL's `GL_ANY_SAMPLES_PASSED` boolean.
- Related files: `OcclusionQuery.cpp` (thin passthrough, no gap of its own — confirmed it forwards
  `PixelCount()` unmodified), the not-yet-audited (in this batch) EasyGL "visible quad" counterpart
  test.
- Suggested future action (not implemented by this audit): if the target GL context genuinely
  supports `GL_SAMPLES_PASSED` (desktop GL, or GLES3 with the appropriate extension), prefer it over
  `GL_ANY_SAMPLES_PASSED` to report a real magnitude; otherwise, document the 0/1-only limitation in
  `OcclusionQuery.hpp`'s own Doxygen comment (currently not checked as part of this batch) so API
  consumers do not write code assuming a real pixel-area magnitude on this backend.

### F2 — `PixelCount() <= 0` comparison has a dead "or lower" branch for this backend

- Severity: LOW
- Confidence: HIGH
- Category: maintainability (dead-code-shaped, not a bug)
- Location/symbol: line 160: `query.getPixelCountProperty() <= 0`
- Evidence: `EasyGLOcclusionQueryBackend::PixelCount()` returns `static_cast<int>(query_.result())`
  where `query_.result()` is a GL query result register that, for `GL_ANY_SAMPLES_PASSED`, is
  specified to be exactly `0` or `1` (a `GLuint`/boolean-style result) — never negative.
- Why it matters: purely cosmetic; `<= 0` reads as unnecessarily defensive against a case
  (`PixelCount() < 0`) that cannot occur given the current backend, though it would remain correct
  and harmless if a future backend (e.g. a hypothetical negative-sentinel "query not ready" encoding)
  ever changed that invariant.

## Cross-File Observations

- Shares the exact `colourMatch` helper (byte-wise tolerance-40 RGB comparison) with
  `easygl_model_two_meshes_effects_test.cpp` in this same batch, duplicated in each file's own
  anonymous namespace rather than factored into a shared test-utility header — a minor,
  low-severity duplication consistent with this shard's general pattern of small, self-contained
  hand-rolled (non-`PixelTestGame`) test files.
- F1 is the more consequential finding and belongs primarily to `EasyGLGraphicsBackend.cpp`'s and
  `OcclusionQuery.cpp`'s own audits, surfaced here because this file is what led to reading that
  production code.

## Missing or Weak Tests

- This file alone cannot validate `OcclusionQuery.PixelCount`'s magnitude semantics (see F1) — that
  would require the sibling "visible quad" test (or a new one) asserting an actual expected pixel
  count rather than a boolean-shaped `> 0`/`<= 0` check.

## Positive Findings

- The dual verification strategy (query result **and** independent backbuffer colour readback) is a
  genuinely good test design choice — it rules out the "nothing rendered at all" false-positive that
  a query-only check could not distinguish from real occlusion.
- Bounded, sync-forcing completion-polling loop is a reasonable, non-flaky way to wait for GPU query
  results without an unbounded spin or a fixed sleep.

## Final Assessment

Functionally correct and well-reasoned as a "does full occlusion report zero" test; its main value
beyond that narrow claim is limited by a genuine, evidence-confirmed parity gap in the underlying
`OcclusionQuery` backend (0/1 boolean semantics instead of XNA's real sample-count semantics) that
this specific occluded-quad scenario cannot expose either way.
