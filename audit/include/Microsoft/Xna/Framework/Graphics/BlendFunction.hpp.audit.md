# Audit: include/Microsoft/Xna/Framework/Graphics/BlendFunction.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/BlendFunction.hpp` (21 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header (enum)
- XNA/FNA relevance: Direct XNA type; FNA reference: `Graphics/States/BlendFunction.cs` (fully diffed)
- Main related tests: not independently located in this pass

## Purpose
Defines the 5 blend-combination functions (`Add`, `Subtract`, `ReverseSubtract`, `Max`, `Min`).

## Executive Verdict
Correct — all 5 values present in the same order/ordinal as FNA. One notable, positive finding: FNA's own XML doc comments for `Max`/`Min` are internally inconsistent with their own names (FNA's `Max` doc says "will extract **minimum** of the source and destination... `min(...)`", and its `Min` doc says "will extract **maximum**... `max(...)`" — the two doc comments are swapped relative to the enum member names in FNA's own source). This port's Doxygen comments (`Max`: "Returns the **maximum** of source and destination: `max(...)`"; `Min`: "Returns the **minimum**... `min(...)`") are self-consistent with the actual member names — i.e. this port did not copy FNA's doc-comment bug verbatim, which is the correct choice (the member *names* `Max`/`Min` are unambiguous XNA API surface; only FNA's *prose* was self-contradictory).

## Checklist Results
No issues found.

## Detailed Findings
None. (The FNA doc-comment inconsistency noted above is an FNA-side documentation bug, not a CNA defect — recorded here for completeness since it directly explains why this file's wording differs from a literal copy of FNA's comments.)

## Cross-File Observations
None.

## Missing or Weak Tests
Not applicable (plain enum).

## Positive Findings
Correctly avoided propagating FNA's own internally-contradictory doc comments for `Max`/`Min`.

## Final Assessment
No findings.
