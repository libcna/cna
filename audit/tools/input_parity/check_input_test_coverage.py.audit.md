# Audit: tools/input_parity/check_input_test_coverage.py

## Metadata
- Source file: `tools/input_parity/check_input_test_coverage.py` (222 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-input-parity` shard
- File type: Python script (developer inspection tool)
- XNA/FNA relevance: developer tooling, not XNA API surface — cross-checks Input types against the
  test tree for coverage gaps
- Main related tests: N/A (this tool inspects the test tree; it is not itself a test)

## Purpose
Scans every public XNA `Input` header (+ internal `CNA::Internal::Input` sources) and every test
file, reporting whether each type has a dedicated `TEST(<Type>Test, …)` suite and how many test
files reference it at all, flagging orphaned/under-covered types as candidate `INPUT-TEST-*` tasks.

## Executive Verdict
Correct and appropriately self-aware about its own limits — the docstring explicitly states
"this is an inspection aid, not an authority" and the hand-maintained `KNOWN_COVERED_ELSEWHERE`
exemption table (18 entries) documents, per-type, exactly why a missing same-named suite is
expected rather than a real gap, with several entries explicitly noting "confirmed real coverage
exists... before adding the exemption."

## Checklist Results
- `types_in_header()`'s regex (`\b(?:enum\s+class|class|struct)\s+([A-Za-z_]\w*)`) is a simple
  textual scan, not a real parser — it would over-match a forward declaration or a nested/local
  type declared inside a function body. Not flagged as a defect: the tool's own docstring already
  frames every output row as something to "read... before filing a task," not ground truth.
- `analyse()`'s dedicated-suite regex (`TEST(?:_F)?\(\s*<Name>\w*Test\b`) correctly handles both
  `TEST()` and `TEST_F()` macros, and the `\w*Test` suffix correctly matches suite names like
  `KeyboardInputTest` for type `Keyboard` without requiring an exact match.
- The exemption table's own comments show real diligence: several entries (e.g. `ISdlHapticBackend`,
  `KeyModifiersEXT`) explicitly state real coverage was independently confirmed before exempting,
  rather than exemptions being added reflexively to silence the tool.

## Detailed Findings
None.

## Cross-File Observations
Complements `tools/input_parity/gen_input_parity_matrix.py` (audited alongside this file) — this
tool checks *test* coverage; that one checks *FNA member-level* parity. Together they cover two
distinct axes of the Input subsystem's health that this session's earlier `xna-input` shard audit
(committed, found only two LOW documentation findings) didn't need to duplicate.

## Missing or Weak Tests
This file IS a testing/inspection tool; not applicable in the usual sense. No test was located
verifying the tool's own output against a known-correct fixture (e.g. a synthetic header + test
pair with a known-orphaned type), which would guard against a regression in the tool's own regex
logic silently under- or over-reporting gaps.

## Positive Findings
The exemption table's discipline (each entry justified, several explicitly citing that real
coverage was verified before exempting) is a strong example of a "review aid" tool being kept
honest rather than becoming a rubber stamp.

## Final Assessment
No findings.
