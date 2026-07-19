# Audit: include/Microsoft/Xna/Framework/Graphics/PbrEffect.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/PbrEffect.hpp`
- Audit status: AUDITED (full read, 261 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: NOXNA extension, no FNA equivalent (real XNA predates the PBR content pipeline
  entirely, per this file's own doc comment) — reviewed for internal consistency with sibling
  stock effects instead
- Main related tests: not independently located in this pass

## Purpose
Implements glTF 2.0's metallic-roughness PBR material model (base color, normal,
metallic-roughness, emissive, occlusion maps) with the same 3-directional-light + ambient
convention every other CNA stock effect uses.

## Executive Verdict
Correct and honestly scoped: the class's own doc comment explicitly discloses which backends have
a real PBR shader (most) versus an untextured/unlit fallback (Software/Canvas/Ascii/Headless/
SDL_Renderer/Dx3), and clarifies the BRDF used (real glTF spec GGX/Smith-Schlick-GGX/Schlick
Fresnel) versus what's explicitly out of scope (image-based lighting). See the paired `.cpp`
report for a MEDIUM finding: `setLightingEnabledProperty(false)` throws a raw `std::runtime_error`
instead of `System::NotSupportedException`, mirroring `SkinnedEffect`'s own established (if
XNA-irrelevant here) constraint.

## Checklist Results
- Doxygen coverage: complete, including honest scope-boundary documentation in the class-level
  comment (rare and valuable for a NOXNA extension).
- Correctly and completely populates its own `Effect::Parameters` collection (a smaller set than
  the XNA-native stock effects — `DiffuseColor`/`FogColor`/`FogVector`/`WorldViewProj` only, since
  the PBR-specific factors/textures have no XNA-equivalent named-parameter precedent to match).
- All 5 texture-map ownership helpers (`SetOwnedTexture`/`SetOwnedNormalMap`/
  `SetOwnedMetallicRoughnessMap`/`SetOwnedEmissiveMap`/`SetOwnedOcclusionMap`) correctly mirror
  `BasicEffect`'s established ownership pattern, independently, for each of the 5 texture slots.

## Detailed Findings
None new in this header (see `.cpp` report).

## Cross-File Observations
`setLightingEnabledProperty`'s doc comment (lines 99-106) explicitly and honestly frames the
"must be true" constraint as a deliberate NOXNA divergence mirroring `SkinnedEffect`'s own
identical real-XNA constraint, not an oversight — a good example of disclosed design consistency
even for a type with no FNA reference to match.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Exceptionally clear, honest scope documentation for a from-scratch NOXNA extension with no FNA
precedent to lean on.

## Final Assessment
No new findings in this header; see the paired `.cpp` report for the MEDIUM exception-type
finding.
