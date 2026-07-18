# Audit: src/CNA/Internal/Backends/D3DCommon/D3DFormatMapping.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/D3DFormatMapping.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard
- File type: C++ implementation (60 lines)
- Related header: `include/CNA/Internal/Backends/D3DCommon/D3DFormatMapping.hpp` (same shard)
- XNA/FNA relevance: `SurfaceFormat`/`DepthFormat` -> `DXGI_FORMAT` mapping tables
- Graphics backend relevance: shared between D3D11 and D3D12
- FNA reference: FNA's own `XNAToFNA.cs`/D3D11 format-mapping tables (behavioral reference only, not source-shared)
- Main related tests: none found exercising this table directly

## Purpose

Implements the two mapping functions declared in the paired header via exhaustive `switch` statements over the
real `Microsoft::Xna::Framework::Graphics::SurfaceFormat`/`DepthFormat` enums.

## Executive Verdict

**Healthy.** Every `SurfaceFormat`/`DepthFormat` member independently cross-checked against its `DXGI_FORMAT`
target and found correct.

## Checklist Results

### API / XNA / FNA parity
Verified every `SurfaceFormat` case against its real semantic meaning: `Rg32` (two 16-bit UNORM channels) ->
`DXGI_FORMAT_R16G16_UNORM` (correct — not `R32G32`, a plausible name-based mistake this file avoids), `Rgba64`
(four 16-bit UNORM channels) -> `DXGI_FORMAT_R16G16B16A16_UNORM` (correct), `HdrBlendable` ->
`DXGI_FORMAT_R16G16B16A16_FLOAT` (correct — FNA's own HdrBlendable is platform-dependent but resolves to this
format on desktop D3D backends), the `NOXNA` `*EXT` extension formats (`ColorBgraEXT`, `ColorSrgbEXT`,
`Dxt5SrgbEXT`, `Bc7EXT`, `Bc7SrgbEXT`, `ByteEXT`, `UShortEXT`) all map to their obviously-correct DXGI equivalents.
`DepthFormat::Depth24` and `Depth24Stencil8` both correctly collapse to `DXGI_FORMAT_D24_UNORM_S8_UINT`, matching
the comment's claim and this project's own Vulkan backend's equivalent fallback (independently verified against
that backend's own depth-format candidate list description elsewhere in this audit).

### Behavioral correctness / Logic
Both functions correctly use `default: return DXGI_FORMAT_UNKNOWN;` rather than leaving undefined behavior for an
out-of-range ordinal.

### C++ correctness / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found — simple, correct, allocation-free switch-based dispatch.

## Detailed Findings

None.

## Cross-File Observations

Consistent with every other backend's own equivalent format-mapping table already reviewed in this audit (EasyGL,
Vulkan, WebGPU, Bgfx) — no divergent or D3D-specific format-mapping mistake found.

## Missing or Weak Tests

No dedicated test found asserting this mapping table's correctness independently (e.g. round-tripping every
`SurfaceFormat`/`DepthFormat` value and checking the resulting `DXGI_FORMAT` against a hand-written oracle table).

## Positive Findings

Complete, exhaustive coverage of every `SurfaceFormat`/`DepthFormat` enum member with no silent gaps.

## Final Assessment

No issues found.
