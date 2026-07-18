# Audit: include/CNA/Internal/Backends/D3D12/D3D12PipelineStateCache.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D12/D3D12PipelineStateCache.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d12` shard
- XNA/FNA relevance: `BlendState`/`DepthStencilState`(partial)/`RasterizerState`(partial) -> D3D12 PSO baking
- Graphics backend relevance: D3D12-specific
- FNA reference: N/A
- Main related tests: `examples-tests-d3d12` (not yet audited)

## Purpose

Declares `D3D12PipelineStateDesc` (the full field-tuple PSO cache key) and `D3D12PipelineStateCache::GetOrCreate()`.

## Executive Verdict

**Needs attention — a real, significant, but honestly-disclosed HIGH-severity FNA-parity gap (Stencil/Scissor), alongside an exceptional piece of self-correcting documentation.**

## Checklist Results

### API / FNA parity
**F1 (HIGH, see `AUDIT_CROSS_CUTTING_FINDINGS.md`):** the header's own comment explicitly and honestly discloses "Stencil state and scissor-enable are deliberately NOT part of this first key/desc" — **confirmed via the `.cpp`/`D3D12GraphicsBackend.cpp` reports that this means stencil testing and scissor testing are currently 100% non-functional on this backend**, not merely "not yet optimally cached." A real, currently-active regression relative to D3D11 (full stencil+scissor support), for two commonly-used XNA rendering techniques.

### Positive/documentation quality
**Exceptional, rare finding**: this header's own comment (lines 52-63) documents and corrects a mistake in an *earlier version of the same comment block* (mislabeling `Blend::One`'s ordinal as `2` instead of its real value `0`), explicitly confirms the mistake was "functionally inert while it stood" (traced why: a matching, internally-consistent wrong constant elsewhere canceled it out, and the real code path never depended on the wrong default), and leaves this self-correction in place as a record — one of the most transparent, self-critical pieces of engineering documentation found anywhere in this audit.

### Architecture
The "one PSO per full composite key" caching strategy is explicitly justified as the correct first-implementation choice given D3D12's PSO-bakes-everything model (unlike D3D11's independently-cacheable state objects), with an honest note that a smaller/derived key MAY be needed later if the PSO count becomes a measured problem — not assumed prematurely.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

**F1 (HIGH):** Stencil/Scissor completely non-functional — see `AUDIT_CROSS_CUTTING_FINDINGS.md` for full detail and the confirming `.cpp` evidence.

## Cross-File Observations

Directly connects to `D3D12GraphicsBackend::ApplyDepthStencilState()`/`ApplyRasterizerState()`'s own commented-out unused parameters — the C++ dispatch layer and this cache's key both consistently omit the same fields, confirming this is a deliberate, coherent (if significant) scope cut, not an inconsistency between layers.

## Missing or Weak Tests

No dedicated test found exercising `StencilState`/`ScissorRectangle` on D3D12 (unsurprising — no Windows-native CI for this backend per D-P4).

## Positive Findings

A genuinely rare example of a comment correcting and documenting its own prior mistake rather than silently fixing it and moving on — a model of engineering transparency.

## Final Assessment

One HIGH-severity, honestly-disclosed FNA-parity gap (Stencil/Scissor non-functional); otherwise exceptionally well-documented.
