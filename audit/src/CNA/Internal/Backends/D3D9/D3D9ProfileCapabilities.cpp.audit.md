# Audit: src/CNA/Internal/Backends/D3D9/D3D9ProfileCapabilities.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/D3D9ProfileCapabilities.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ implementation
- XNA/FNA relevance: Implements real Reach/HiDef GraphicsProfile capability queries and enforcement, backed by actual D3DCAPS9/hardware format queries.
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Implements real Reach/HiDef GraphicsProfile capability queries and enforcement, backed by actual D3DCAPS9/hardware format queries.

## Executive Verdict

Healthy — a genuinely authentic, thorough profile-tier implementation.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
Faithfully models XNA's real documented Reach (2048 texture/512 cube/no volume textures/1 RT/9-format whitelist) vs. HiDef (4096/4096/256/4 RTs/20-format whitelist) capability tiers, with `MeetsHiDefFloorEXT()` checking real `D3DCAPS9` fields (shader model, texture/volume limits, simultaneous RT count, and — correctly — that POW2-only texture support is NOT set, since real HiDef requires unrestricted NPOT). `IsRenderTargetFormatSupportedByHardwareEXT()`/`IsBackBufferFormatSupportedByHardwareEXT()`/`ClampMultiSampleCountForFormatEXT()` all genuinely query the real adapter via `CheckDeviceFormat`/`CheckDeviceType`/`CheckDeviceMultiSampleType` rather than assuming support — consistent with this backend's own stated "pixel-for-pixel authenticity" goal.

## Detailed Findings

Faithfully models XNA's real documented Reach (2048 texture/512 cube/no volume textures/1 RT/9-format whitelist) vs. HiDef (4096/4096/256/4 RTs/20-format whitelist) capability tiers, with `MeetsHiDefFloorEXT()` checking real `D3DCAPS9` fields (shader model, texture/volume limits, simultaneous RT count, and — correctly — that POW2-only texture support is NOT set, since real HiDef requires unrestricted NPOT). `IsRenderTargetFormatSupportedByHardwareEXT()`/`IsBackBufferFormatSupportedByHardwareEXT()`/`ClampMultiSampleCountForFormatEXT()` all genuinely query the real adapter via `CheckDeviceFormat`/`CheckDeviceType`/`CheckDeviceMultiSampleType` rather than assuming support — consistent with this backend's own stated "pixel-for-pixel authenticity" goal.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

A genuinely authentic, hardware-query-backed implementation of XNA's real Reach/HiDef capability tiers — more thorough profile-fidelity than has been observed in most other backends checked.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
