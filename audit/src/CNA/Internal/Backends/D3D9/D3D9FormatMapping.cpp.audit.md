# Audit: src/CNA/Internal/Backends/D3D9/D3D9FormatMapping.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/D3D9FormatMapping.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ implementation
- XNA/FNA relevance: Implements the XNA SurfaceFormat/DepthFormat -> D3DFORMAT mapping tables.
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Implements the XNA SurfaceFormat/DepthFormat -> D3DFORMAT mapping tables.

## Executive Verdict

Healthy — genuinely careful, well-verified bit-layout reasoning throughout.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
Every non-obvious mapping is justified by an explicit bit-layout derivation, not just a plausible-looking name match: correctly picks `D3DFMT_A2B10G10R10` (not the superficially-similar-named `D3DFMT_A2R10G10B10`) for `Rgba1010102` by deriving both formats' actual channel-bit-order from Microsoft's own legacy-format table rather than assuming from name similarity — explicitly flagged in-comment as a trap avoided, not just avoided by luck. Correctly notes `Bc7EXT`/`Bc7SrgbEXT` have no D3D9 equivalent at all (postdates D3D9 entirely) rather than silently substituting a lookalike compressed format, and that D3D9's sRGB handling is a per-sampler render state, not a distinct format bit, unlike D3D11.

## Detailed Findings

Every non-obvious mapping is justified by an explicit bit-layout derivation, not just a plausible-looking name match: correctly picks `D3DFMT_A2B10G10R10` (not the superficially-similar-named `D3DFMT_A2R10G10B10`) for `Rgba1010102` by deriving both formats' actual channel-bit-order from Microsoft's own legacy-format table rather than assuming from name similarity — explicitly flagged in-comment as a trap avoided, not just avoided by luck. Correctly notes `Bc7EXT`/`Bc7SrgbEXT` have no D3D9 equivalent at all (postdates D3D9 entirely) rather than silently substituting a lookalike compressed format, and that D3D9's sRGB handling is a per-sampler render state, not a distinct format bit, unlike D3D11.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

Careful, explicitly-justified bit-layout verification for every non-trivial mapping, with 2 documented traps correctly avoided (A2R10G10B10 vs A2B10G10R10; no silent BC7 substitute).

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
