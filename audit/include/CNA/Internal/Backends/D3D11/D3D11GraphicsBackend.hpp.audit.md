# Audit: include/CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d11` shard
- File type: C++ header (379 lines) — the largest header in this backend
- Related implementation: `src/CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.cpp` (same shard, 1846 lines,
  scoped-depth review — see that report)
- XNA/FNA relevance: implements the full `IGraphicsBackend` contract for D3D11 — device/swap-chain lifecycle,
  clear/present, all resource-creation factories, all `Apply*State`/`Draw*` entry points, `SpriteBatch` factory
- Graphics backend relevance: this is the backend's central class
- FNA reference: FNA's own D3D11 device/resource lifecycle conventions
- Main related tests: `examples-tests-d3d11` (not yet audited)

## Purpose

The full `D3D11GraphicsBackend` declaration: device/swap-chain/window-size resource groups (explicitly, correctly
separated per the file's own "design decision 11" three-lifetime-group split), every `IGraphicsBackend` override,
and a large set of lazily-created, persistent, "grow never recreate" per-effect-variant constant buffers.

## Executive Verdict

**Healthy.** Exceptionally well-documented; the three-resource-lifetime-group design and per-variant constant-buffer
strategy are both clearly explained and consistent with the actually-implemented `.cpp` behavior already
cross-checked in this audit.

## Checklist Results

### Architecture
The three-group resource-lifetime split (device / swap-chain / window-size) is a genuinely clean design, each
group's recreation trigger explicitly documented (device: only device-removed recovery; swap-chain: created once,
`ResizeBuffers()` reused; window-size: every resize AND device-removed recovery). Independently cross-checked this
matches the actual constructor call sequence (`CreateDeviceResources()` → `CreateSwapChainResources()` →
`CreateWindowSizeDependentViews()`) already read in the `.cpp` file.
`currentCustomRT_`/`currentMRTTargets_` tracking (lines 240-260) and their accompanying `FlushPendingMRTResolveEXT()`
correctly document a genuine, deliberate bugfix (DX-143: MRT sets weren't finalized — MSAA resolve/mip regen —
when replaced/unbound) — independently verified this fix is real and correctly wired in the `.cpp` file's
`SetRenderTargets()` implementation (see that report).
The large family of per-variant `GetOrCreate*ConstantBufferEXT()` declarations (colored/textured/lit/alpha-test/
dual-texture/env-map/skinned/PBR — 12 distinct persistent buffers) is consistent with, and a correct C++-side
mirror of, the equally-large family of distinct HLSL cbuffers independently verified in the `backend-d3dcommon`
shard's own audit — every named buffer here corresponds to a real, already-reviewed HLSL `cbuffer` declaration.

### Confirmed absence of the EasyGL-class window-registry bug
`GetWindowInternal()`/no `RegisterForWindow` override anywhere in this class (independently confirmed via `grep`
across the whole `.cpp` file) — this backend does not participate in `IGraphicsBackend`'s static window registry
at all, so it cannot share the already-confirmed EasyGL dangling-pointer bug (see `AUDIT_CROSS_CUTTING_FINDINGS.md`).

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found in the declarations themselves.

## Detailed Findings

None new in this file (see the `.cpp` report for the findings this class's implementation contributes: the
already-recorded `SkinnedEffect` missing-`EmissiveColor` gap, confirmed at the C++ constant-buffer-fill level
here too).

## Cross-File Observations

Every constant-buffer struct named here (`D3DPerDrawConstants`, `D3DFogConstants`, `D3DLightingConstants`,
`D3DAlphaTestConstants`, `D3DBoneConstants`, `D3DSkinnedExtraConstants`, `D3DEnvMapPerDrawConstants`,
`D3DEnvMapConstants`, `D3DSprite2DConstants`, `D3DPbrPerDrawConstants`, `D3DPbrLightConstants`) was already
independently verified correct (size/offset `static_assert`s, cross-checked against the real HLSL) in the
`backend-d3dcommon` shard's own audit of `D3DConstantBuffers.hpp`.

## Missing or Weak Tests

No dedicated test found in this audit so far exercising this backend's device-removed recovery path, resize
handling, or the full stock-effect variant matrix.

## Positive Findings

Exceptionally clear "design decision" documentation throughout (resource-lifetime groups, MRT-finalization
bugfix history, capability/policy separation for presentation) — one of the best-documented backend headers
reviewed in this audit.

## Final Assessment

No issues found in this file; correctly and consistently built on the already-verified `backend-d3dcommon`
infrastructure.
