# Audit: examples/sdlrenderer_occlusionquery_throws_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_occlusionquery_throws_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 727, `OcclusionQuery::Begin/End` throw correctness on
  SDL_Renderer. This file's own commit fixed a real bug (`CreateOcclusionQuery` previously fell through to the
  shared default silent-`nullptr`, letting `OcclusionQuery` construct successfully with a permanently-null
  backend), then added this test as new coverage.
- File type: standalone `Game`-subclass executable, CTest-registered (`SDL_Renderer_OcclusionQueryThrows` /
  `cna_test_sdl_occlusionquery_throws`, `cmake/Tests/SdlRendererTests.cmake:400-402`).
- XNA/FNA relevance: `OcclusionQuery` construction/`Begin`/`End`/`IsComplete`/`PixelCount` semantics.
- Related production code: `include/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.hpp`
  (`CreateOcclusionQuery` override, lines 163-167), `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`CreateOcclusionQuery`, lines 807-811; `ThrowNo3D`, lines 779-783).

## Purpose

Confirms `OcclusionQuery`'s constructor now throws the exact expected message on SDL_Renderer (rather than
silently succeeding with a null backend, the pre-fix bug this task discovered), and that
`GraphicsDevice` remains fully usable afterward. Explicitly notes `Begin()`/`End()` can never be reached on a
valid instance on this backend since construction always throws first — the same shape as the sibling Task 720
(VertexBuffer/DrawPrimitives) finding.

## Executive Verdict

**Healthy** — the fix and its test are both confirmed correct and consistent with the rest of this backend's
"throw loudly, don't silently degrade" design; one INFO-level note that the header comment's "only existing test
coverage" claim, while true at the time it was written, is now stale given later-added sibling-backend occlusion
query tests (does not affect this file's own correctness).

## Checklist Results

### API / XNA / FNA parity

`OcclusionQuery(dev)` constructing and immediately failing is the correct XNA-facing behavior for a backend that
genuinely cannot implement real GPU occlusion queries — matches this backend's already-established pattern for
every other 3D-only construction path (`VertexBuffer`, `IndexBuffer`, confirmed in the sibling
`sdlrenderer_graphics_capability_test.cpp`/`SdlGraphicsBackend.cpp` audits in this same shard).

### Behavioral correctness

Traced the fix directly: `SdlGraphicsBackend::CreateOcclusionQuery()` (`SdlGraphicsBackend.cpp:807-811`) is
`ThrowNo3D("CreateOcclusionQuery"); return nullptr;` — `ThrowNo3D` (lines 779-783) throws
`std::runtime_error(std::string("SDL_Renderer does not support 3D: ") + methodName)`, producing exactly
`"SDL_Renderer does not support 3D: CreateOcclusionQuery"` — a byte-for-byte match to this test's asserted string
(line 77). The header declares this override explicitly (`SdlGraphicsBackend.hpp:163-167`, with its own comment
crediting Task 727 and explaining the prior silent-nullptr gap), confirming this is a real, present override, not
merely inherited default behavior that happens to match.

`ThrowsExactRuntimeError()` (lines 49-65) correctly narrows to `std::runtime_error` with an exact string compare
(`std::strcmp`) rather than a substring/prefix match — the strictest reasonable assertion for a message this
precise.

### Robustness

Since construction always throws, there is genuinely no way to reach `Begin()`/`End()` on a valid instance on this
backend — the test correctly does not attempt to (its own comment at lines 80-82 states this explicitly), avoiding
a test that would otherwise need to either skip a code path (misleadingly) or use an invalid/partially-constructed
object (undefined behavior risk). The post-throw functionality check (lines 84-91: cyan `Clear` +
1x1 readback) follows the same pattern already verified correct in this shard's other throw-tests.

### Testing

Confirmed via grepping `cmake/Tests/*.cmake` and `git log --diff-filter=A` timestamps: at the time this file was
authored (2026-07-08 23:00), the only other `OcclusionQuery`-touching test in the repository was
`examples/occlusion_query_test.cpp` (`EasyGL_OcclusionQuery_Cycle`, created 2026-06-14) — every other
backend-specific occlusion-query test now in the tree
(`bgfx_occlusion_query_test.cpp`, `easygl_occlusion_query_visible_quad_test.cpp`,
`easygl_occlusion_query_occluded_quad_test.cpp`, `vulkan_occlusionquery_pixelcount_test.cpp`,
`bgfx_occlusionquery_pixelcount_test.cpp`, `bgfx_occlusionquery_dispose_active_test.cpp`) was added *after* this
file, on 2026-07-09/07-10. The comment's claim was accurate when written; see F1 below for the resulting present-day
staleness.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. One INFO-level note:

### F1 — Header comment's "OcclusionQuery's only existing test coverage" claim is now stale (was accurate at authoring time)

- Severity: INFO
- Confidence: HIGH (confirmed via `git log --diff-filter=A` timestamps on every relevant file)
- Category: documentation-accuracy (historical claim, not a current defect)
- Location/symbol: header comment lines 10-13
- Evidence: this file was authored 2026-07-08 23:00:48 (`fix(Task 727)` commit). At that moment,
  `examples/occlusion_query_test.cpp` (2026-06-14) genuinely was the only occlusion-query test in the tree. Six
  more occlusion-query tests for Bgfx/EasyGL/Vulkan were added over the following two days (2026-07-09 through
  2026-07-10), all postdating this file.
- Why it matters: a reader today taking the comment's "OcclusionQuery's only existing test coverage is
  EasyGL_OcclusionQuery_Cycle" at face value would incorrectly conclude occlusion queries are undertested
  project-wide; they are not (Bgfx and Vulkan now both have dedicated, more thorough occlusion-query test
  suites). This does not affect the correctness of this file's own fix or test — it is a documentation-freshness
  observation matching the audit's own instruction to independently re-verify such claims rather than accept them
  at face value, per this shard's precedent (the EasyGL specular test's F1 in a prior batch).
- FNA/XNA comparison: N/A.
- Related files: `examples/occlusion_query_test.cpp`, `examples/bgfx_occlusion_query_test.cpp`,
  `examples/vulkan_occlusionquery_pixelcount_test.cpp`, `examples/easygl_occlusion_query_visible_quad_test.cpp`,
  `examples/easygl_occlusion_query_occluded_quad_test.cpp`.
- Suggested future action (not implemented by this audit): none required — this is a point-in-time historical
  note in a comment, not a currently-misleading claim about *this backend's* coverage (which remains accurate:
  this file genuinely is still the only SDL_Renderer-run `OcclusionQuery` test).

## Cross-File Observations

- Directly parallels `sdlrenderer_graphics_capability_test.cpp`'s own confirmation that
  `SupportsCapability(GraphicsCapability::OcclusionQuery)` returns `false` on this backend — the two files
  together give complete coverage of both the advisory-capability-check surface and the actual throw behavior for
  occlusion queries on SDL_Renderer.
- Same "construction throws, so Begin/End are unreachable" shape as Task 720's `VertexBuffer`/`DrawPrimitives`
  finding, explicitly cross-referenced in this file's own comment (line 82) — consistent, not a new pattern.

## Missing or Weak Tests

None for this file's stated scope. As with the sibling `VertexBuffer`/`IndexBuffer` throw-tests, there is no way
to test `Begin()`/`End()`'s own logic on this backend given construction always fails first — this is an inherent
backend limitation, not a coverage gap in the test.

## Positive Findings

- This file's own underlying production fix (adding the missing `ThrowNo3D` override) closes a genuine silent-
  no-op bug the author found while writing this test — a good example of test-writing surfacing a real defect
  rather than merely confirming pre-existing behavior.
- Exact-string exception assertion, consistent with this shard's strongest test files.
- Explicitly and correctly reasons about *why* this fix was safe (zero pre-existing SDL_Renderer test coverage of
  `OcclusionQuery` to regress), a genuinely useful category of due diligence this audit independently confirmed
  via `git log`.

## Final Assessment

A correct fix paired with a precise, well-targeted regression test. The one historical claim in the header
comment (F1) has since become stale due to unrelated later work on other backends, but this does not affect the
file's own current correctness.
