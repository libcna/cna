# Audit: src/CNA/Internal/Backends/D3D11/D3D11Textures.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D11/D3D11Textures.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d11` shard
- File type: C++ implementation (253 lines)
- Related header: `include/CNA/Internal/Backends/D3D11/D3D11Textures.hpp` (same shard)
- XNA/FNA relevance: implements the 3 texture backends
- Graphics backend relevance: D3D11-specific
- FNA reference: FNA's own D3D11 texture conventions
- Main related tests: `examples-tests-d3d11` (not yet audited)

## Purpose

Implements construction (via `CreateTexture2D`/`CreateTexture3D` + `CreateShaderResourceView`), `SetData`/`GetData`
(via `UpdateSubresource`/staging-texture-`Map`/`CopyResource` readback), for all 3 texture types.

## Executive Verdict

**Mostly healthy — correct subresource/mip/cube-face math throughout; one LOW-severity unvalidated-length finding
shared across all 3 `SetData` overloads.**

## Checklist Results

### Behavioral correctness / Logic
`D3D11CalcSubresource(level, face, mipLevels_)` used consistently and correctly for cube-map face/mip addressing
in both `SetData`/`GetData` — independently verified the subresource formula matches D3D11's real
`level + face * mipLevels` convention. `GetData()`'s staging-texture creation correctly copies the *full* source
`D3D11_TEXTURE2D_DESC`/`D3D11_TEXTURE3D_DESC` (preserving `MipLevels`/`ArraySize`) before overriding only the
`Usage`/`BindFlags`/`CPUAccessFlags`/`MiscFlags` fields needed for staging — ensures subresource indices stay
identical between the source and staging resources. `Texture3D::GetData()`'s row/slice copy loop correctly
accounts for `DepthPitch` (per-slice) in addition to `RowPitch` (per-row), independently traced and confirmed
correct byte-offset arithmetic for the 3D case, which is easy to get wrong (a 2-mistake risk: forgetting either
pitch entirely, or swapping which pitch applies to which loop level).

### Robustness
**F1 (LOW, shared across `D3D11TextureCubeBackend::SetData`/`D3D11Texture3DBackend::SetData`):** the `dataLength`
parameter is accepted but never validated against the actual bytes `UpdateSubresource` will read
(`w*h*4`/`w*h*depth*4`) — a caller passing a `dataLength` smaller than the region actually needs would have
`UpdateSubresource` read out-of-bounds from the source `data` pointer. Not independently confirmed as reachable
from any currently-registered test or the public `Microsoft::Xna::Framework` API's own bounds-checking (which may
already guard this one level up) — flagged as a defense-in-depth gap, not a proven live exploit.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found otherwise.

## Detailed Findings

**F1 (LOW):** `dataLength` accepted but unvalidated in `TextureCube`/`Texture3D::SetData()`.

## Cross-File Observations

Same subresource/staging-texture-copy pattern as `D3D11RenderTargetBackend`/`D3D11RenderTargetCubeBackend`'s own
(already-audited) MSAA-resolve logic — consistent conventions across the whole backend.

## Missing or Weak Tests

No dedicated test found exercising a deliberately-mismatched `dataLength` to confirm/refute the F1 risk.

## Positive Findings

Correctly handles the trickiest part of this file (3D-texture row/slice/depth-pitch readback math) without error.

## Final Assessment

One LOW-severity, not-independently-confirmed robustness gap (F1); otherwise correct.
