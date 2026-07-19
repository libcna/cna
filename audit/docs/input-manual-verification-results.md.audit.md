# Audit: docs/input-manual-verification-results.md

## Metadata
- Source file: `docs/input-manual-verification-results.md` (140 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown dated verification log
- XNA/FNA relevance: N/A directly — manual/hardware verification record for the Input namespace
- Main related tests: `SetPositionConvertsLogicalToWindowForLetterboxedRenderer`,
  `SetPositionHandlesLetterboxOffsetNotJustScale` (not yet audited in this session's test shards)

## Purpose
Records 2 dated verification entries (2026-07-04, 2026-07-06) of hardware- and platform-gated Input
checks the headless `CnaTests` suite cannot cover, plus a hardware-verification matrix (all cells
currently ⬜, no hardware available) and recording templates for future entries.

## Executive Verdict
Exceptionally honest by explicit design: "This log is deliberately honest: items that could not be
verified in a given environment are marked *not verified*, not silently passed." Every entry
correctly separates what WAS verified (via a specific unit test or a real-window automated check)
from what remains genuinely hardware/human-gated (rumble, sensors, light bar, real IME composition,
touch) — no cell is marked ✅ without a dated row backing it, consistent with the document's own
stated rule ("Never mark a cell ✅ without a dated row backing it").

## Checklist Results
- The 2026-07-04 entry's automated-baseline numbers are explicitly marked superseded by a
  2026-07-05 note pointing to `docs/input-build-and-test.md` for the current authoritative count —
  correct staleness handling rather than leaving two contradictory "current" counts in the
  document.
- The Wayland-specific finding (`SDL_GetGlobalMouseState` returns `(0,0)` under this compositor's
  security policy) is stated as a confirmed, reproducible platform behavior with a specific
  workaround identified (relative mouse mode / pointer lock as the supported path) — not a vague
  "Wayland doesn't work" claim.
- The hardware verification matrix (lines 98-115) correctly uses `n/a` for cells that are
  structurally inapplicable (e.g. trigger haptics for Xbox/Switch Pro controllers that don't support
  it) versus `⬜` for genuinely untested-but-applicable cells — a real, meaningful distinction rather
  than a blanket "not done" marker.
- The 2026-07-06 entry's ASan+UBSan result (314 green, 0 sanitizer errors, "the only leaks are
  third-party `libGLX_mesa`, not CNA input code") correctly attributes a known, external leak source
  rather than either hiding it or falsely treating it as a CNA regression.

## Detailed Findings
None.

## Cross-File Observations
Consistent with `input-pre-merge-checklist.md`'s (read in an earlier batch) own "Input stable"
release-gate requirement for a current, dated hardware-verification entry with a real controller
family/touchscreen/IME — this document's own matrix, still entirely ⬜, is honest, direct evidence
that gate has not yet been met, consistent rather than contradictory between the two files.

## Missing or Weak Tests
Not applicable — the document itself IS the record of what remains untested on real hardware; every
gap is already explicitly disclosed, not hidden.

## Positive Findings
The explicit "not verified, not silently passed" design principle, backed by a genuinely followed
practice throughout both entries (n/a vs ⬜ distinction, honest external-leak attribution, correct
staleness superseding), makes this one of the most trustworthy verification-status documents in the
`docs` shard.

## Final Assessment
No findings.
