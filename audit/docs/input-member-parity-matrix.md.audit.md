# Audit: docs/input-member-parity-matrix.md

## Metadata
- Source file: `docs/input-member-parity-matrix.md` (708 lines)
- Audit status: AUDITED (full read of header/summary + start and end sections; large generated
  table format sampled for internal consistency rather than read line-by-line throughout, given its
  explicitly machine-generated, non-hand-editable nature)
- Subsystem: `docs` shard
- File type: Markdown, machine-generated report
- XNA/FNA relevance: member-level FNA-parity matrix for every public `Microsoft::Xna::Framework::Input`
  (+`::Touch`) type
- Related audit: `docs/input-public-api-frozen.md`/`docs/input-test-coverage.md` (both audited in
  this batch), `xna-input` shard (this session)

## Purpose
Auto-generated (`tools/input_parity/gen_input_parity_matrix.py`) per-type, per-member table listing
each member's tier tag (STRICT/EXT/NOXNA) and whether it has an FNA counterpart (`yes`/`internal`/
`no`), for every type in the `GamePad`, `Keyboard`/`Mouse`, and `TouchPanel`/`Touch` clusters.

## Executive Verdict
Trustworthy by construction: explicitly self-identified as generated ("Do not hand-edit; re-run the
generator"), and its own "Review summary" makes a strong, specific, and — per the sampled rows —
accurate claim: "No STRICT/EXT member is missing an FNA counterpart, and no FNA public member is
missing a CNA counterpart," with exactly 4 named, explicitly-scoped exceptions (`TouchCollection`'s
`IList<T>`/`IEnumerator`/`IDisposable` plumbing, correctly not mirrored by design in CNA's value-type
collection). This is consistent with this session's own `xna-input` shard audit, which found no
member-level parity gap.

## Checklist Results
- Sampled the `Buttons` enum table (all 21 members, all STRICT/`yes`) against this session's own
  `xna-input` shard's confirmed exhaustive `Buttons` parity — consistent, no discrepancy.
- Sampled the `TouchLocation`/`TouchLocationState`/`TouchPanel`/`TouchPanelCapabilities` tables (end
  of file) against `docs/input-public-api-frozen.md`'s own per-member tier tags for the same types
  (audited alongside this file) — tags match exactly member-for-member where both documents cover
  the same type (e.g. `TouchPanel`'s `NOXNA static void EnqueueGesture(...)` tagged `NOXNA`/`internal`
  identically in both documents).
- The document's own caveat ("Rows flagged above are heuristic (name-level) and may be false
  positives... review each against the .cs before acting") is an honest, correctly-scoped disclosure
  of the generator's own methodology limits — not overclaiming perfect precision for a heuristic
  name-matching tool.

## Detailed Findings
None found in the sampled portions; given the file's generated, non-hand-editable nature and its
demonstrated consistency with the independently-authored `docs/input-public-api-frozen.md` on every
spot-checked type, a full line-by-line read of all 708 lines was judged lower-value than confirming
representative consistency — flagging this scoping choice transparently rather than claiming
exhaustive verification.

## Cross-File Observations
Fully consistent with `docs/input-public-api-frozen.md`'s tier tags on every type sampled — expected,
since both are ultimately derived from (or hand-maintained against) the same public headers, but
worth confirming they haven't drifted apart, which they have not.

## Missing or Weak Tests
N/A — this document is itself a coverage/parity report; its correctness depends on
`tools/input_parity/gen_input_parity_matrix.py` being re-run per `docs/input-pre-merge-checklist.md`'s
own gate (already read in this batch), not on a separate test suite.

## Positive Findings
The heuristic-false-positive caveat, combined with a demonstrated track record of actually matching
the independently-maintained golden-signature document on every sampled type, makes this a
trustworthy generated artifact rather than a "generated so presumably fine" assumption.

## Final Assessment
No findings in the sampled portions. Consistent with `docs/input-public-api-frozen.md` and this
session's own `xna-input` shard audit.
