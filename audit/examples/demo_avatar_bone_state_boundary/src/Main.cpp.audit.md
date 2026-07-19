# Audit: examples/demo_avatar_bone_state_boundary/src/Main.cpp

## Metadata
- Source file: `examples/demo_avatar_bone_state_boundary/src/Main.cpp` (35 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_avatar_bone_state_boundary` shard
- File type: standalone demo entry point
- XNA/FNA relevance: N/A (process bootstrap only)

## Purpose
Parses `--show-help`/`--screenshot <path>` and runs `BoundaryDemo`. No `--smoke` flag — this demo
always self-exits after a fixed frame count (`framesBeforeExit_ = 30` in the header), consistent
with its "prints once, then exits" design.

## Executive Verdict
Correct. `new BoundaryDemo()` / `game->Run()` / `delete game` — clean.

## Checklist Results
- Argument parsing is minimal and unambiguous.

## Detailed Findings
None.

## Cross-File Observations
None beyond the ownership confirmation already noted in `BoundaryDemo.cpp.audit.md`.

## Missing or Weak Tests
Not applicable — process entry point.

## Positive Findings
Correctly omits a `--smoke` flag since this demo doesn't need one (it always self-exits
deterministically) — no unused/dead CLI surface.

## Final Assessment
No findings.
