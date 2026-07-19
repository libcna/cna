# Audit: scripts/avatar_visual_regression_check.py

## Metadata
- Source file: `scripts/avatar_visual_regression_check.py` (222 lines)
- Audit status: AUDITED (full read)
- Subsystem: `scripts` shard
- File type: Python script (visual regression check)
- XNA/FNA relevance: N/A (developer tooling around `cna_demo_avatar`'s real rendering pipeline)
- Main related tests: not a CTest itself; a manually-invoked regression check against `cna_demo_avatar`

## Purpose
Reproducible visual regression check against the REAL avatar rendering pipeline (not a synthetic
fixture): captures screenshots at fixed camera poses and measures a "speckle" metric (dark pixels
adjacent to bright pixels) per fixed region, specifically targeting interpenetration-seam artifacts
between low-poly meshes, plus global brightness/darkness sanity checks.

## Executive Verdict
Excellent — this is the 4th documented iteration of this exact check (per its own top-of-file
comment), each iteration explicitly citing what the PRIOR version got wrong: version 3 only
measured global brightness/darkness, which the audit correctly rejected as insufficient since a
localized defect could hide behind a passing global average. The "speckle" metric is a
well-reasoned, defect-shape-targeted proxy (interleaved dark/bright transitions = an intersection
seam) rather than a generic image-diff.

## Checklist Results
- Explicitly documents that PASSING does NOT mean the avatar looks correct, only that it hasn't
  regressed below a recorded floor — an honest, non-overclaiming contract, with a pointer to
  `NEXTnet.md` for the list of still-open defects.
- Per-region thresholds are recorded with the actually-measured value in an adjacent comment (e.g.
  `foot_left: (75, 1.0), # measured 49`) — a real, non-arbitrary margin above the last known-good
  measurement, not a guessed round number.
- Left/right foot boundaries are deliberately tracked as SEPARATE regions specifically because the
  avatar is mirror-symmetric and a one-sided regression could otherwise be averaged away by a
  combined box — a subtle, correctly-reasoned test design choice.
- `--report` mode correctly always exits 0 (for re-recording thresholds) while still printing
  measured values — a clean separation between "measure" and "enforce" modes.

## Detailed Findings
None.

## Cross-File Observations
This is round 4 of the same regression-check evolution referenced in this session's own
`project_net_audit_second_round` memory (round 5's flat-cap garment redesign was measured WORSE and
reverted, round 6 predates this file's own most recent recorded thresholds) — consistent with that
history of iterative, evidence-based avatar-rendering quality tracking.

## Missing or Weak Tests
N/A (this IS a check/tool, not itself under test).

## Positive Findings
The explicit "prior version was insufficient, here's why, here's the improved metric" framing
across 4+ iterations is a strong example of a test suite that gets more precise over time in
response to real, documented defects, rather than staying static.

## Final Assessment
No findings. A well-reasoned, honestly-scoped, iteratively-improved regression check.
