# Audit: docs/devices-api-coverage.md

## Metadata
- Source file: `docs/devices-api-coverage.md` (376 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown flat per-member API-coverage lookup table
- XNA/FNA relevance: exhaustive `Microsoft::Devices`/`Microsoft::Devices::Sensors` member-by-member
  Real/NOXNA/confidence classification

## Purpose
A standalone per-member reference distinguishing real XNA 4.0/WP7 API surface from CNA-only (`NOXNA`)
extensions across `VibrateController`, `SensorBase<T>`, `Accelerometer`/`Gyroscope`/`Compass`/`Motion`,
their reading structs, and exceptions/enums — with an explicit confidence legend (archived-MSDN-page
citation = High; MonoGame-cross-check/inference = Medium).

## Executive Verdict
The most rigorously self-auditing document found in this entire `docs` corpus. `DEV-API-001`'s
re-verification pass found and fixed **4 genuine "Extra-unmarked" bugs** (CNA-only additions
incorrectly presented as real XNA API without a `NOXNA` tag: `TimeBetweenUpdatesChanged`,
`operator==`/`operator!=`, `ToString()`, `GetHashCode()` across 5 reading structs) and one genuinely
real bug in `AccelerometerReadingEventArgs` (public setters that don't exist in the real, get-only
API) — each resolved with a direct citation to an archived MSDN "previous-versions" page, not
assumed or pattern-matched.

## Checklist Results
- Cross-checked the `Dispose(bool)` "public override" row in the "Cross-cutting members" table
  against this session's own confirmed MEDIUM finding (all four sensor classes declare
  `Dispose(bool)` `public` when the base `SensorBase<T>` correctly declares it `protected`) — **this
  document's own table states `Dispose(bool)` is "Real (public override)"**, which is the same
  visibility-mismatch this session's `microsoft-devices` shard audit flagged as a real MEDIUM finding
  (the base class declares it `protected`, matching the standard C# `IDisposable` dispose pattern; the
  four subclasses incorrectly widen it to `public`). This document does not flag the mismatch — its
  own row conflates "the base pattern is a real, project-wide C# `IDisposable` convention" (true) with
  "this specific `public` visibility on the four subclasses is correct" (the confirmed bug this
  session found). See Detailed Findings.
- The `AccelerometerReadingEventArgs` setter-removal finding (`READINGS-002`) is independently
  consistent with this session's own read of the class during the `microsoft-devices`/
  `tests-microsoft-devices` shard audits — no contradiction.
- The `SensorBase<T>::TimeBetweenUpdatesChanged` `NOXNA`-tagging fix is consistent with this session's
  own confirmed-clean review of `SensorBase.hpp`.

## Detailed Findings

### LOW — "Cross-cutting members" table's `Dispose(bool)` row does not flag the same public-vs-protected visibility bug this session confirmed
This document's own table (row: `Dispose(bool)` (public override) | Real | High | "Matches the
standard C# `IDisposable` dispose pattern CNA uses project-wide, not WP7-specific") treats the public
visibility as simply correct/expected, without noting that CNA's own project-wide `IDisposable`
convention (per `CLAUDE.md`) declares `Dispose(bool)` `protected`, not `public` — exactly the
convention `SensorBase<T>`'s own base-class declaration follows (`protected: virtual void
Dispose(bool disposing)`, confirmed directly this session). This document's "Wrong signature/visibility"
tracking mechanism (used elsewhere in the same file for two other genuine findings) did not catch
this one, even though the file's own "Cross-cutting members" table is precisely the place it would
belong. Not a defect in the document's methodology in general (which is otherwise excellent) — simply
a gap this specific row didn't catch, now closed by this session's independent `microsoft-devices`
shard finding.

## Cross-File Observations
This document's own table is the most detailed, most citation-backed API surface reference in this
whole corpus — a natural candidate for a follow-up correction adding the `Dispose(bool)`
public-vs-protected finding this session confirmed, using the exact same "Wrong signature/visibility"
category this file already has a working convention for.

## Missing or Weak Tests
The document itself references test coverage precisely (e.g. "every getter on every struct is
exercised by that struct's own `ParameterizedConstructorStoresValues` test") — consistent with this
session's own `tests-microsoft-devices` shard findings; no gap identified beyond the one Detailed
Finding above.

## Positive Findings
The citation-driven verification methodology (fetching an archived MSDN "previous-versions" page
directly for every Real/NOXNA classification, rather than trusting a prior pass's table) is the
single best evidentiary standard found in this entire `docs` corpus, and is directly analogous to
this audit session's own practice of verifying claims against current source rather than trusting
summaries.

## Final Assessment
One LOW finding: this document's own "Cross-cutting members" table doesn't flag the
`Dispose(bool)` public-vs-protected visibility mismatch this session's `microsoft-devices` shard audit
separately confirmed as a real MEDIUM production-code finding — a genuine, if minor, gap in an
otherwise exceptionally rigorous API-coverage reference.
