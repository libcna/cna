# Audit: include/Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp`
- Audit status: AUDITED (full read, 227 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: NOXNA extension, no FNA equivalent — used by
  `GamerServices::AvatarRenderer::EnableRealRenderingEXT` for real avatar rendering (deliberately
  independent of `Model`/`ModelBone`/`ModelMesh`)
- Main related tests: not independently located in this pass; `GetOwned*CountForTesting()` accessors
  strongly suggest a Task 11.5 ownership-leak regression test exists

## Purpose
A real, GPU-skinnable mesh + skeleton + animation-clip set: owns its vertex/index buffers, mesh
parts, and textures; computes per-bone skinning matrices from a named animation clip via
`ComputeBoneTransformsEXT`.

## Executive Verdict
Correct, and — per this audit's specific investigation into a project-memory-recorded history of
joint-weight-blending defects ("infinite slab" symptom, reverted flat-cap garment redesign) —
**this type's `ComputeBoneTransformsEXT` does NOT contain the kind of per-vertex multi-bone weight
blending that symptom would require.** See the paired `.cpp` report's Detailed Findings for the
full analysis; this is an important corrective note for this project's own audit trail, since a
sibling shard's `AvatarRenderer` audit (completed just before this one) flagged this file as the
most likely location for that history. `Parts`/vertex-buffer/index-buffer/texture ownership is
correctly modeled via `std::unique_ptr` (owning) alongside `PartEXT`'s own non-owning raw-pointer
descriptors (`Part`/`Texture`), matching this project's established dual-ownership-model
convention for GamerServices-adjacent collections already confirmed elsewhere in this audit.

## Checklist Results
- Doxygen coverage: complete, including a detailed `@throws` on `AttachPartEXT` documenting exactly
  what its one cheap validation check can and cannot detect (same-count-but-different-skeleton is
  explicitly disclosed as undetectable).
- `NOXNA` tagging: correctly applied throughout (entire file has no real XNA API surface).
- Move-only semantics (`= delete` copy, `noexcept` move) correctly modeled for a GPU-resource-owning
  type.
- `GetOwned*CountForTesting()` accessors are appropriately `NOXNA`-tagged testing hooks, consistent
  with this project's established `*ForTesting()` convention seen elsewhere in this audit (e.g.
  `NetworkSession::GetOwnedGamerCountForTesting()` in the `xna-net` shard).

## Detailed Findings
None in this header (see the paired `.cpp` report for the corrective note on the "infinite slab"
investigation and confirmed prior-fix verification).

## Cross-File Observations
`PartEXT::Name`'s doc comment explicitly documents that `AvatarRenderer::PartTintEXT` matches by
substring (not exact equality) against the source Blender object name — a real, disclosed coupling
between this file's naming convention and the tinting logic in the sibling
`GamerServices::AvatarRenderer` (already audited this session).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The doc comments throughout this file consistently cite specific tracked tasks (11.4, 11.5, 11.21)
for every non-obvious design decision, matching the same high documentation standard already
observed in the `xna-net`/`xna-gamerservices` shards this session.

## Final Assessment
No findings against this header.
