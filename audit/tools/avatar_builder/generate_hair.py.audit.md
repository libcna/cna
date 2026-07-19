# Audit: tools/avatar_builder/generate_hair.py

## Metadata
- Source file: `tools/avatar_builder/generate_hair.py` (168 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-avatar-builder` shard
- File type: Python script (Blender `bpy`/`bmesh`)
- XNA/FNA relevance: content-generation tooling for CNA's avatar rendering system, not XNA API surface
- Main related tests: self-contained `assert`-based `__main__` check

## Purpose
Builds placeholder hair (`Cap` hemisphere shell, or `Ponytail` — the same cap plus a tapered cone
tail), parented to the skeleton with automatic weights (rigidly following only the `Head` bone in
practice).

## Executive Verdict
Correct. Reuses `generate_body.fix_automatic_weights()` unchanged (line 148) for its own weight
blending, inheriting the already-fixed "infinite slab" logic; since only `Head` is nearby, no
bend-joint blending in `BEND_JOINTS` actually applies here in practice, making this file low-risk
for that specific defect class regardless.

## Checklist Results
- `_head_center_and_radius()`'s docstring explicitly notes it's "mirrored from generate_body.py's
  Head handling" — confirmed consistent: same midpoint-of-head/tail center, same
  `BONE_RADII["Head"] * head_scale` radius formula (plus this file's own `HAIR_PADDING` outward
  offset).
- `_build_ponytail_mesh()` builds the cone tail with `bmesh.ops.create_cone` and a rotation derived
  from `rotation_difference` against a `droop_direction` vector — a reasonable, explicit geometric
  construction, not a magic-number placement.
- `build_hair()` correctly raises `ValueError` for an unknown `style` argument (line 128) rather
  than silently defaulting or crashing with a `KeyError` deep in `HAIRSTYLES[style]`.

## Detailed Findings
None.

## Cross-File Observations
Reuses `generate_body.fix_automatic_weights()` — see `generate_body.py.audit.md` for the "infinite
slab" fix this inherits (though largely moot for this file specifically, since hair geometry is
only ever near the `Head` bone).

## Missing or Weak Tests
None beyond the general pattern noted in sibling files.

## Positive Findings
The explicit `ValueError` for an unrecognized style, and the clear "mirrored from generate_body.py"
cross-reference rather than silently duplicating logic with no acknowledgment, are both good
practices.

## Final Assessment
No findings.
