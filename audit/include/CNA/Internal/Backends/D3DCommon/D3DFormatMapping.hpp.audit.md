# Audit: include/CNA/Internal/Backends/D3DCommon/D3DFormatMapping.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3DCommon/D3DFormatMapping.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard
- File type: C++ header (24 lines)
- Related implementation: `src/CNA/Internal/Backends/D3DCommon/D3DFormatMapping.cpp` (same shard)
- XNA/FNA relevance: `Microsoft::Xna::Framework::Graphics::SurfaceFormat`/`DepthFormat` -> `DXGI_FORMAT`
- Graphics backend relevance: shared format-mapping table for both D3D11 and D3D12
- FNA reference: N/A (a platform-format-mapping table, not XNA behavior itself)
- Main related tests: none found exercising this table directly

## Purpose

Declares `SurfaceFormatToDxgi(int)`/`DepthFormatToDxgi(int)`, taking the XNA enum's int ordinal (decoupling this
shared header from the XNA namespace, mirroring `IGraphicsBackend`'s own `surfaceFormat`-as-int convention) and
returning the corresponding `DXGI_FORMAT`.

## Executive Verdict

**Healthy.** Clear, minimal, correctly-documented declarations.

## Checklist Results

### API / XNA parity
Both functions correctly document their fallback behavior (`DXGI_FORMAT_UNKNOWN` for an unrecognized ordinal or
`DepthFormat::None`) — see the `.cpp` report for the implementation-level verification against the real
`SurfaceFormat`/`DepthFormat` enum members.

### Architecture
Good rationale for the `int`-not-enum parameter type, correctly cross-referencing `IGraphicsBackend`'s own
established convention rather than inventing a new one.

### C++ correctness / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found — pure, stateless mapping functions.

## Detailed Findings

None.

## Cross-File Observations

See the `.cpp` report for the full case-by-case verification.

## Missing or Weak Tests

No dedicated test found for this mapping table on either backend.

## Positive Findings

Clear documentation of the `Depth24` -> `DXGI_FORMAT_D24_UNORM_S8_UINT` fallback (D3D11 has no pure 24-bit-only
depth format), explicitly and correctly cross-referenced against this project's own Vulkan backend's identical
fallback choice — consistent cross-backend behavior, not an undocumented D3D-only quirk.

## Final Assessment

No issues found.
