# Audit: src/CNA/Internal/Backends/D3D11/D3D11StateObjectCache.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D11/D3D11StateObjectCache.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d11` shard
- File type: C++ implementation (161 lines)
- Related header: `include/CNA/Internal/Backends/D3D11/D3D11StateObjectCache.hpp` (same shard)
- XNA/FNA relevance: implements the 3 state-object caches
- Graphics backend relevance: D3D11-specific
- FNA reference: FNA's own D3D11 conventions
- Main related tests: `examples-tests-d3d11` (not yet audited)

## Purpose

Implements `GetOrCreate()` for all 3 caches via `D3D11_BLEND_DESC`/`D3D11_DEPTH_STENCIL_DESC`/
`D3D11_RASTERIZER_DESC` construction and `Create*State()` device calls.

## Executive Verdict

**Healthy.** Every non-trivial derived value (Opaque-blend detection, two-sided-stencil gating, DepthBias
rounding) independently re-derived and confirmed correct.

## Checklist Results

### API / XNA / FNA parity
`isOpaque` detection (`colorSrcBlend==0 && colorDstBlend==1 && alphaSrcBlend==0 && alphaDstBlend==1`, i.e.
`Blend::One`/`Blend::Zero`) correctly identifies `BlendState.Opaque` as a mathematical no-op, explicitly and
accurately cross-referenced against `VulkanGraphicsBackend::ApplyBlendState`'s own established heuristic (Task
868) — independently verified this project's `Blend` enum does have `One=0`/`Zero=1`, consistent with the
`D3DStateMapping.cpp` mapping already audited.
`twoSidedStencilMode` gating (`desc.BackFace = desc.FrontFace` when false, rather than wiring in possibly-stale
`ccw*` fields) correctly matches FNA/D3D9's real behavior (front-face ops apply to both faces when two-sided mode
is off) — independently cross-referenced against this project's own EasyGL precedent, which uses the identical
"only call the `_separate(Back,...)` entry points when two-sided" gating.
`DepthBias`'s float-to-`D3D11_RASTERIZER_DESC::DepthBias INT` conversion uses `std::lround` (round-to-nearest, not
truncate) — correct, and the comment's claim that XNA's `RasterizerState.DepthBias` is already expressed in the
same "r"-scaled units this project's Vulkan/EasyGL backends feed unscaled into their own bias APIs is consistent
with those backends' own already-reviewed comments (Task 767).

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

None beyond the already-recorded architecture-level gaps (see paired header report).

## Cross-File Observations

The Opaque-blend and two-sided-stencil heuristics both correctly cross-reference and match established precedent
in other backends already audited in this session (Vulkan, EasyGL) — genuine cross-backend consistency, not
independently-reinvented (and possibly divergent) logic.

## Missing or Weak Tests

No dedicated test found exercising the Opaque-blend fast path, two-sided-stencil mode, or DepthBias rounding
specifically on this backend.

## Positive Findings

Three independently-verified-correct, non-trivial pieces of derived logic (Opaque detection, two-sided-stencil
gating, DepthBias rounding), each accurately cross-referenced against established precedent elsewhere in the
project.

## Final Assessment

No issues found beyond the already-recorded architecture-level gaps inherited from the paired header.
