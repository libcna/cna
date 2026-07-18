# Audit: include/CNA/Internal/Backends/D3DCommon/D3DVertexFormatHelper.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3DCommon/D3DVertexFormatHelper.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard
- File type: C++ header (52 lines)
- Related implementation: `src/CNA/Internal/Backends/D3DCommon/D3DVertexFormatHelper.cpp` (same shard)
- XNA/FNA relevance: stride-keyed vertex-layout inference for `VertexPositionColor`/`VertexPositionTexture`/
  `VertexPositionColorTexture`/`VertexPositionNormalTexture`/`VertexPositionNormalTextureSkinned`/
  `VertexPositionNormalTangentTexture`/`VertexPositionNormalTangentTextureSkinned` and the stride-56
  skinned+Color variant
- Graphics backend relevance: `D3D11_INPUT_ELEMENT_DESC`/`D3D12_INPUT_ELEMENT_DESC` construction for both backends
- FNA reference: matches each named vertex struct's own `getVertexDeclarationStatic()` layout (documented, not
  independently re-derived from FNA in this pass, but internally cross-referenced against
  `EasyGLGraphicsBackend::ApplyLayout`'s equivalent case-56/68 conventions)
- Main related tests: none found exercising this table directly

## Purpose

Declares `InputElementsForStride`/`InputElementsForStrideD3D12`, both stride-keyed (16/20/24/32/48/52/56/68 bytes)
lookups returning a `D3D11_INPUT_ELEMENT_DESC*`/`D3D12_INPUT_ELEMENT_DESC*` array and its element count.

## Executive Verdict

**Healthy.** Complete, correctly-documented layout table with a well-reasoned "separate array, not
reinterpret_cast" design choice.

## Checklist Results

### API / XNA parity
The documented byte-offset table (lines 27-36) was cross-checked against the `.cpp` implementation and found to
match exactly for every one of the 8 strides.

### Architecture
The choice to maintain separate `D3D11_INPUT_ELEMENT_DESC`/`D3D12_INPUT_ELEMENT_DESC` arrays rather than a single
array reinterpret-cast between the two types is explicitly justified (D3D12 has no separate input-layout COM
object; more importantly, a `reinterpret_cast` across an SDK-defined struct boundary is fragile if a future SDK
version's layout diverges) — a defensively sound choice given the file's own stated verification discipline.

### C++ correctness / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

None.

## Cross-File Observations

See the `.cpp` report for the full per-stride offset verification.

## Missing or Weak Tests

No dedicated test found asserting every stride's `D3D11_INPUT_ELEMENT_DESC`/`D3D12_INPUT_ELEMENT_DESC` array
content against the corresponding named vertex struct's real `getVertexDeclarationStatic()` output.

## Positive Findings

Clear, byte-offset-annotated documentation of all 8 supported strides directly in the header, making the `.cpp`
implementation straightforward to verify without needing to consult the vertex-struct definitions separately.

## Final Assessment

No issues found.
