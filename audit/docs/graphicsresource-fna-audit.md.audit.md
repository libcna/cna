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
Healthy — a precise, falsifiable per-member table with clearly separated status categories
(✅/⚠️/❌) and explicit gap tracking with a named milestone (Task 212) for follow-up. Not
independently re-verified against current `GraphicsResource.cpp` source in this pass (out of scope
for a docs-shard audit; this project's own `xna-graphics` shard audit, completed earlier in this
session, covered `GraphicsDevice.cpp`'s raw-exception-throw findings but did not specifically
re-check `GraphicsResource`'s resource-list-registration gap) — flagged as unverified-in-this-pass,
not confirmed stale.

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
None — no claim in this document was found to be internally inconsistent or contradicted by other
material read in this session.

## Cross-File Observations
None specific to other files in this batch, though Gap 1's resource-list concern is conceptually
related to `graphics-resource-lifetime.md` (not yet read in this shard) — worth cross-checking when
that file is audited.

## Missing or Weak Tests
Not applicable — a documentation audit, not code; the document itself doesn't name specific test
coverage for its own 2 fixes.

## Positive Findings
Precise, falsifiable per-member status table with honest, calibrated gap-severity assessment.

## Final Assessment
No findings within this document's own content; its resource-list gap (Gap 1) was not independently
re-verified against current source in this pass and should be cross-checked if
`graphics-resource-lifetime.md` or `GraphicsDevice.cpp`/`GraphicsResource.cpp` are revisited.
