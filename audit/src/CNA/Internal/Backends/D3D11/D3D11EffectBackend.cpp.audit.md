# Audit: src/CNA/Internal/Backends/D3D11/D3D11EffectBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D11/D3D11EffectBackend.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d11` shard
- File type: C++ implementation (161 lines)
- Related header: `include/CNA/Internal/Backends/D3D11/D3D11EffectBackend.hpp` (same shard)
- XNA/FNA relevance: NOXNA
- Graphics backend relevance: D3D11-specific
- FNA reference: N/A
- Main related tests: `examples-tests-d3d11` (not yet audited)

## Purpose

Implements `CompileProgram()` (runtime `D3DCompile()` for both stages, then `CreateVertexShader`/`CreatePixelShader`/
`CreateInputLayout`/constant-buffer creation), `Bind()`/`Unbind()`, and the 6 `SetUniform*` setters plus
`SetViewportSizeEXT()`.

## Executive Verdict

**Healthy.** Every uniform setter's byte-offset math independently verified to exactly match the header's
documented slot layout; correct compile-error surfacing.

## Checklist Results

### API / NOXNA parity (internal consistency against the documented convention)
Verified the exact float-index arithmetic against the header's documented byte-slot layout (`pushConst_` is
`float[32]`, i.e. 128 bytes / 4 bytes-per-float):
- `SetViewportSizeEXT()`: writes `pushConst_[0]`/`[1]` -- matches the documented `[0..15]` (vpSize) range.
- `SetUniformMat4()`: `memcpy(pushConst_ + 4, matrix, 64)` -- writes float indices `[4..19]` = byte range
  `[16..79]` -- matches the documented mat4 slot exactly (64 bytes = 16 floats).
- `SetUniformVec4/Vec3/Vec2()`: all write `pushConst_[20..23]` -- matches the documented `[80..95]` (vec4) range.
- `SetUniformFloat/Int()`: both write `pushConst_[24]` -- matches the documented `[96..99]` range.
All four offsets check out exactly with zero discrepancy — a fully internally-consistent implementation of its
own documented contract.

### Behavioral correctness / Logic
`CompileProgram()` correctly resets all state (`vs_.Reset()`, etc.) at the start, before attempting a fresh
compile, so a failed recompile doesn't leave a stale-but-still-`valid_` program bound. Compile errors correctly
surface the real `D3DCompile()` error blob text when present, falling back to a formatted `HRESULT` only when the
blob itself is unexpectedly null.
`Unbind()`'s no-op body is correctly justified in its own comment: D3D11 has no separate pipeline-object assembly
step to defer (unlike Vulkan), so `Bind()` already performs the real, immediate GPU state change — there is
nothing for `Unbind()` to undo, mirroring `VulkanEffectBackend::Unbind()`'s equivalent (tracking-pointer-only)
behavior for the same underlying reason.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

None.

## Cross-File Observations

Byte-slot convention exactly matches the header's documentation and (per the header's own claim, not
independently re-verified against Vulkan's own source in this pass) `VulkanEffectBackend`'s equivalent layout.

## Missing or Weak Tests

No dedicated test found for the compile-error-surfacing path (a deliberately-invalid HLSL source) on this backend.

## Positive Findings

Zero-discrepancy match between every uniform setter's actual byte offset and its documented slot — a fully
verified, internally-consistent implementation.

## Final Assessment

No issues found.
