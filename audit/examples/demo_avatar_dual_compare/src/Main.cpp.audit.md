# Audit: examples/demo_avatar_dual_compare/src/Main.cpp

## Metadata
- Source file: `examples/demo_avatar_dual_compare/src/Main.cpp` (43 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_avatar_dual_compare` shard
- File type: standalone demo entry point
- XNA/FNA relevance: N/A (process bootstrap only)

## Purpose
Parses `--smoke [N]`/`--show-help`/`--screenshot <path>` and runs `DualCompareDemo`.

## Executive Verdict
Correct. `new DualCompareDemo()` / `game->Run()` / `delete game` — clean, matches every other demo.

## Checklist Results
- No ambiguous/overlapping flag parsing.

## Detailed Findings
None.

## Cross-File Observations
None beyond the ownership confirmation already noted in `DualCompareDemo.cpp.audit.md`.

## Missing or Weak Tests
Not applicable — process entry point.

## Positive Findings
Clean, minimal, consistent with the rest of this demo family.

## Final Assessment
No findings.
