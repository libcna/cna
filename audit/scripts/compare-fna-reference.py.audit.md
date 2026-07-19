# Audit: scripts/compare-fna-reference.py

## Metadata
- Source file: `scripts/compare-fna-reference.py` (100 lines)
- Audit status: AUDITED (full read)
- Subsystem: `scripts` shard
- File type: Python script (JSON diff tool)
- XNA/FNA relevance: N/A (developer tooling — diffs FNA-side vs CNA-side reference-value JSON dumps)
- Main related tests: not a CTest itself; consumes `tools/fna-reference/`'s and `tools/cna-reference/`'s JSON output

## Purpose
Generic, recursive JSON-diff tool comparing FNA's reference-value dump against CNA's own, one key
path at a time, with a documented one-directional comparison (every FNA key must exist and match on
the CNA side; CNA-only keys, e.g. NOXNA extensions, are correctly not flagged as mismatches).

## Executive Verdict
Correct and appropriately minimal. **High-value cross-check completed**: this script is a pure,
generic JSON differ — it does NOT itself choose or generate any test input values; it only
compares whatever numbers already exist in the two JSON files handed to it. This confirms the
already-documented root-cause finding for the `Byte4`/`Short2`/`Short4` truncation-vs-rounding
defect (see `audit/tools/fna-reference/PackedVectorReference.cs.audit.md`) lies entirely upstream,
in `PackedVectorReference.cs`'s choice of integer-only test inputs — this script had no independent
opportunity to catch that gap, since it never sees or chooses the inputs, only the already-computed
outputs.

## Checklist Results
- The one-directional comparison design (FNA→CNA only) is explicitly and correctly justified: CNA
  has real NOXNA extensions with no FNA equivalent, and requiring bidirectional coverage would
  reject legitimate intentional extensions as false failures.
- Float comparison uses an explicit absolute tolerance (default `1e-4`), not exact equality —
  appropriate for cross-language floating-point reference comparison.
- `--category` filtering with a sensible default list (`NonRenderingApis`, `PackedVector`,
  `Viewport`) and a clear warning (not silent skip) when a requested category is absent from the
  FNA JSON.

## Detailed Findings
None.

## Cross-File Observations
Directly downstream of `tools/fna-reference/PackedVectorReference.cs` (audited earlier this
session) and `tools/cna-reference/CnaReferenceDump.cpp` — this script's own correctness does not
change the conclusion that the already-confirmed HIGH finding (integer-only PackedVector test
inputs hiding the Byte4/Short2/Short4 truncation bug) originates entirely in the FNA-side generator,
not in this comparison logic.

## Missing or Weak Tests
N/A (developer tool, not itself under test) — its own correctness is straightforward enough
(a generic recursive dict-path comparison) that the lack of a dedicated test for this script itself
is a low-risk gap, not flagged as a finding.

## Positive Findings
Clear, correct one-directional-comparison design with an explicit, well-reasoned justification
rather than an unexplained asymmetry.

## Final Assessment
No findings in this file itself. Confirms (via direct code inspection) that it could not have
independently caught the PackedVector rounding defect — that responsibility lies solely with the
FNA-side reference generator's test-input selection.
