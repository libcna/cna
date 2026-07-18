# Audit: include/CNA/Internal/Backends/D3D11/D3D11EffectBackend.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D11/D3D11EffectBackend.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d11` shard
- File type: C++ header (69 lines)
- Related implementation: `src/CNA/Internal/Backends/D3D11/D3D11EffectBackend.cpp` (same shard)
- XNA/FNA relevance: NOXNA — a custom `ShaderEffect` runtime-compile facility (`SpriteBatch.SetCustomEffect`
  wiring), not a stock XNA effect itself
- Graphics backend relevance: D3D11-specific runtime `D3DCompile()`-based custom shader compiler
- FNA reference: N/A (NOXNA extension)
- Main related tests: `examples-tests-d3d11` (not yet audited)

## Purpose

Declares `D3D11EffectBackend`: compiles arbitrary vertex+fragment HLSL source at runtime via `D3DCompile()`, with
a fixed 128-byte uniform-slot convention and a fixed `Sprite2DVertex`-shaped input layout, deliberately mirroring
`VulkanEffectBackend`'s established contract.

## Executive Verdict

**Healthy.** Honest, explicit scope boundary; correctly documented fixed-slot convention.

## Checklist Results

### Architecture
The header's own "Scope boundary (honest, not a placeholder)" comment is a good practice worth noting — it
explicitly states which parts are real/GPU-tested vs. deferred to a later phase (`SetViewportSizeEXT` filling a
previously-reserved slot), rather than leaving the reader to guess.
The fixed 128-byte uniform-slot layout (`[0..15]=vpSize, [16..79]=mat4, [80..95]=vec4, [96..99]=float/int`)
deliberately mirrors `VulkanEffectBackend`'s own convention byte-for-byte, including the "`name` parameter
deliberately ignored" design (a single fixed slot per type, not a general named-uniform system) — an intentional,
disclosed limitation appropriate for this mechanism's actual scope (a `SpriteBatch`-custom-shader facility, not a
general arbitrary-uniform system), not a broader `Effect`/`EffectParameter` implementation.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

None.

## Cross-File Observations

See `.cpp` report for verification that the byte-offset math in each `SetUniform*` setter actually matches this
header's documented slot layout.

## Missing or Weak Tests

No dedicated test found for this backend's custom-effect compile path in this audit so far.

## Positive Findings

Clear, honest "what's real vs. deferred" scope-boundary documentation; correctly disclosed fixed-slot (not
general) uniform convention matching an established cross-backend precedent.

## Final Assessment

No issues found.
