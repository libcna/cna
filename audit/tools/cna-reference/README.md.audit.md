# Audit: tools/cna-reference/README.md

## Metadata
- Source file: `tools/cna-reference/README.md` (63 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-cna-reference` shard
- File type: documentation
- XNA/FNA relevance: documents a developer workflow for diffing CNA's Graphics enums/state-
  presets/PackedVector/Viewport values against real FNA
- Main related tests: N/A

## Purpose
Documents how to build/run `cna_reference_dump`, generate the equivalent FNA-side reference values,
diff them via `scripts/compare-fna-reference.py`, and reports the actual historical result of doing
so (one genuine divergence found and since fixed).

## Executive Verdict
Accurate against the actual tool it documents. The build/run commands
(`cmake --build ... --target cna_reference_dump`, default output filename
`cna-reference-values.json`) match `CnaReferenceDump.cpp`'s own `main()` (audited alongside this
file) exactly, including the default-output-path fallback and the one-directional comparison
semantics (extra CNA-only keys not reported as mismatches).

## Checklist Results
- The documented comparison direction ("every key present on the FNA side must exist on the CNA
  side... Keys that exist only on the CNA side are not reported as mismatches") correctly matches
  `CnaReferenceDump.cpp`'s own `PrimitiveType.PointListEXT` comment, which relies on exactly this
  asymmetry.
- The "Status" section's specific, dated claim ("Task 921 has since fixed this (2026-07-09)") is
  the kind of concrete, falsifiable historical record that stays useful rather than going stale —
  it documents both the original finding and its resolution, not just one or the other.

## Detailed Findings
None.

## Cross-File Observations
Directly documents `CnaReferenceDump.cpp` and `JsonWriter.hpp` (both audited alongside this file) —
all three files' claims are mutually consistent.

## Missing or Weak Tests
N/A (documentation file).

## Positive Findings
Accurately documents both a positive result (21 enums, 16 presets, 17 PackedVector types, 5
Viewport cases all match FNA exactly) and a real, historical divergence with its resolution —
neither overstating success nor hiding a past defect.

## Final Assessment
No findings.
