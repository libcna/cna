# Audit: src/CNA/Internal/Backends/D3D9/D3D9SpriteBatch.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/D3D9SpriteBatch.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ implementation
- XNA/FNA relevance: Implements D3D9SpriteBatchBackend: Begin/End/Draw/FlushBatch, custom-effect support, and SetTransformMatrix.
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Implements D3D9SpriteBatchBackend: Begin/End/Draw/FlushBatch, custom-effect support, and SetTransformMatrix.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
**`SetTransformMatrix()` is confirmed correctly implemented and genuinely consumed** (line 83: `transform_ = m;`; line 186: `return transform_ * projection;`) — NOT a no-op, unlike Vulkan's own confirmed defect; matches every other checked backend except Vulkan. `SetCustomEffect()` (line 86) correctly flushes any pending batch before switching effects (D9-112, explicitly documented as mirroring `D3D11SpriteBatchBackend::SetCustomEffect()`'s identical fix) — a pending batch built against the previous shader must be drawn before the switch, not silently redrawn with the new one. `ResolveD3D9TextureEXT()`'s own comment candidly documents a prior, now-fixed bug: a `RenderTarget2D` used as a SpriteBatch texture previously silently dropped the texture (drew untextured) because only `D3D9TextureBackend` was tried, not `D3D9RenderTargetBackend` — fixed by trying both concrete types via `dynamic_cast`.

## Detailed Findings

**`SetTransformMatrix()` is confirmed correctly implemented and genuinely consumed** (line 83: `transform_ = m;`; line 186: `return transform_ * projection;`) — NOT a no-op, unlike Vulkan's own confirmed defect; matches every other checked backend except Vulkan. `SetCustomEffect()` (line 86) correctly flushes any pending batch before switching effects (D9-112, explicitly documented as mirroring `D3D11SpriteBatchBackend::SetCustomEffect()`'s identical fix) — a pending batch built against the previous shader must be drawn before the switch, not silently redrawn with the new one. `ResolveD3D9TextureEXT()`'s own comment candidly documents a prior, now-fixed bug: a `RenderTarget2D` used as a SpriteBatch texture previously silently dropped the texture (drew untextured) because only `D3D9TextureBackend` was tried, not `D3D9RenderTargetBackend` — fixed by trying both concrete types via `dynamic_cast`.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

Confirmed correct, consumed SetTransformMatrix (contrast with Vulkan's own no-op bug); a candidly documented, now-fixed prior bug (RenderTarget2D-as-SpriteBatch-texture silently dropped).

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
