# Audit: docs/avatar-real-rendering-ext.md

## Metadata
- Source file: `docs/avatar-real-rendering-ext.md` (235 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown architecture/design document
- XNA/FNA relevance: documents the NOXNA `SkinnedModelEXT`/`AvatarRenderer::EnableRealRenderingEXT`
  extension and explicitly reaffirms the faithful XNA `AvatarRenderer`/`AvatarAnimation` behavior is
  unchanged by it

## Purpose
The authoritative architecture document for CNA's opt-in real avatar-rendering extension: new types,
bone-index decoupling from the real 71-bone Xbox arrays, content-pipeline schema, GamerServices API
surface additions, per-backend support status, and the Phase 7 mesh-craft body/clothing generation
rework.

## Executive Verdict
Thorough and internally consistent, with a genuinely valuable "three real, previously-undetected
bugs" section (path resolution, C++ argument-evaluation-order, bone-hierarchy reordering) that reads
as an honest engineering post-mortem, not a marketing summary. Its claim that the faithful XNA
`AvatarRenderer.Draw()` behavior (no-op, `Unavailable` state) is completely untouched by this
extension is consistent with this session's own `xna-gamerservices` shard audit, which did not find
any contamination of the faithful-XNA Avatar path by this extension.

## Checklist Results
- The C++ "argument-evaluation-order" bug description (`clipReader.Read<float>()` calls chained
  directly as constructor arguments, no guaranteed left-to-right order) is a precise, technically
  correct description of real, well-known C++ undefined behavior — not an exaggerated or
  mischaracterized claim.
- The bone-index-mapping claim ("`AvatarRenderer::ParentBones`/`IAvatarAnimation::BoneTransforms`...
  are untouched... hard-throws `ArgumentException` unless given exactly 71 entries") is consistent
  with this session's own audit of the Net/GamerServices shard, which independently confirmed
  `AvatarRenderer`'s faithful-XNA throw behavior.
- Backend support table: EasyGL "proven end-to-end," Vulkan/Bgfx "real...wiring exists; not yet
  smoke-tested for this feature" — an honestly incomplete-but-labeled-as-such status, not an
  overclaim.
- Phase 7's "Honest result" section explicitly lists residual gaps (shoe-area artifact, `Wave`-pose
  chest-band artifact, `validate_gltf.py`'s missing NaN/Inf/bounds checks) rather than declaring
  unconditional success — consistent with the identical gaps named in the sibling `avatar-demos.md`.

## Detailed Findings
None — no claim in this document was contradicted by this session's own independent audits of the
avatar pipeline, `AvatarRenderer`/`AvatarAnimation` (xna-gamerservices shard), or the mesh-craft
generator scripts (tools-avatar-builder shard).

## Cross-File Observations
- Directly corroborates `docs/avatar-demos.md`'s troubleshooting section (same residual gaps, same
  `validate_gltf.py` caveat).
- Directly corroborates `docs/avatar-art-direction.md`'s topology/skinning requirements as the target
  state the Phase 7 mesh-craft fix moves toward.
- This document's Phase 7 section states `generate_body.fix_automatic_weights` "still handles
  skinning, with a widened blend radius to match the new geometry" — consistent with, and does not
  contradict, this session's own confirmed finding that the historical "infinite slab" bend-joint
  blend bug in that exact function has since been fixed (a perpendicular-distance check was added,
  per the `tools-avatar-builder` shard's `generate_body.py.audit.md`). This document does not
  name that specific historical bug, but nothing here is inconsistent with it having existed and
  been fixed.

## Missing or Weak Tests
N/A for the document itself; it does reference `examples/avatar_real_render_integration_test.cpp` as
EasyGL's own proof, consistent with this session's `examples-common`/graphics-shard audits not
flagging that test as absent or broken.

## Positive Findings
The three-bugs section is an excellent example of "found by actually rendering, not by static review"
engineering documentation — it explicitly states the Phase 10 synthetic fixture could not have caught
any of the three (identity matrices, single hand-built bone, hand-constructed quaternion), which is a
genuinely useful lesson about the limits of synthetic test fixtures versus real content.

## Final Assessment
No findings. A thorough, accurate, internally and cross-file consistent architecture document.
