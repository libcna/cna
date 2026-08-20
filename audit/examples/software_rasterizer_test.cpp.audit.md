# Audit: examples/software_rasterizer_test.cpp

## Metadata
- Source file: `examples/software_rasterizer_test.cpp` (244 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-tests-software` shard
- File type: C++ source (standalone backend integration/smoke test)
- XNA/FNA relevance: exercises `GraphicsDevice::DrawPrimitives`/`DrawIndexedPrimitives` through `BasicEffect`, real per-pixel color interpolation, and depth testing on the Software backend's rasterizer core
- Main related tests: N/A (this IS the test; registered as `Software_Rasterizer` in `cmake/Tests/SoftwareTests.cmake`)

## Purpose
5-check proof that the Software backend's CPU rasterizer core actually works: flat-color fill,
per-pixel barycentric color interpolation, depth-test correctness in BOTH draw orders (proving
real occlusion, not "last write wins"), and `DrawIndexedPrimitives`/`DrawPrimitives` result parity.

## Executive Verdict
Excellent test design, particularly Checks C/D's depth-test pair: drawing the same far/near
triangle pair in both orders and requiring the near (red) triangle to win either way is a genuinely
strong test that specifically rules out the common false-positive failure mode of a depth test
that merely happens to produce the right answer because of draw order rather than real Z-comparison
— the file's own comment explicitly names this concern ("not an accidental 'last write wins'
artifact of Check C's particular draw order").

## Checklist Results
- Check B's use of an explicit pure-`(0,255,0)` green rather than XNA's `Color::Green` (which is
  the CSS-standard `(0,128,0)`) is correctly explained in-comment as keeping the "each channel ≈
  255/3" barycentric-average expectation simple and symmetric — a deliberate, well-reasoned test
  data choice, not an accidental deviation from the "real" XNA green.
- `RasterizerState::CullNone` is explicitly set with a comment explaining the test triangles were
  authored for pixel-correctness, not XNA winding-convention compliance, and are back-facing under
  the real default — correctly scoping this file's assertions away from a concern
  (`software_culling_test.cpp`) that's independently tested elsewhere.
- The "NDC equals raw vertex position directly since World/View/Projection are all identity"
  simplification is explicitly stated and correctly used throughout to let the test reason about
  exact screen locations without a full projection matrix.

## Detailed Findings
None.

## Cross-File Observations
The `RasterizerState::CullNone` scoping decision and its rationale exactly matches
`software_effects_test.cpp`'s and `software_dual_envmap_skinned_test.cpp`'s identical pattern —
consistent test-isolation discipline across the shard.

## Missing or Weak Tests
Not applicable — the 5-check scope matches the file's own stated `plans/plan_software.md` Phase S4/S6
task range.

## Positive Findings
The order-reversed depth-test pair (Checks C/D) is an exemplary test design that specifically rules
out a subtle false-positive failure mode most test suites would miss.

## Final Assessment
No findings.
