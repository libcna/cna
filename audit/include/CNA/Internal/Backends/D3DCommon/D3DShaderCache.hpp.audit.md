# Audit: include/CNA/Internal/Backends/D3DCommon/D3DShaderCache.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3DCommon/D3DShaderCache.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard
- File type: C++ header (79 lines)
- Related implementation: `src/CNA/Internal/Backends/D3DCommon/D3DShaderCache.cpp` (same shard)
- XNA/FNA relevance: N/A directly (internal shader-object creation plumbing for the stock XNA effects)
- Graphics backend relevance: shared D3D11 shader-object factory for both D3D11 and (indirectly, via bytecode
  access) D3D12
- FNA reference: N/A
- Main related tests: none found exercising this cache directly

## Purpose

Declares `D3DShaderVariant` (17 enumerators, one per compiled HLSL shader pair) and four functions:
`GetVertexShaderBytecode`/`GetPixelShaderBytecode` (raw DXBC access, needed separately for
`ID3D11Device::CreateInputLayout`'s own bytecode requirement) and `CreateVertexShaderForVariant`/
`CreatePixelShaderForVariant` (actual `ID3D11VertexShader`/`ID3D11PixelShader` object creation).

## Executive Verdict

**Healthy.** Clear, complete, correctly-scoped declarations; the 17-variant enum was independently counted and
confirmed to match the 17 vertex/fragment shader pairs in `shaders/` exactly.

## Checklist Results

### API / XNA / FNA parity
N/A directly — internal plumbing.

### Architecture
Correctly documents its own deliberately narrow scope ("only proves/exposes the DXBC -> D3D11-shader-object path
... not a replacement for" full pipeline wiring) — verified accurate by cross-referencing the actual pipeline
wiring in `D3D11GraphicsBackend.cpp`/`D3D12GraphicsBackend.cpp`, which do consume this cache's output as described.

### Error handling
`CreateVertexShaderForVariant`/`CreatePixelShaderForVariant` are documented to return a null `ComPtr` (not throw)
on failure — a deliberate, explicit design choice matching "this project's existing D3D11 backend error-handling
style," independently verified consistent with the `.cpp` implementation.

### C++ correctness / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

None.

## Cross-File Observations

The 17-enumerator `D3DShaderVariant` list was cross-checked against the 17 `.hlsl` file pairs and the 17
`SHADERS` tuples in `compile_shaders_hlsl.py` — all three are in exact 1:1 correspondence, with no missing or
extra variant in any of the three places.

## Missing or Weak Tests

No dedicated test found exercising every `D3DShaderVariant` case through `CreateVertexShaderForVariant`/
`CreatePixelShaderForVariant` (e.g. asserting a non-null result for every valid variant on an initialized D3D11
device).

## Positive Findings

Explicit, accurate self-documentation of scope boundaries (DXBC-to-shader-object only, not full pipeline wiring)
prevents this file from being mistaken for more than it is.

## Final Assessment

No issues found.
