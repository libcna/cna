# Audit: include/Microsoft/Xna/Framework/Input/Keys.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Input/Keys.hpp`
- Audit status: AUDITED (full read, 495 lines, header-only, no `.cpp`)
- Subsystem: `xna-input` shard
- File type: C++ header (enum)
- XNA/FNA relevance: Direct XNA type; every one of the 160 key values cross-checked by name and
  numeric value against `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Keys.cs` via an
  automated diff (extract all `Name = value` pairs from both files, normalize whitespace, diff) —
  zero differences found across the full set
- Main related tests: not independently located in this pass

## Purpose
Defines every XNA virtual key code (letters, digits, function keys, OEM/media/browser keys, IME
keys).

## Executive Verdict
Correct. A complete, automated cross-check of all 160 key names and their numeric values against
FNA's real `Keys.cs` found zero discrepancies — this is a full, verified 1:1 port.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Consumed throughout the shard (`KeyboardState`, `Keyboard`).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Complete, automated-diff-verified match to FNA across all 160 values.

## Final Assessment
No findings.
