# Audit: include/CNA/Internal/Net/ENetLibrary.hpp

## Metadata
- Source file: `include/CNA/Internal/Net/ENetLibrary.hpp`
- Audit status: AUDITED (full read, 27 lines)
- Subsystem: `cna-internal-core` shard (Net)
- File type: C++ header
- XNA/FNA relevance: N/A -- NOXNA, internal ENet library lifecycle management behind `NetworkSession`'s
  SystemLink transport
- Main related tests: not independently located in this pass

## Purpose
Declares process-wide, lazy, thread-safe `enet_initialize()` invocation, never torn down (matching the
project's existing "no .NET AppDomain.ProcessExit equivalent" precedent).

## Executive Verdict
Healthy.

## Checklist Results
### Observation: SPDX license header diverges from project convention
Every file in this Net subsystem (`ENetLibrary`, `ENetHostHandle`, `ENetDiscoveryService`,
`NetDiscoveryProtocol`, `NetPacketCodec`, `ENetBackend`) uses `// SPDX-License-Identifier: MIT` plus an
explicit `// Copyright (c) Robert Vokac and contributors` line, while every other CNA-original NOXNA file
audited elsewhere in this repository (`Json.hpp`, the Media subsystem, `GltfImportCore`, `PbrMaterial.hpp`,
etc.) uses only `// SPDX-License-Identifier: MS-PL` with no separate copyright line. This may be an
intentional choice (ENet itself is MIT-licensed, and licensing CNA's ENet-integration code under the same
terms is a defensible position), but it is a real, project-wide inconsistency worth an explicit, recorded
decision rather than an unremarked accident -- especially since `include/CNA/Misc.hpp` (a third file audited
earlier this shard) has no SPDX header at all, meaning this repository currently has three different header
conventions in play across its own NOXNA/CNA-original code.

## Detailed Findings
None rising to actionable severity (see the SPDX observation above, also documented in this shard's
cross-cutting notes).

## Cross-File Observations
Correctly reused by every other Net file needing ENet initialized (`ENetHostHandle::CreateHost`/
`CreateClient`, `ENetDiscoveryService::EnsureSocket`).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct use of a C++11 function-local static ("magic static") for thread-safe lazy init; correctly relies
on the standard's own retry-on-throw guarantee for function-local statics (a failed `enet_initialize()` can
be retried on a later call, rather than permanently poisoning the process).

## Final Assessment
No issues found; one repository-wide SPDX-header-convention inconsistency noted (shared across this whole
subsystem, see `AUDIT_CROSS_CUTTING_FINDINGS.md`).
