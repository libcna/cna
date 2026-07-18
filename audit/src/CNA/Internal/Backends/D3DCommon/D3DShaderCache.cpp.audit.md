# Audit: src/CNA/Internal/Backends/D3DCommon/D3DShaderCache.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/D3DShaderCache.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard
- File type: C++ implementation (102 lines)
- Related header: `include/CNA/Internal/Backends/D3DCommon/D3DShaderCache.hpp` (same shard)
- XNA/FNA relevance: N/A directly
- Graphics backend relevance: wires `hlsl_shaders.hpp`'s embedded DXBC bytes to `D3DShaderVariant` and to real
  `ID3D11VertexShader`/`ID3D11PixelShader` objects
- FNA reference: N/A
- Main related tests: none found exercising this cache directly

## Purpose

`VertexBytecodeFor`/`PixelBytecodeFor` (anonymous-namespace helpers) switch over `D3DShaderVariant` to select the
matching `hlsl_shaders.hpp` byte array; the four public functions build on these.

## Executive Verdict

**Healthy.** Exhaustive, verified 1:1 mapping between the 17 `D3DShaderVariant` enumerators and the 34
`hlsl_shaders.hpp` byte arrays (17 vertex + 17 pixel); correct null/failure handling.

## Checklist Results

### Behavioral correctness / Logic
Every one of the 17 `D3DShaderVariant` cases is present in both `VertexBytecodeFor` and `PixelBytecodeFor`'s
switches (independently counted and cross-checked against `D3DShaderCache.hpp`'s enum and `hlsl_shaders.hpp`'s 34
array names) — no missing variant, no case falling through to the `{nullptr, 0}` default unexpectedly. Since the
switch has no `default:` label and covers every enumerator, an added-but-unhandled future variant would trigger a
`-Wswitch` compiler warning rather than silently returning null bytecode — a genuinely good defensive design choice
worth calling out.

### Memory/resource lifetime
`CreateVertexShaderForVariant`/`CreatePixelShaderForVariant` correctly null-check both `bc.bytes` and `device`
before calling `CreateVertexShader`/`CreatePixelShader`, and correctly return a default-constructed (null)
`ComPtr` without throwing on either a bad variant or a failed device call — matches the header's documented
contract exactly.

### C++ correctness / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found. `Bytecode` is a trivial, allocation-free pointer+size pair; no unnecessary copies.

## Detailed Findings

None.

## Cross-File Observations

The 17-variant, 34-array 1:1 correspondence spans three independently-checked files
(`D3DShaderCache.hpp`'s enum, this file's two switches, and `hlsl_shaders.hpp`'s array names/count) with zero
discrepancies found anywhere — a well-maintained, internally-consistent shader-embedding pipeline.

## Missing or Weak Tests

No dedicated test found exercising `CreateVertexShaderForVariant`/`CreatePixelShaderForVariant` for every variant
against a real (or Wine/DXVK-hosted) `ID3D11Device`.

## Positive Findings

The no-`default:`-label switch design turns a future missing-case bug into a compile-time warning rather than a
silent runtime null-bytecode failure — a stronger-than-typical defensive pattern for this kind of enum-to-data
dispatch.

## Final Assessment

No issues found.
