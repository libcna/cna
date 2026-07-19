# Audit: examples/canvas_graphics_capability_test.cpp

## Metadata
- Source file: `examples/canvas_graphics_capability_test.cpp` (88 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-tests-canvas` shard
- File type: standalone backend integration-test executable (`Game` subclass)
- XNA/FNA relevance: exercises `GraphicsDevice::SupportsCapability()` (a NOXNA CNA extension,
  `CNA::GraphicsCapability`) against the Canvas (HTML Canvas 2D, 2D-only) backend

## Purpose
Verifies `SupportsCapability()` correctly reports Canvas supports none of the 8 enumerated
capabilities, and that calling the corresponding 3D methods anyway still throws exactly as before —
`SupportsCapability()` is a pre-check, not an enforcement mechanism.

## Executive Verdict
Correct. The final two checks (`SetDepthTestEnabled`/`VertexBuffer` construction still throw
despite `SupportsCapability()` returning false first) are the important, non-trivial part of this
test: they prove the capability-query API is purely advisory and doesn't accidentally short-circuit
or bypass the real throw path.

## Checklist Results
- All 8 `GraphicsCapability` enumerants are checked (`ThreeD`, `DepthStencilBuffer`,
  `MultiSampleAntiAliasing`, `MultipleRenderTargets`, `AnisotropicFiltering`, `WireFrame`,
  `OcclusionQuery`, `CustomEffects`) — full coverage of the capability set for this backend.
- Header comment explicitly identifies this as a twin of
  `sdlrenderer_graphics_capability_test.cpp`/`dx3_graphics_capability_test.cpp` — consistent
  cross-backend test design for the same capability-query API.

## Detailed Findings
None.

## Cross-File Observations
Twin test design shared with `dx3_graphics_capability_test.cpp` (audited in the same batch) — both
files assert an identical "no capability supported, but the real 3D call still throws" pattern for
their respective 2D-only backends, mutually consistent.

## Missing or Weak Tests
None identified for this file's stated scope.

## Positive Findings
The explicit distinction drawn between "capability check" and "enforcement" (verified by actually
calling the throwing method after the check reports unsupported) is a meaningful API-contract test
that a naive implementation could get wrong (e.g. by making `SupportsCapability()` accidentally
gate the real call).

## Final Assessment
No findings.
