# Audit: include/Microsoft/Xna/Framework/Rectangle.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Rectangle.hpp`
- Audit status: AUDITED (full read, 268 lines)
- Subsystem: `xna-framework-core` shard
- File type: C++ header
- XNA/FNA relevance: matches real XNA 4.0 `Microsoft.Xna.Framework.Rectangle` exactly
- Main related tests: not independently located in this pass

## Purpose
Declares `Rectangle`: integer axis-aligned rectangle with Left/Right/Top/Bottom/Location/Center properties,
Contains/Intersects/Intersect/Union, Offset/Inflate.

## Executive Verdict
Healthy.

## Checklist Results
Complete API matching real XNA `Rectangle` exactly -- no members missing or extraneous.

## Detailed Findings
None -- see the paired `.cpp`.

## Cross-File Observations
Depends on `Point` (already audited, correct).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Complete, correct API surface.

## Final Assessment
No issues found.
