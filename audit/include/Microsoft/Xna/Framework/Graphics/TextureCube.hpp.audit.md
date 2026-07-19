# Audit: include/Microsoft/Xna/Framework/Graphics/TextureCube.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/TextureCube.hpp`
- Audit status: AUDITED (full read, 161 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/TextureCube.cs`
- Main related tests: not independently located in this pass

## Purpose
Represents a cube map texture (six equal-size faces): `SetData`/`GetData` per-`CubeMapFace`, plus
`DDSFromStreamEXT` for loading compressed DDS cube maps.

## Executive Verdict
Correct. Every `SetData`/`GetData` overload takes an explicit `CubeMapFace face` parameter, matching
FNA's own per-face API shape exactly — **this directly confirms the XNA-facing `TextureCube` class
itself has no role in the "cube mip regeneration touches all 6 faces" defect already found at the
backend level (SdlGpu, D3D11) in this project's own cross-cutting findings**: this class's own
public contract is correctly scoped to one face per call; a backend that then conservatively
regenerates mips for all 6 faces regardless is doing so on its own, not because this XNA-facing
API failed to communicate which face changed.

## Checklist Results
- Doxygen coverage: complete.
- `NOXNA` tagging: correctly applied to move-only declarations, `DDSFromStreamEXT`, `GetBackend`,
  the protected pre-built-backend constructor, and `GetBackendRaw`.
- `GetTypeName()` correctly declared to return the fully-qualified `.NET` name (confirmed correct in
  the paired `.cpp`).

## Detailed Findings
None.

## Cross-File Observations
The protected pre-built-backend constructor (used exclusively by `RenderTargetCube`) mirrors
`Texture2D`'s own equivalent constructor pattern for `RenderTarget2D` — consistent design across
both render-target-capable texture families.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correctly scoped per-face API surface; this is the strongest piece of evidence in this batch that
the previously-found cube-mip-regeneration defect is purely backend-level, not an XNA-facing API
design gap.

## Final Assessment
No findings.
