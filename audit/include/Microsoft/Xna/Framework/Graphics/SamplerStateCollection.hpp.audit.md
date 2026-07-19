# Audit: include/Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp` (36 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `Graphics/SamplerStateCollection.cs` (fully diffed)
- Main related tests: not independently located in this pass

## Purpose
A fixed-size (`MaxSamplers=16`) collection of `SamplerState`, indexable by slot, matching real XNA's `GraphicsDevice.SamplerStates`/`VertexSamplerStates`.

## Executive Verdict
Mostly correct, but structurally simpler than FNA's real implementation in one specific way that has a confirmed, real downstream consequence (see Detailed Findings): FNA's `SamplerStateCollection` tracks a `modifiedSamplers` dirty-flag array (set to `true` whenever a slot is written via its indexer), consumed elsewhere to lazily re-apply only the sampler slots a caller actually changed. This port has no equivalent tracking at all.

## Checklist Results
- Doxygen coverage: complete.
- `MaxSamplers=16` matches XNA's real texture-stage limit.
- No `NOXNA`-taggable extensions present.

## Detailed Findings

### LOW-MEDIUM — No per-slot dirty-tracking, unlike FNA's `modifiedSamplers` array; confirmed to cause unconditional full re-application of all 16 sampler slots every call
FNA's real `SamplerStateCollection` (`SamplerStateCollection.cs` lines 16-27, 33-50) is `internal`-constructed with a caller-supplied `bool[] modifiedSamplers` array shared with `GraphicsDevice`; every `this[index] = value` write sets `modifiedSamplers[index] = true`, and `GraphicsDevice`'s own state-application logic (not itself part of this file) uses that flag to skip re-verifying/re-applying samplers that haven't changed since the last draw. This port's `SamplerStateCollection` (this file) has no such flag array — `operator[]` returns a plain mutable `SamplerState&` with no side effect recorded on write.

Traced one concrete consequence (outside this batch's own scope, but directly relevant to characterizing this finding's severity — flagging for that file's own audit rather than auditing it here): `GraphicsDevice::applySamplerStatesToBackend()` (`GraphicsDevice.cpp`) unconditionally loops over and re-applies **all 16** sampler slots to the backend on every call, with no dirty-check at all. This is not a correctness bug (every applied value is always accurate/current), but it is a genuine, confirmed architectural simplification relative to FNA — a real (if likely modest) per-frame performance cost from re-verifying/re-uploading sampler state for slots that never changed, on every backend whose `ApplySamplerState()` call has non-trivial cost (e.g. any backend that does real driver-level state object lookups/creation per call rather than a cheap register write).

- Severity: LOW-MEDIUM (no correctness impact; a real, confirmed performance-architecture divergence from FNA)
- Location: this file (missing dirty-tracking) + `GraphicsDevice.cpp`'s `applySamplerStatesToBackend()` (the confirmed consumer of the gap — that file's own audit should record this too)

## Cross-File Observations
Independently re-confirms this audit's already-recorded cross-cutting finding that `IGraphicsBackend::ApplySamplerState()` never receives `AddressW`: `applySamplerStatesToBackend()`'s call site forwards only `filter`/`addressU`/`addressV`/`maxAnisotropy`, confirming the gap from the backend-interface side as well as the `SamplerState`-class side (see `SamplerState.hpp.audit.md`).

## Missing or Weak Tests
Not independently located in this pass; a test asserting that unchanged sampler slots are NOT redundantly re-applied would be a reasonable regression test if the dirty-tracking gap above is ever addressed.

## Positive Findings
The simpler, flag-free design is easier to reason about and cannot itself introduce a stale-sampler-state bug (since it always re-applies everything) — a reasonable trade-off, just one with an unstated performance cost relative to FNA's more optimized original.

## Final Assessment
One LOW-MEDIUM finding: no per-slot dirty-tracking, a confirmed (not merely theoretical) architectural simplification vs. FNA with a real, if likely modest, performance cost and zero correctness impact.
