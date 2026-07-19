# Audit: scripts/xna-diff.py

## Metadata
- Source file: `scripts/xna-diff.py` (82 lines)
- Audit status: AUDITED (full read)
- Subsystem: `scripts` shard
- File type: Python script
- XNA/FNA relevance: this IS the actual pixel-comparison logic underlying
  `tools/xna-oracle`'s FNA-vs-CNA rendering comparison — foundational trust infrastructure for
  every "confirmed against FNA via the oracle" claim in this audit and in this project's own
  commit history (e.g. commit `74ad3bae`'s fog-formula fix cites divergent-pixel counts from
  exactly this script)
- Main related tests: none located directly for this script; consumed by
  `scripts/run-oracle-corpus-diff.sh`/`run-oracle-corpus-diff-easygl.sh` (not yet audited)

## Purpose
Diffs a real-XNA-4.0 reference PNG (produced by `tools/xna-oracle/Oracle.cs`) against a CNA-
rendered PNG (produced by `tools/xna-oracle/CnaOracleRender.cpp`), reporting the count of
differing pixels and the max per-channel delta, with an optional visual diff image.

## Executive Verdict
Correct, and closes a completeness gap explicitly flagged by the parallel `tools-xna-oracle` shard
fork's own audit this session: that fork could not independently verify this script (it lives
outside the `tools/xna-oracle/` directory the `tools-xna-oracle` manifest shard covers) despite it
being "as foundational as the two renderers." Direct review confirms the pixel-comparison logic is
sound: correct per-pixel/per-channel indexing (no transpose/off-by-one), a size-mismatch guard that
fails loudly with a clear message rather than silently comparing misaligned data, and a
default-zero tolerance with an explicit, strongly-worded comment warning against raising it "to
turn a red comparison green without a documented, per-scene reason."

## Checklist Results
- `xna.size != cna.size` (line 42) is checked and fails loudly (`return 1`) before any pixel
  comparison is attempted — correctly prevents a misleading/undefined comparison on mismatched
  dimensions rather than silently truncating or wrapping.
- The per-pixel loop (lines 55-65) correctly indexes both images at the same `(x, y)` coordinate
  pair for every comparison — no accidental flip/transpose that could silently compare the wrong
  pixel.
- `delta = max(abs(a[i] - b[i]) for i in range(4))` correctly considers all 4 RGBA channels, not
  just RGB — a full-channel comparison, consistent with the script's own `.convert("RGBA")` calls
  on both images.
- `passed = differing == 0` (line 71) is independent of the diff-image-writing path — the tool's
  pass/fail verdict doesn't depend on whether `--diff-out` was requested.
- The top-of-file docstring's warning about DXVK needing to be installed in *both* the XNA oracle's
  and CNA's Wine prefixes (lines 13-16) — otherwise the script "would silently measure a driver
  difference and blame CNA for it" — is a sharp, specific, correctly-identified false-positive risk
  for this exact kind of cross-implementation pixel diffing.

## Detailed Findings
None.

## Cross-File Observations
This is the file the parallel `tools-xna-oracle` shard fork's own audit this session explicitly
flagged as unverified ("the actual diff logic is as foundational as the two renderers" but "lives
under `scripts/`, outside this shard's 42-file manifest"). This review confirms the concern was
worth raising (it's genuinely foundational) but resolves it: the logic is correct.

## Missing or Weak Tests
No dedicated test file was located for this script in this pass; its correctness was verified by
direct code reading rather than by locating an existing regression test for it.

## Positive Findings
The default-zero-tolerance discipline and its accompanying warning comment are exactly the kind of
guardrail that keeps a diff tool honest over time — explicitly anticipating and warning against the
common failure mode where tolerance creeps upward to make failing comparisons pass rather than
fixing the underlying discrepancy.

## Final Assessment
No findings. Confirms the `tools-xna-oracle` shard's pixel-comparison foundation is sound.
