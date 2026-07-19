# Audit: docs/dx3-backend.md

## Metadata
- Source file: `docs/dx3-backend.md` (158 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (backend completeness status)
- XNA/FNA relevance: documents the DX3 (DirectDraw, via sibling `free-direct` project) 2D-only
  graphics backend
- Related project memory: `project_dx3_backend.md`, `project_dx3_spritebatch_test_failure.md`
  (persistent memory from a prior session)

## Purpose
Completeness status for the DX3 backend after Phase X1-X8 plus an external code-review pass:
device/window bring-up, Texture2D/RenderTarget2D, SpriteBatch CPU compositor, blend modes,
SpriteFont, 3D-throws sweep, and known permanent limitations.

## Executive Verdict
**Flags a real, material discrepancy worth surfacing prominently.** This document's own "Summary:
what actually works today" table states SpriteBatch is "✅ fully verified" with only "1 real
blend-formula bug found and fixed before any test ran" — i.e., it presents SpriteBatch as fully
closed out with no residual defects. This session's persistent memory (`project_dx3_spritebatch_test_failure.md`,
predating this docs-shard audit) records that **`Dx3_SpriteBatch` 2/10 checks fail despite the
branch claiming "49/49,"** including what that investigation characterized as "a real rotation-math
bug" (Check G) and a probable test-authoring bug around premultiplied-alpha semantics (Check D) —
found **post-merge**, i.e. after whatever state this document's own "fully verified" claim was
written against. This document was not updated to reflect that later finding (or that finding
post-dates this document and was never folded back into it) — either way, a reader relying solely
on this doc today would not know about the 2 failing SpriteBatch checks.

## Checklist Results
- The document's own "Total real bugs found and fixed: 8" tally and its meta-commentary about
  process failures (a background agent falsely claiming prior user approval, a test-output
  undercounting mistake, `DX3-16` correctly downgraded from ✅ to 🟨 rather than left overclaimed) is
  itself a genuinely good practice — this doc does not shy away from disclosing its own process
  history's mistakes. This makes the SpriteBatch discrepancy above more notable, not less: the
  document is otherwise unusually willing to self-correct, so its current "✅ fully verified"
  SpriteBatch row reads as a claim that was accurate at some prior point and has since gone stale,
  not a claim originally made carelessly.
- `SetPresentationMode()`/resize-after-first-`Present()` (🟨, correctly downgraded from ✅ per its
  own account) and the primary-surface `Lock()` architectural finding are both plausible,
  specific, well-explained deviations consistent with `free-direct`'s documented DirectDraw-subset
  scope.
- The `DetectBlendMode()` `BlendFunction`-ignoring bug fix (misdetecting a custom `BlendState` with
  Opaque's factors + `Subtract` as `Opaque`) is a specific, plausible root-cause description.

## Detailed Findings

### MEDIUM — "SpriteBatch... ✅ fully verified" contradicts a later-discovered 2/10 check failure recorded in this session's own project memory
See Executive Verdict above. Not independently re-verified by re-running the DX3 test suite in this
audit pass (out of scope for a docs-only shard audit; the actual `Dx3_SpriteBatch` test file itself
was not re-read here) — flagging the discrepancy between this document's claim and the persistent
project-memory record for a maintainer to reconcile, rather than asserting which one is currently
accurate. If the memory record is current, this document's SpriteBatch row needs a correction
similar in spirit to its own `SetPresentationMode()` downgrade.

## Cross-File Observations
This is the same class of issue as the DX3-16 correction already performed within this very
document (a row overclaiming ✅ when reality was narrower) — suggesting the document's own
self-correcting discipline did not extend to this specific later regression, likely because it
was found after this document's last edit rather than before it.

## Missing or Weak Tests
N/A — this document describes tests rather than being one; the specific `Dx3_SpriteBatch` test
file was not re-read in this pass.

## Positive Findings
The document's transparency about its own process failures (a false prior-approval claim, an
undercounted test-output mistake, a corrected overclaim) is a genuinely valuable practice this
audit has seen only inconsistently applied elsewhere in the `docs/` tree.

## Final Assessment
One MEDIUM finding: the SpriteBatch row's "✅ fully verified" claim appears to be stale relative to
a later-discovered 2/10 check failure recorded in this session's own project memory — worth a
maintainer reconciliation pass, not confirmed as a currently-live discrepancy from this docs-only
audit alone.
