# Audit: examples/software_dual_envmap_skinned_test.cpp

## Metadata
- Source file: `examples/software_dual_envmap_skinned_test.cpp` (263 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-tests-software` shard
- File type: standalone backend integration-test executable (`Game` subclass)
- XNA/FNA relevance: exercises `DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect` (public
  XNA API) against the Software backend's CPU rasterizer

## Purpose
Verifies `DualTextureEffect`'s second-texture blend formula, `EnvironmentMapEffect`'s real cube-map
storage and reflection-vector sampling (plus `EnvironmentMapAmount=0` degrading to plain texture),
and `SkinnedEffect`'s real per-vertex bone-transform skinning — the 3 genuinely new pieces of
functionality this phase adds, explicitly distinguished from the "no lighting engine in v1" design
decision shared with plain `BasicEffect`.

## Executive Verdict
Excellent, carefully hand-derived test design. Check A's expected color
(`(100/255*2) * (200/255) * 255 ≈ 156.8`) is precisely computed from FNA's own `PSDualTexture`
formula, not approximated. Check B's reflection-direction reasoning (`eyeVector=(0,0,1)=Normal` for
a camera-facing quad, so `reflect(-E,N)=(0,0,1)` must sample exactly the `PositiveZ` cube face) is
geometrically exact, and the cube's 5 OTHER faces are deliberately set to a different, distinctive
color specifically so accidentally sampling any of them would be caught, not just "some cube color
appeared."

## Checklist Results
- Check C (`EnvironmentMapAmount=0`) is a real negative control for Check B — proving the env-map
  contribution is genuinely gated by this parameter, not unconditionally blended in regardless of
  its value.
- Check D's `SkinnedEffect` test checks BOTH that the shifted position shows red (`AnyMatchNear` at
  the translated location) AND that the ORIGINAL bind-pose position does NOT show red — a real
  before/after discrimination proving the vertex actually moved, not merely "red appeared
  somewhere on screen."
- `dev.setRasterizerStateProperty(RasterizerState::CullNone)` is set once at the top for the same
  reason established in `software_effects_test.cpp`/`software_rasterizer_test.cpp` (quads authored
  for pixel-correctness, not XNA winding convention) — consistent with the shard-wide SOFTWARE-81
  isolation practice.

## Detailed Findings
None.

## Cross-File Observations
Check A's `PSDualTexture` formula (`tex0*2 * tex1 * diffuse`) is the same formula this session's
`dx3_blend_test.cpp`/others document for their own effects — consistent, cross-backend-verified
understanding of FNA's actual stock-effect shader math, not a Software-backend-specific
reinterpretation.

## Missing or Weak Tests
None identified for this file's stated scope.

## Positive Findings
The deliberate choice to set 5 of the cube's 6 faces to one distinctive "wrong" color and only the
expected face to a different, equally distinctive color (rather than leaving the other faces at a
default/zero value) is a genuinely careful test-design choice — it converts "did we get some
plausible-looking result" into "did we specifically NOT sample any of the 5 wrong faces."

## Final Assessment
No findings.
