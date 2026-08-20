# Audit: include/CNA/Graphics/LodGroupEXT.hpp

## Metadata

- Source file: `include/CNA/Graphics/LodGroupEXT.hpp`
- Physical location: `modules/graphics-ext/include/CNA/Graphics/LodGroupEXT.hpp`
- Audit status: PENDING
- Subsystem: `cna-graphics` shard — the `CNA::Graphics` engine layer
- File type: C++ header
- XNA/FNA relevance: N/A — `CNA::Graphics`, not `Microsoft::Xna`. The whole layer is behind the
  `CNA_CNAEXT` CMake option (default OFF) and every file in it is `#ifdef CNA_CNAEXT`-guarded,
  which `scripts/check_cnaext_guards.sh` enforces.
- Graphics renderer relevance: none directly — the engine layer talks to `GraphicsDevice` and the renderer contracts, never to a renderer implementation
- Plan rows: `MOD-1406`, `MOD-1407`, `MOD-1408`

## Purpose

How a `LodGroupEXT` decides which level a distance selects. */ enum class LodSelectionMode { /** @brief By camera distance alone: the first level whose maximum distance covers it.

## Executive Verdict

Not yet audited. This entry is a work-queue placeholder created by `plan_modern.md` `MOD-12` so the
engine layer's files are tracked rather than invisible to the audit inventory; the file itself
landed with its own tests and build verification under the plan row(s) named above.

## Checklist Results

Pending.

## Detailed Findings

Pending.

## Cross-File Observations

Pending.

## Missing or Weak Tests

Pending. Note that the plan requires tests with each row, so the starting point for an audit here is
`modules/graphics-ext/tests/CNA/Graphics/`, not an empty slate.

## Positive Findings

Pending.

## Final Assessment

Pending.
