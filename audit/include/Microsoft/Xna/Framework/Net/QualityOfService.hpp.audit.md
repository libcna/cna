# Audit: include/Microsoft/Xna/Framework/Net/QualityOfService.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Net/QualityOfService.hpp`
- Audit status: AUDITED (full read, 88 lines)
- Subsystem: `xna-net` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material to diff line-by-line, but the
  file's own comment quotes FNA's real internal constructor comment ("TODO: Everything below") —
  unverifiable against the local FNA tree (see shard-wide cross-cutting note) but plausible and
  consistent with FNA's documented all-networking-stubbed-out design.
- Main related tests: not independently located in this pass

## Purpose
Describes measured network quality (round-trip time, bandwidth) between the local machine and a
remote gamer.

## Executive Verdict
Correct and, notably, goes beyond FNA's acknowledged stub: a second `CreateInternal(TimeSpan)`
overload (Task 4.2) lets `ENetDiscoveryService` populate a *real* measured round-trip time from
actual LAN discovery query/reply timing, rather than only ever returning the always-zero,
`IsAvailable=true`-but-meaningless stub FNA's own constructor produces. The doc comment is explicit
that bandwidth stays unmeasured (`bytesPerSecondDownstream_`/`bytesPerSecondUpstream_` remain 0)
even on the measured overload, since UDP discovery has no established connection to sample
throughput from — an honest, disclosed scope boundary rather than a silently-incomplete field.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
`AvailableNetworkSession::getQualityOfServiceProperty()` exposes whichever `QualityOfService`
instance was supplied at construction — consistent with this type's two-tier stub/measured design.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`CreateInternal()`'s doc comment explicitly distinguishes "IsAvailable is still true even though
nothing is actually measured — this matches FNA's own reference... not a CNA gap," correctly
separating a genuine upstream stub from what would otherwise look like a suspicious always-true
flag on all-zero data.

## Final Assessment
No findings.
