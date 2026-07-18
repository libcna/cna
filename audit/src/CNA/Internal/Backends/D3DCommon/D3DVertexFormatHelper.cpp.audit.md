# Audit: src/CNA/Internal/Backends/D3DCommon/D3DVertexFormatHelper.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/D3DVertexFormatHelper.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard
- File type: C++ implementation (190 lines)
- Related header: `include/CNA/Internal/Backends/D3DCommon/D3DVertexFormatHelper.hpp` (same shard)
- XNA/FNA relevance: implements the stride-keyed vertex-layout tables
- Graphics backend relevance: shared D3D11/D3D12 input-layout descriptor tables
- FNA reference: N/A directly (a byte-layout table, not XNA behavior)
- Main related tests: none found exercising this table directly

## Purpose

Defines 8 `D3D11_INPUT_ELEMENT_DESC` arrays (strides 16/20/24/32/48/52/56/68) and 8 parallel
`D3D12_INPUT_ELEMENT_DESC` arrays, each dispatched by `InputElementsForStride`/`InputElementsForStrideD3D12`.

## Executive Verdict

**Healthy.** Every one of the 16 arrays (8 D3D11 + 8 D3D12) independently checked field-by-field and found to
byte-exactly mirror its D3D11/D3D12 counterpart, and every semantic/offset/format combination is internally
consistent with the layouts documented in `hlsl_shaders.hpp`'s corresponding `.hlsl` source `VSInput` structs read
earlier in this shard's own audit.

## Checklist Results

### API / XNA parity
Stride 52 (`VertexPositionNormalTextureSkinned`) checked against `skinned3d.vert.hlsl`'s own `VSInput` struct
(`POSITION0`/`NORMAL0`/`TEXCOORD0`/`BLENDWEIGHT0`/`BLENDINDICES0`) — offsets (0/12/24/32/48) match exactly. Stride
68 (`VertexPositionNormalTangentTextureSkinned`) checked against `pbr_skinned3d.vert.hlsl`'s `VSInput`
(`POSITION0`/`NORMAL0`/`TANGENT0`/`TEXCOORD0`/`BLENDWEIGHT0`/`BLENDINDICES0`) — offsets (0/12/24/40/48/64) match
exactly. Stride 56 (skinned + per-vertex Color) checked against `skinned_colored3d.vert.hlsl`'s `VSInput`
(`COLOR0` appended at offset 52 after the stride-52 fields) — matches exactly.

### Behavioral correctness / Logic
Every D3D11 array has an exactly-matching D3D12 counterpart (same semantic names, same `DXGI_FORMAT`s, same byte
offsets), differing only in the `D3D11_INPUT_PER_VERTEX_DATA`/`D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA`
enumerator-type spelling — independently verified for all 8 strides, not just a sample.

### C++ correctness / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found. `std::size()` (not a hardcoded element count) used consistently for every `count` output —
avoids a class of off-by-one bug this pattern is otherwise prone to.

## Detailed Findings

None.

## Cross-File Observations

The stride-52/56/68 offset tables here are internally consistent with the corresponding `VSInput` structs already
read and verified in this shard's `skinned3d.vert.hlsl`/`skinned_colored3d.vert.hlsl`/`pbr_skinned3d.vert.hlsl`
reports — no cross-file layout mismatch found anywhere in this shard.

## Missing or Weak Tests

No dedicated test found round-tripping every stride through this table and comparing against the named vertex
struct's own `getVertexDeclarationStatic()` output.

## Positive Findings

Consistent use of `std::size()` for element counts rather than hardcoded/derived-and-possibly-wrong literals;
complete, verified 1:1 D3D11/D3D12 parity for every stride.

## Final Assessment

No issues found.
