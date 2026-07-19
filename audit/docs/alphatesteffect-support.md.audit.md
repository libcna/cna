# Audit: docs/alphatesteffect-support.md

## Metadata
- Source file: `docs/alphatesteffect-support.md` (146 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown feature-support matrix
- XNA/FNA relevance: describes `AlphaTestEffect` conformance across EasyGL/Vulkan/Bgfx

## Purpose
Summarizes Phase 43's audit and fix history for `AlphaTestEffect`: property defaults,
`CompareFunction` pixel coverage, reference-alpha scaling, vertex-color interaction, fog, and
null-texture fallback, across all three graphics backends of that era.

## Executive Verdict
Internally consistent and appropriately self-updating: Task 888 (project-wide fog no-op on
Vulkan/Bgfx) is correctly marked `~~Task 888~~ — fixed by Task 899` rather than left stale. The one
still-open item (Task 887, `VertexColorEnabled` not affecting Vulkan/Bgfx's alpha-test comparison) is
consistently marked ❌ in both the prose and the summary table, with no contradiction between them.

## Checklist Results
- Cross-checked the still-open Task 887 (`VertexColorEnabled` × alpha-test on Vulkan/Bgfx) against
  this session's own `xna-graphics` shard audit (191 files, includes `AlphaTestEffect.cpp`) — no
  finding in that shard's report contradicts or confirms this specific gap (it wasn't independently
  re-derived there), so no corroboration either way; treated as still-open per this doc's own claim.
- The support matrix's legend (✅ verified working / ❌ confirmed not implemented) is used
  consistently in every row — no ambiguous middle-ground markers left unexplained.

## Detailed Findings
None — this document only describes historical Phase 43 work and is consistent with itself
throughout; the one open item (Task 887) is clearly flagged as open, not silently implied fixed.

## Cross-File Observations
Task 899 (fog fix) is independently corroborated by `docs/README.md`'s own Graphics section, which
cites the identical Task 899 fix date (2026-07-07) for the same fog gap across `BasicEffect` and
siblings — consistent cross-referencing, not a contradiction.

## Missing or Weak Tests
N/A — describes test work already done (Tasks 372-379 explicitly add tests); no gap identified in
the document's own account of test coverage for this effect.

## Positive Findings
The "why it was never fixed inline" discipline (Task 887 explicitly scoped out as "a genuinely large,
6-shader-file, 2-backend change, not a Task-377-sized fix") mirrors the same honest-scoping pattern
found elsewhere in this project's own CMake test-registration comments (`cmake/Tests/VulkanTests.cmake`'s
Task 868 disclosures, audited in the `build-cmake-tests` shard) — a consistent project-wide value.

## Final Assessment
No findings. An accurate, internally consistent, appropriately-updated historical record.
