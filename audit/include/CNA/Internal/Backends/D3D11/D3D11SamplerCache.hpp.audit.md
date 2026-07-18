# Audit: include/CNA/Internal/Backends/D3D11/D3D11SamplerCache.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D11/D3D11SamplerCache.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d11` shard
- File type: C++ header (38 lines)
- Related implementation: `src/CNA/Internal/Backends/D3D11/D3D11SamplerCache.cpp` (same shard)
- XNA/FNA relevance: `SamplerState` -> `ID3D11SamplerState` caching
- Graphics backend relevance: D3D11-specific (uses `D3DCommon::TextureFilterToD3D11`/`TextureAddressModeToD3D11`)
- FNA reference: N/A directly
- Main related tests: `examples-tests-d3d11` (not yet audited)

## Purpose

Declares `D3D11SamplerCache::GetOrCreate()`, keyed by `(filter, addressU, addressV, maxAnisotropy)`.

## Executive Verdict

**Needs attention — one confirmed, project-wide architecture-level gap, honestly disclosed here.**

## Checklist Results

### API / FNA parity
**Confirmed HIGH-relevance architecture gap (already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`)**: this
header's own comment discloses that `IGraphicsBackend::ApplySamplerState()` carries no `addressW` parameter at
all — a real, project-wide interface limitation, not something introduced here — so `AddressW` (confirmed present
as a real, documented `SamplerState` property in `SamplerState.hpp`) is unenforceable via this cache regardless of
what the XNA-facing `SamplerState.AddressW` is actually set to. `D3D11SamplerCache.cpp` correctly implements the
disclosed workaround (reuse `AddressV` for `AddressW`) rather than silently defaulting to an arbitrary value.

### Architecture
Correct "distinct-state caching" discipline, consistent with `D3D11InputLayoutCache`/`D3D11StateObjectCache`'s
established convention in this same backend.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

**F1 (MEDIUM, architecture-level, not D3D11-specific):** `AddressW` unenforceable via the shared
`IGraphicsBackend` interface — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Cross-File Observations

See `D3D11StateObjectCache.hpp`'s own report for two more instances of the same shape (missing color-write-mask,
missing `MultiSampleAntiAlias`) — this is a recurring, `IGraphicsBackend`-interface-wide pattern, not isolated to
sampler state.

## Missing or Weak Tests

No test found anywhere in this codebase exercising a `Texture3D`/volume-texture scenario where `AddressW` differs
from `AddressV` in a way that would surface this gap.

## Positive Findings

Honest, accurate self-disclosure of a pre-existing interface limitation rather than silently working around it
without comment.

## Final Assessment

One MEDIUM, architecture-level (not D3D11-introduced) finding, honestly disclosed in the file's own comments.
