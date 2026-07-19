# Audit: tools/avatar_builder/generate_clothes_meshcraft.py

## Metadata
- Source file: `tools/avatar_builder/generate_clothes_meshcraft.py` (203 lines)
- Audit status: AUDITED (full read) — reviewed as part of this fork's "infinite slab" bone-weight
  investigation, since this is the actual production clothes-generation path
- Subsystem: `tools-avatar-builder` shard
- File type: Python script (Blender `bpy` + external `mc3togltf` subprocess)
- XNA/FNA relevance: content-generation tooling for CNA's avatar rendering system, not XNA API surface
- Main related tests: self-contained `assert`-based `__main__` check

## Purpose
Phase 7 drop-in replacement for `generate_clothes.build_clothes()`: builds each garment shell as a
real, watertight mesh-craft CSG union of capsules instead of `bpy.ops.object.join()`ed primitives —
same fix rationale as `generate_body_meshcraft.py`.

## Executive Verdict
Correct, and confirmed to reuse (not reimplement) `generate_body.fix_automatic_weights()` for its
own skinning — inheriting the "infinite slab" fix documented in `generate_body.py.audit.md`. Beyond
that, this file documents its own genuinely careful, measured fix for a *related but distinct*
skinning defect: a garment/body blend-radius mismatch.

## Checklist Results
- Line 159-169's comment documents a real, quantified, previously-fixed defect distinct from the
  infinite-slab bug: passing the SAME `blend_radius` the body is skinned with
  (`body_blend_radius = (sum(_radii.values()) / len(_radii)) * 1.6`, matching
  `generate_body_meshcraft.build_body()`'s own value) instead of `fix_automatic_weights()`'s
  `0.08` default — because "a garment skinned with a different joint-blend width than the body it
  covers deforms differently from that body at every animated joint," causing the two surfaces to
  cross under animation. Measured as "body=0.1474 vs garment=0.08, an ~84% mismatch" and confirmed
  as the real source of "dark blotches" the audit reported at the Wave-pose torso/shoulder. This is
  a correct fix for a real, geometrically-motivated defect (mismatched deformation profiles between
  two meshes meant to move together), independently verified via the specific numeric mismatch
  cited.
- `_garment_to_mc3_xml()`'s radius scaling comment (lines 60-69) documents another real, measured
  fix: garment padding constants tuned against the old, thinner body read as "a barely-visible
  sliver" against the new, thicker mesh-craft body radii — fixed with a `padding * 1.8` multiplier,
  confirmed by direct visual inspection per the comment.
- Correctly re-exports `GARMENT_STYLES`/`DEFAULT_STYLES`/`NAME_PREFIX` from `generate_clothes.py`
  unchanged (lines 43-45) rather than duplicating them, so which bones/styles exist stays a single
  source of truth across both the bpy-primitive and mesh-craft generation paths.

## Detailed Findings
None. Confirms this file's weight-blending inherits `generate_body.py`'s "infinite slab" fix, and
documents its own separate, correctly-verified blend-radius-matching fix.

## Cross-File Observations
Depends on `generate_body_meshcraft.py` (`_mc3_position`, `_capsule_transform`, `_locate_mc3togltf`,
`BONE_RADII`, `_recalculate_smooth_normals`) and `generate_body.py` (`fix_automatic_weights`) —
confirmed all imports resolve to the expected already-audited functions, no divergent
reimplementation of shared logic.

## Missing or Weak Tests
Same gap as its siblings: no automated cross-body-part weight-bleeding check in this file's own
`__main__` self-test.

## Positive Findings
The blend-radius-mismatch fix, with its specific quantified before/after values (0.1474 vs 0.08,
~84% mismatch), is a strong example of root-causing a visual defect to a precise numeric parameter
mismatch rather than iteratively tweaking until it looked better.

## Final Assessment
No findings.
