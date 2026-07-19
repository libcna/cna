# Audit: tools/xna-oracle/scenes/dualtexture_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/dualtexture_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `DualTextureEffect` baseline scene, exercises the custom
  `VertexPositionDualTexture` format both `Oracle.cs` and `CnaOracleRender.cpp` define (real XNA has
  no built-in dual-UV vertex struct)
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Confirms `DualTextureEffect`'s second-texture multiply-blend and the custom
`VertexPositionDualTexture` vertex declaration (stride 28, offsets 0/12/20) work identically on both
sides.

## Executive Verdict
Correct fixture, and load-bearing for validating a genuinely nontrivial piece of both-sides custom
interop code (the custom vertex struct, confirmed `[StructLayout(LayoutKind.Sequential)]`-correct
on the C# side per `Oracle.cs`'s own audit).

## Checklist Results
- `texture0`/`texture1` keys present and distinct textures used, giving genuine discriminating
  power for the multiply-blend (verified the two source textures differ).
- Confirmed pixel-perfect per `README.md`'s status log.

## Detailed Findings
None.

## Cross-File Observations
The only scene in the corpus exercising `VertexPositionDualTexture`/`DualTextureEffect` — see
`Oracle.cs.audit.md` and `CnaOracleRender.cpp.audit.md` for the custom-vertex-format interop
analysis this scene validates.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Uses genuinely distinct textures rather than a degenerate case where both texture layers would
look identical regardless of correctness.

## Final Assessment
No findings.
