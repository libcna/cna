# Audit: tools/avatar_builder/generate_body_meshcraft.py

## Metadata
- Source file: `tools/avatar_builder/generate_body_meshcraft.py` (333 lines)
- Audit status: AUDITED (full read) — reviewed as part of this fork's "infinite slab" bone-weight
  investigation, since this is the actual production body-generation path
- Subsystem: `tools-avatar-builder` shard
- File type: Python script (Blender `bpy` + external `mc3togltf` subprocess)
- XNA/FNA relevance: content-generation tooling for CNA's avatar rendering system, not XNA API surface
- Main related tests: self-contained `assert`-based `__main__` check

## Purpose
Phase 7 drop-in replacement for `generate_body.build_body()`: authors the same per-bone geometry as
real, watertight capsules via the sibling `mesh-craft` CSG tool (external `mc3togltf` subprocess)
instead of `bpy` cylinder+sphere primitives joined via `bpy.ops.object.join()` — fixing the
"monster avatar" self-intersecting-mesh defect at every joint.

## Executive Verdict
Correct, and confirmed to inherit `generate_body.py`'s already-fixed weight-blending logic rather
than reimplementing (and potentially regressing) it: `build_body()` (line 308) calls
`generate_body.fix_automatic_weights(body_obj, bones, blend_radius=avg_radius * 1.6)` — the exact
same function this fork traced the "infinite slab" fix to in `generate_body.py`, just parameterized
with a wider `blend_radius` (derived from this module's own, thicker `BONE_RADII`) to match the
now-thicker, CSG-merged geometry. No separate weight-blending code exists in this file.

## Checklist Results
- `_locate_mc3togltf()` correctly resolves the external binary via `$MC3TOGLTF` first, then a
  small, explicit set of conventional build-output paths, raising a clear `FileNotFoundError`
  (not a silent failure) if none resolve.
- `bones_to_mc3_xml()` correctly converts `generate_skeleton.py`'s Z-up frame to mesh-craft's Y-up
  frame via `_mc3_position()` (`(x, y, z) -> (x, z, y)`) — confirmed empirically-verified per the
  README's own claim, and self-consistently applied to both head/tail points before computing the
  capsule transform.
- `build_body()` correctly checks `result.returncode != 0 or not glb_path.is_file()` after the
  `mc3togltf` subprocess call and raises `RuntimeError` with both stdout/stderr on failure — fails
  loudly rather than silently proceeding with a missing/malformed export.
- `_recalculate_smooth_normals()`'s own docstring documents a real, two-round remediation history
  (flat CSG normals rendering as near-black patches under fixed-direction lighting; then a second
  round finding `shade_smooth()` alone was a near-no-op because Blender's glTF importer sets
  *custom split normals* that override it) — the final fix (`customdata_custom_splitnormals_clear()`
  before `shade_smooth()`) is verified with an inline `assert not obj.data.has_custom_normals` right
  after, a real regression guard rather than an unverified claim.

## Detailed Findings
None. No new bone-weight-blending defects found; confirmed this file correctly reuses, rather than
diverges from, `generate_body.py`'s already-fixed `fix_automatic_weights()`.

## Cross-File Observations
See `generate_body.py.audit.md` for the full "infinite slab" root-cause/fix analysis this file's
own weight-blending inherits unchanged.

## Missing or Weak Tests
Same gap as `generate_body.py`: no automated assertion specifically checking for cross-body-part
weight bleeding; relies on `diagnose_avatar_mesh.py` for that class of verification.

## Positive Findings
The `_recalculate_smooth_normals()` docstring's two-round "here's what I got wrong the first time
and why" account is an excellent example of honest, iterative documentation of a real debugging
process, including a self-verifying `assert` rather than just a comment claiming correctness.

## Final Assessment
No findings.
