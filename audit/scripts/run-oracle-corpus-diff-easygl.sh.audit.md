# Audit: scripts/run-oracle-corpus-diff-easygl.sh

## Metadata
- Source file: `scripts/run-oracle-corpus-diff-easygl.sh` (85 lines)
- Audit status: AUDITED (full read)
- Subsystem: `scripts` shard
- File type: Bash script (cross-backend XNA-oracle measurement, NOT a registered CTest)
- XNA/FNA relevance: measures the EasyGL backend's divergence from real XNA 4.0 reference PNGs
- Main related tests: not a CTest itself — a one-time cross-backend measurement tool

## Purpose
Cross-backend twin of `run-oracle-corpus-diff.sh`: renders the same checked-in scene corpus through
the EasyGL-native `cna_oracle_render_easygl` binary (no Wine involved — native Linux ELF/OpenGL) and
diffs against the same checked-in real-XNA reference PNGs, at the same `tolerance=0`.

## Executive Verdict
Correct, and appropriately honest about its own different purpose from its D3D9 sibling: explicitly
NOT registered as a CTest, since (per its own comment) EasyGL is not expected to be pixel-identical
to real XNA the way the DXVK-backed D3D9 path is — this is a one-time "record the deltas"
measurement, not a permanently-enforced gate, with results tracked in
`docs/d3d9-divergence-report.md`'s "Cross-backend measurement" section.

## Checklist Results
- Correctly sets `SDL_VIDEODRIVER=x11`/`DISPLAY` defaults (this binary needs a real X11 display,
  unlike the D3D9 Wine-wrapped equivalent) while still respecting an already-exported `DISPLAY`.
- Repeats the same "never widen tolerance to turn red green" discipline from its D3D9 sibling.
- Clearly labels its own summary line with "-- EasyGL backend" to distinguish its output from the
  D3D9 script's otherwise-identical-looking summary line.

## Detailed Findings
None.

## Cross-File Observations
Shares scene/reference directories and the `xna-diff.py` comparison tool with
`run-oracle-corpus-diff.sh` — the two scripts' divergent purposes (permanent gate vs. one-time
measurement) are consistently and clearly documented in both files.

## Missing or Weak Tests
N/A by design — not intended as a permanently-enforced test.

## Positive Findings
Honest, explicit framing of "this measures divergence, it does not gate on pixel-perfect parity" —
avoids the failure mode of either silently degrading a real gate's strictness or misleadingly
presenting a measurement tool as a pass/fail test.

## Final Assessment
No findings.
