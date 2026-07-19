# Audit: docs/gdm-coverage.md

## Metadata
- Source file: `docs/gdm-coverage.md` (149 lines)
- Audit status: AUDITED (full read + cross-check against existing audit report)
- Subsystem: `docs` shard
- File type: Markdown documentation (FNA-vs-CNA feature coverage table)
- XNA/FNA relevance: documents `GraphicsDeviceManager` public API/event/property coverage
- Related audit: `audit/src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp.audit.md` (this
  session, confirmed HIGH finding), `audit/tests/Microsoft/Xna/Framework/GraphicsDeviceManagerTests.cpp.audit.md`

## Purpose
Compares CNA's `GraphicsDeviceManager` against the FNA reference: properties, static constants,
events, methods, protected virtuals, `IGraphicsDeviceManager` interface, NOXNA extensions, the
`ApplyChanges()` deviation, service registration, and property defaults. Last updated: "Task 230
(2026-06-27)."

## Executive Verdict
**Materially incomplete relative to a confirmed HIGH finding in this project's own already-audited
source.** This document's "Public events" table lists `DeviceCreated`/`DeviceDisposing`/
`DeviceReset`/`DeviceResetting`/`PreparingDeviceSettings` all as "✅ supported," and its own text
correctly describes each as raised from this class's own manual call sites (e.g. "`DeviceReset` —
Raised at end of `ApplyChanges()`"). That much is accurate. But this session's own
`GraphicsDeviceManager.cpp` audit (`audit/src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp.audit.md`)
confirms a HIGH finding this document never mentions: **`GraphicsDeviceManager` never subscribes to
`GraphicsDevice`'s own `DeviceResetting`/`DeviceReset`/`Disposing` events, so a real backend-detected
device-lost recovery (the independent callback path in `GraphicsDevice.cpp` lines 1459-1478) never
reaches `IGraphicsDeviceService` listeners at all** — a real gap between "this class can raise these
events when *it* decides to" (which this document correctly documents) and "this class forwards a
device's *own*, backend-triggered lifecycle events" (which it does not do, and which this document
is silent about). A reader relying on this coverage table alone would reasonably conclude
`GraphicsDeviceManager`'s device-lifecycle event story is fully closed; it is not.

## Checklist Results
- Direct spot-check of "Public properties" table: `getGraphicsProfileProperty()`/
  `setGraphicsProfileProperty()`, `IsFullScreen`, `PreferMultiSampling` (correctly marked ⚠ partial —
  "backend MSAA cannot change at runtime"), `PreferredBackBufferFormat`/`Width`/`Height`,
  `PreferredDepthStencilFormat` (correctly marked ⚠ partial), `SynchronizeWithVerticalRetrace`,
  `SupportedOrientations` — all consistent with what a `GraphicsDeviceManager.hpp`/`.cpp` audit would
  be expected to find; no contradiction with this session's own audit of that file.
- "Protected virtual methods" table's `CanResetDevice`/`FindBestDevice`/`RankDevices` rows (correctly
  marked ⚠ partial: "FNA throws `NotImplementedException`; CNA returns/no-ops a sensible default") —
  a reasonable, honestly-labeled deviation, not overclaimed as ✅.
- "ApplyChanges — key deviation from FNA" section is a detailed, accurate-reading explanation of why
  `PreferredBackBufferFormat`/`PreferredDepthStencilFormat`/`PreferMultiSampling` changes don't reach
  backend GPU resources until recreation — consistent in spirit with `PreferMultiSampling`'s own ⚠
  partial property-table row.

## Detailed Findings

### MEDIUM — Event coverage table omits a confirmed HIGH cross-cutting gap: `GraphicsDeviceManager` never forwards `GraphicsDevice`'s own backend-triggered lifecycle events
See Executive Verdict for the full description. This is a documentation-completeness gap, not a
fabricated claim — every individual cell in the table is accurate as far as it goes — but the
document's overall effect (a clean events table with no caveat) is silent about a confirmed,
already-audited HIGH-severity production gap in the exact class this document exists to describe.
Rated MEDIUM (doc-completeness) rather than the HIGH severity of the underlying code finding itself,
since the code defect is already tracked in its own dedicated audit report; this document's own gap
is that it doesn't point there.

## Cross-File Observations
Directly ties to `audit/src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp.audit.md`'s own
Cross-File Observations, which separately note: "`Graphics::GraphicsDevice`'s `deviceEventCallback`
(device-lost forwarding) is cited by its own comment as implemented by only one of ten backends" —
this document's silence on the forwarding gap compounds that already-confirmed narrowness, since a
reader of this coverage doc alone would not even know to look for it.

## Missing or Weak Tests
Consistent with this session's own finding (from the `tests-xna-framework-core` shard consolidation)
that `GraphicsDeviceManagerTests.cpp` has "zero real coverage (a 2-line stub file)" — this document's
own event-coverage table cannot be cross-checked against a real regression test for the
`DeviceCreated`/`DeviceReset`/etc. forwarding behavior, because no such test currently exists either.

## Positive Findings
Where this document does mark a deviation, it does so honestly (`⚠ partial` rows are genuinely
partial, not silently rounded up to ✅) — the gap found here is one of omission (a whole
cross-cutting concern not covered by this document's scope), not of a specific cell being
misrepresented.

## Final Assessment
One MEDIUM finding: this otherwise-accurate coverage table is silent about a confirmed HIGH
production gap (device-lost/reset event forwarding) in the very class it documents, dated
"Task 230 (2026-06-27)" — predating whatever later task confirmed that finding. Recommend adding an
explicit row or note cross-referencing the gap, following the same honesty already shown elsewhere
in this document's ⚠-partial rows.
