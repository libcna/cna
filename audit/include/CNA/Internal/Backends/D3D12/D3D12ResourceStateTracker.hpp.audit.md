# Audit: include/CNA/Internal/Backends/D3D12/D3D12ResourceStateTracker.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D12/D3D12ResourceStateTracker.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d12` shard
- XNA/FNA relevance: N/A directly (internal GPU resource-state-transition plumbing)
- Graphics backend relevance: D3D12-specific
- FNA reference: N/A
- Main related tests: `examples-tests-d3d12` (not yet audited)

## Purpose

Declares `D3D12ResourceStateTracker`: the single source of truth for every `ID3D12Resource`'s current `D3D12_RESOURCE_STATES`, emitting a transition barrier only when a requested state genuinely differs from the last-known one.

## Executive Verdict

**Healthy — a well-reasoned, correctly and consistently applied design.**

## Checklist Results

### Architecture
The class-level comment correctly frames this as addressing "the single biggest source of 'silently wrong' bugs in a first D3D12 backend" (ad-hoc, scattered barrier calls) by centralizing all state tracking in one place — independently verified this discipline is followed everywhere: every `ID3D12Resource`-owning class in this backend (`D3D12Buffers`, `D3D12Textures`, `D3D12TextureCube`, `D3D12Texture3D`, `D3D12RenderTargets`, plus the backbuffer/depth-stencil resources in `D3D12GraphicsBackend` itself) calls `TrackResource()` immediately after creating its underlying resource — confirmed via a full grep across every `.cpp` file (17 call sites, all at resource-construction time).

### Memory/resource lifetime
**Considered risk, found mitigated**: keying by raw `ID3D12Resource*` could in theory let a destroyed-then-recreated resource at a reused memory address silently inherit a stale tracked state if some path forgot to call `TrackResource()` on the new object. `D3D12Buffers.cpp`'s own comment (line 108) explicitly acknowledges this exact concern and confirms it's handled: every construction path immediately re-registers its own resource, `TrackResource()`'s own contract is to overwrite any prior entry unconditionally — verified this holds for all 17 call sites, so the theoretical risk does not manifest given the codebase's actual, consistent usage pattern.

### C++ correctness / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

None — a theoretical raw-pointer-key risk was investigated and found correctly mitigated by consistent call-site discipline.

## Cross-File Observations

`Clear()`'s doc comment correctly anticipates device-lost recovery (DX-110) needing to drop all tracked state once every resource is being recreated from scratch — consistent with `D3D11InputLayoutCache::Clear()`'s identical forward-looking design in the sibling backend.

## Missing or Weak Tests

No dedicated test found exercising a genuine multi-transition resource-state sequence, or the redundant-barrier-skipped guarantee (`TransitionTo()`'s own `bool` return value, documented as "real proof for tests," doesn't appear to be asserted by any currently-registered test).

## Positive Findings

A textbook-correct centralized resource-state-tracking design, with the exact discipline needed to avoid the class of bug this project's own plan explicitly worried about — and independently confirmed consistently applied at all 17 real call sites, not just documented as a rule.

## Final Assessment

No issues found; a genuinely well-designed and consistently-applied D3D12-specific class.
