# Audit: docs/README.md

## Metadata
- Source file: `docs/README.md` (114 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation index
- XNA/FNA relevance: none directly; indexes docs that describe XNA/FNA-facing behavior

## Purpose
A curated index of the 58 files under `docs/`, grouping them by topic and flagging which are
known-current vs. historical/dated, so a reader doesn't have to open every file to find the
authoritative one for a given question.

## Executive Verdict
A well-designed, honest index: it explicitly states its own limitation ("entries not explicitly
flagged have not been individually re-verified... treat their currency with normal caution") rather
than implying blanket freshness, and consistently points to `NEXT.md §5`/`graphics-backend-feature-matrix.md`
as the tie-breaker when a dated snapshot conflicts with current status. Cross-checked several of its
specific claims against files this session directly audited (see Cross-File Observations) — all held up.

## Checklist Results
- Its claim that `easygl_bugs.md` is "dated Task 227 (2026-06-27), predates hundreds of subsequent
  EasyGL changes" and `graphics-compatibility-report.md`'s "5 confirmed bugs gate is closed" are both
  consistent with this session's own `xna-graphics`/backend-shard findings (no unresolved historical
  bug from that era was rediscovered as still-open).
- Its `coverage.md` note ("superseded for Graphics... kept for its non-Graphics namespace estimates")
  undersells how stale `coverage.md` actually is now — see `docs/coverage.md.audit.md` for the
  specific gap (its Net/GamerServices "stale row" self-corrections are themselves now further
  superseded by this session's own confirmed `xna-net`/`xna-gamerservices` shard completeness).
- Does not list `docs/cna_audio_deep_audit_2026-07-17.md` at all (a 2026-07-17 file, i.e. added after
  or around this index's own 2026-07-11 writing date) — not a defect (the index predates the file),
  but worth noting for whoever next refreshes this index.

## Detailed Findings

### LOW — Index predates several now-existing docs, most notably the audio deep-audit
`docs/cna_audio_deep_audit_2026-07-17.md`, `docs/cnatests-mingw-setenv-proposal.md`, and the
`devices-*.md` cluster's most recent updates all postdate or are contemporaneous with this index's
stated 2026-07-11 writing date and are not listed. Not a functional defect, but reduces the index's
usefulness for exactly the newest, most-in-flux docs a reader would most want a map for.

## Cross-File Observations
Directly corroborated by this session's own audit of `docs/coverage.md` (confirmed significantly more
stale than this index's own characterization) and `docs/cna_audio_deep_audit_2026-07-17.md` (confirmed
to be a historical snapshot whose findings have since been substantially actioned — see that file's
own audit report).

## Missing or Weak Tests
N/A — documentation index, not testable code.

## Positive Findings
The index's own explicit epistemic humility ("a file listed above... may still contain stale claims
that simply weren't hit by this pass... when in doubt, prefer `NEXT.md §5`") is exactly the right
posture for a hand-maintained index in a fast-moving codebase, and is a genuinely useful pattern this
audit's own findings validate rather than contradict.

## Final Assessment
One LOW finding: the index is itself now somewhat stale (predates several newer docs), consistent
with its own disclosed caveat. No factual errors found in what it does cover.
