# Audit: docs/graphicsresource-fna-audit.md

## Metadata
- Source file: `docs/graphicsresource-fna-audit.md` (85 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown per-class FNA-fidelity audit
- XNA/FNA relevance: direct — a member-by-member `GraphicsResource` vs FNA comparison
- Main related tests: none named directly; implies coverage via Task 211's own fixes

## Purpose
Member-by-member comparison of `Microsoft::Xna::Framework::Graphics::GraphicsResource` against
FNA's `GraphicsResource.cs`, documenting 2 real, already-applied fixes (Task 211: `ToString()`
missing the `Name` check; `Dispose(bool)` event-before-flag ordering) and 2 open gaps (no device
resource-list registration; no `GraphicsDeviceResetting()` callback).

## Executive Verdict
**Update (follow-up cross-check against `docs/graphics-resource-lifetime.md`, now read): Gap 1 is
confirmed stale.** This document's "Gap 1 — Device resource tracking" states flatly: "**CNA
status**: No resource list exists on `GraphicsDevice`. Resources are not notified on device reset or
device disposal." `docs/graphics-resource-lifetime.md` (audited separately this session) describes,
in specific unqualified detail, a `GraphicsDevice::resources_` tracking list with registration via
`AddResourceReference`/deregistration via `RemoveResourceReference`, a documented safe-disposal
order, and `ResourceCreated`/`ResourceDestroyed` events raised around that same list — none of which
this session's `xna-graphics`/`cna-graphics` shard audits flagged as fictional or aspirational. This
document's own phrasing ("Tracking: Task 211 documents; Task 212 is the correct milestone to
address") reads as describing a gap *about to be* closed, not a permanent one — the most plausible
explanation is that Task 212 (or a later task) built the list and this document was simply never
updated afterward, the same "planning-doc-not-updated-after-the-work-landed" pattern found
elsewhere in this shard. Aside from this now-resolved cross-check, the document otherwise remains a
precise, falsifiable per-member table with clearly separated status categories (✅/⚠️/❌).

## Checklist Results
- Both "Fixes Applied in Task 211" entries include the exact FNA source line and the exact
  before/after CNA behavior — a genuinely falsifiable description, not a vague "fixed" note.
- Gap 1 (no device resource-list) correctly identifies its own downstream consequence
  (`GraphicsDevice.Dispose()`/`Reset()` can't notify tracked resources) and honestly rates its
  current impact as "Low in current test-driven usage where the device outlives all resources" —
  a calibrated severity assessment, not an alarmist one.
- The Intentional C++ Deviations table (copy-constructor addition, no `GC.SuppressFinalize`, raw
  `Tag` pointer, no finalizer guard) are all standard, expected C++-vs-C# port deviations with clear
  one-line justifications — consistent with the general deviation patterns seen throughout this
  audit's other FNA-fidelity findings.

## Detailed Findings

### MEDIUM — "Gap 1: No resource list exists on GraphicsDevice" contradicts `docs/graphics-resource-lifetime.md`'s detailed, unqualified description of exactly such a list, and is not corroborated by this session's own source-level Graphics audits
See the updated Executive Verdict above. This document should either be updated to reflect that
Gap 1/Gap 2 (the dependent `GraphicsDeviceResetting()` callback) were closed, or explicitly marked
historical/superseded the way several other dated documents in this shard correctly are (e.g.
`docs/graphics-compatibility-report.md`'s own "do not treat as current" banner). As currently
written, with no date-relative caveat beyond "Task 211" in its own header, a reader has no signal
that this is describing a since-resolved state.

## Cross-File Observations
Direct mirror of the finding recorded from `docs/graphics-resource-lifetime.md`'s own perspective
(audited separately this session, in the same `docs` shard) — see that report for the corroborating
detail. This resolves the to-do this report itself flagged in its original pass.

## Missing or Weak Tests
Not applicable — a documentation audit, not code; the document itself doesn't name specific test
coverage for its own 2 fixes. Whether a resource-list-driven `GraphicsDeviceResetting()` callback
test exists was not checked (this document's own Gap 2 says the callback doesn't exist at all —
the same claim now under dispute).

## Positive Findings
Precise, falsifiable per-member status table with honest, calibrated gap-severity assessment (Gap 1
was itself rated "Low" impact even before this cross-check found it stale — a calibrated, not
alarmist, original assessment).

## Final Assessment
One MEDIUM finding, resolved via cross-check: this document's "Gap 1" (no `GraphicsDevice`
resource-tracking list) is stale, contradicted by `docs/graphics-resource-lifetime.md` and not
corroborated by this session's own source-level Graphics-namespace audits. Recommend reconciling the
two documents — most likely by marking this one's Gap 1/Gap 2 as resolved-since-Task-212, with a
forward reference to `docs/graphics-resource-lifetime.md` as the current description.
