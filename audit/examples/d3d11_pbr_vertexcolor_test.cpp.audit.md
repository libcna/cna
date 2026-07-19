# Audit: examples/d3d11_pbr_vertexcolor_test.cpp

## Metadata
- Source file: `examples/d3d11_pbr_vertexcolor_test.cpp` (245 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-tests-d3d11` shard
- File type: standalone real-GPU pixel-test executable (`PixelTestGame` subclass, Wine+DXVK)
- XNA/FNA relevance: exercises `PbrEffect`/`SkinnedPbrEffect`/`SkinnedEffect` (public XNA-adjacent
  Effects API; Pbr/SkinnedPbr are NOXNA CNA extensions, `SkinnedEffect.VertexColorEnabled` is real
  XNA API) against the D3D11 backend's real shader dispatch

## Purpose
Proves 4 new D3D11 vertex-stride/shader variants are genuinely selected and execute correctly via a
real GPU draw (not just "does it link"): `PbrEffect` (stride 48), `SkinnedPbrEffect` (stride 68),
and `SkinnedEffect` with `VertexColorEnabled=true` on both `PreferPerPixelLighting` variants
(stride 56).

## Executive Verdict
Excellent, precisely-reasoned pixel-test design. Quads A/B deliberately construct a degenerate,
exactly-hand-derivable lighting scenario (no lights, zero ambient, so `PbrLight()`'s own
contribution is exactly zero) rather than attempting to hand-derive a realistically-lit PBR BRDF
value — the header comment explicitly cites `easygl_pbreffect_golden_test.cpp`'s own reasoning for
why the latter is impractical. The chosen `EmissiveFactor=(1,0,0)` with 0/1-only channel values is
further noted to be gamma/sRGB-invariant (the sRGB transfer function fixes both endpoints), so the
expected pixel is exact regardless of the swapchain's sRGB back-buffer format — a subtle and
correct piece of reasoning that avoids a whole category of tolerance-related test flakiness.

## Checklist Results
- Quad A/B deliberately leave `NormalMap`/`MetallicRoughnessMap`/`EmissiveMap`/`OcclusionMap`
  unbound specifically to exercise `D3D11GraphicsBackend`'s new default-fallback-texture path
  (`GetOrCreateDefaultWhiteSrvEXT`) — a real, deliberate coverage choice, not an oversight.
  ​
- Quad B's single identity bone is explicitly noted as isolating "does the PBR+skinning combo
  dispatch/cbuffer wiring work," not skinning math itself (already covered elsewhere, DX-151) — a
  precise scope statement that avoids this test silently overclaiming skinning-math coverage.
- Quad C/D's pure-black per-vertex color with `VertexColorEnabled=true` correctly reuses
  `easygl_skinnedeffect_vertexcolor_test.cpp`'s own established technique (documented as such): a
  lighting-independent proof that only works because black-times-anything is exactly black,
  regardless of what the lit/textured color would otherwise have been — an unambiguous, tolerance-
  friendly assertion.
- `static_assert(sizeof(...) == N, ...)` on all 3 vertex structs guards against silent struct-
  layout/padding drift breaking the stride-keyed input-layout lookup this test depends on.

## Detailed Findings
None.

## Cross-File Observations
Explicitly built on 2 other already-audited files' established techniques:
`easygl_pbreffect_golden_test.cpp`'s reasoning for why hand-derived BRDF values are impractical, and
`easygl_skinnedeffect_vertexcolor_test.cpp`'s pure-black-vertex-color zeroing technique — both
correctly cited and reused rather than re-invented.

## Missing or Weak Tests
None identified for this file's stated scope.

## Positive Findings
The sRGB-invariance reasoning for the chosen `EmissiveFactor`/pixel values (0/1-only channels,
gamma-function-endpoint-invariant) is a genuinely sophisticated piece of test design that
preemptively avoids a whole class of format-dependent flakiness other, less careful pixel tests in
this codebase could be vulnerable to.

## Final Assessment
No findings.
