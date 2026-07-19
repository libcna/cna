# Audit: docs/input-fna-fidelity.md

## Metadata
- Source file: `docs/input-fna-fidelity.md` (540 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (comprehensive FNA-fidelity + deviation record)
- XNA/FNA relevance: direct — the authoritative record of Input's behavior-vs-FNA fidelity,
  intentional deviations (with FNA line citations), and known gaps
- Related audit: `xna-input`/`tests-xna-input`/`cna-input` shards (this session, fully audited, no
  HIGH findings)

## Purpose
Per-area (Keyboard/Mouse/GamePad/Touch/Gestures/TextInputEXT/SDL-bridge) record of what matches
FNA, what intentionally deviates (with FNA source-line citations and rationale), and what remains a
known gap — including a "Definition of Done" gate and a verified-fact-vs-intended-behavior
convention.

## Executive Verdict
Exceptional. This is the single most rigorous FNA-fidelity document read across this entire
session's docs-shard batch. Every deviation is backed by a specific FNA source file and line range
(e.g. "`SDL3_FNAPlatform.cs:905-908`"), a concrete before/after behavior description, and — critically
— a named regression test pinning it. The document does not merely assert fidelity; it makes each
claim falsifiable. This is consistent with, and provides the underlying rationale for, this
session's own `xna-input`/`tests-xna-input` shard audits, which independently confirmed exhaustive
`Buttons`(31)/`Keys`(160) FNA parity with no HIGH findings.

## Checklist Results
- The `L-015` stick-axis-normalization fix (dividing the negative half by `32768` instead of `32767`,
  causing every non-endpoint negative sample to diverge from FNA by a small but real amount) is a
  genuinely subtle, well-diagnosed numerical bug — the endpoint-only agreement masking a
  systematic-except-at-boundaries divergence is exactly the kind of bug that's easy to miss without
  testing interior values, and the document explains this precisely.
- The gesture-timestamp deviation (P6-012) is a rare and intellectually honest case: the document
  identifies a **bug in FNA itself** (a ~10000x unit mismatch between `Environment.TickCount`
  milliseconds and `TimeSpan.FromTicks`'s 100ns-tick expectation) and deliberately does **not**
  replicate it, explaining precisely why replicating an upstream slip would trade a real correctness
  property for a compatibility benefit that doesn't exist (no game-facing contract depends on the
  absolute timestamp value). This is a mature, well-reasoned "diverge from FNA on purpose, when FNA
  itself is wrong and nothing depends on the wrongness" stance — a case this project's own CLAUDE.md
  "Behavior Fidelity" principle implicitly allows for but doesn't explicitly address, and this
  document handles it correctly by documenting the deviation clearly rather than either silently
  replicating the bug or silently fixing it without a note.
- The `INP-AUD-001`/`INP-AUD-003` fixes (touch-state frame-accuracy split into pure-read vs.
  `AdvanceTouchFrame()`; `GetCapabilities()`'s SDL-enumeration-vs-sticky-flag fix) are both real,
  specific, well-explained bug fixes with before/after behavior and a clear FNA-citation-backed
  correctness argument for the fix.
- The DEC-numbered accepted-deviation catalog (DEC-06 through DEC-20) is internally consistent
  across every area section that references a given DEC number — no contradiction found between,
  e.g., DEC-06's mouse-`ClickedEXT` description and its separate `TextInputEXT` citation.

## Detailed Findings
None.

## Cross-File Observations
- Directly consistent with `docs/input-backend.md`'s own architecture description (event-driven vs.
  FNA's poll-driven model) — the same framing, no contradiction, both read in this batch.
- The "Definition of Done" section's point 3 (Fake-SDL gamepad tests, Phase I15) is consistent with
  `docs/input-build-and-test.md`'s own test-count growth narrative (both read in this batch).
- The final "verified fact (✅) vs. intended behavior (🎯)" convention is the correct epistemic frame
  for reconciling this document's comprehensive fidelity claims with
  `docs/input-manual-verification-results.md`'s hardware-gated "not verified" entries (both read in
  this batch) — no tension between the two once this framing is applied.

## Missing or Weak Tests
The document itself is honest that Phase I15's fake-SDL-gamepad layer proves translation/bookkeeping
correctness but not real-hardware actuation — already correctly scoped as a manual/hardware gap, not
silently presented as fully covered.

## Positive Findings
The gesture-timestamp deviation write-up (P6-012) — correctly identifying and declining to replicate
a genuine bug in the upstream FNA reference itself, with clear reasoning for why — is the standout
example of mature engineering judgment in this entire docs-shard audit. Most FNA-fidelity work
(correctly) treats FNA as authoritative; this is a rare, well-justified, explicitly-flagged exception,
handled exactly the way CLAUDE.md's spirit ("document the intentional deviation... not in source
comments" for interface mismatches) would want for a *behavioral* deviation too.

## Final Assessment
No findings. The most rigorous and intellectually honest FNA-fidelity document in this session's
entire `docs/` shard audit.
