# Audit: include/CNA/Internal/Backends/D3D11/D3D11StateObjectCache.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D11/D3D11StateObjectCache.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d11` shard
- File type: C++ header (91 lines)
- Related implementation: `src/CNA/Internal/Backends/D3D11/D3D11StateObjectCache.cpp` (same shard)
- XNA/FNA relevance: `BlendState`/`DepthStencilState`/`RasterizerState` -> D3D11 state-object caching
- Graphics backend relevance: D3D11-specific (built on `D3DCommon::D3DStateMapping`)
- FNA reference: FNA's own D3D11 blend/depth-stencil/rasterizer conventions (behavioral reference)
- Main related tests: `examples-tests-d3d11` (not yet audited)

## Purpose

Declares `D3D11BlendStateCache`, `D3D11DepthStencilStateCache`, `D3D11RasterizerStateCache` — three
per-distinct-XNA-state object caches for `ID3D11BlendState`/`ID3D11DepthStencilState`/`ID3D11RasterizerState`.

## Executive Verdict

**Mostly healthy — well-documented, with 2 confirmed, project-wide (not D3D11-introduced) interface-level gaps
honestly disclosed.**

## Checklist Results

### API / FNA parity
**F1 (MEDIUM, architecture-level):** `D3D11BlendStateCache`'s own doc comment discloses that
`IGraphicsBackend::ApplyBlendState()` carries no per-render-target color write mask, so every cached blend state
always uses `D3D11_COLOR_WRITE_ENABLE_ALL` regardless of XNA's real, settable `BlendState.ColorWriteChannels`.
**F2 (LOW, architecture-level):** `D3D11RasterizerStateCache`'s own doc comment discloses that
`ApplyRasterizerState()` carries no `MultiSampleAntiAlias` parameter, so every cached rasterizer state hardcodes
`MultisampleEnable = FALSE` — lower practical impact than F1 since this only affects line/point AA algorithm
selection, not MSAA render-target sampling (independently controlled via `DXGI_SAMPLE_DESC`). Both are honestly
self-disclosed as pre-existing `IGraphicsBackend` interface limitations affecting every backend, not something
introduced here — see `AUDIT_CROSS_CUTTING_FINDINGS.md` for the consolidated 3-instance (`AddressW` + these two)
pattern.
`D3D11DepthStencilStateCache`'s doc comment correctly explains why `referenceStencil` is deliberately excluded
from the cache key (it's a bind-time argument to `OMSetDepthStencilState()`, not part of `D3D11_DEPTH_STENCIL_DESC`
itself) — verified accurate against actual D3D11 semantics.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

**F1 (MEDIUM):** no per-target color write mask (architecture-level).
**F2 (LOW):** no `MultiSampleAntiAlias` flag (architecture-level, lower impact).

## Cross-File Observations

Third and fourth instances (with `D3D11SamplerCache`'s `AddressW` gap) of the same "`IGraphicsBackend`'s
`Apply*State()` interface omits fields the real D3D11/XNA state descriptions support" shape — see
`AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated test found exercising `BlendState.ColorWriteChannels`/`RasterizerState.MultiSampleAntiAlias` on this
backend specifically.

## Positive Findings

Accurate, correctly-reasoned documentation of the `referenceStencil` bind-time-vs-desc-time distinction — a subtle
D3D11 semantic detail correctly understood and applied.

## Final Assessment

Two MEDIUM/LOW architecture-level findings (not D3D11-introduced, shared across the whole `IGraphicsBackend`
interface), both honestly disclosed.
