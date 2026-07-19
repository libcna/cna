# Audit: tools/avatar_builder/generate_skeleton.py

## Metadata
- Source file: `tools/avatar_builder/generate_skeleton.py` (156 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-avatar-builder` shard
- File type: Python script (Blender `bpy`, plus bpy-free pure-data exports)
- XNA/FNA relevance: content-generation tooling for CNA's avatar rendering system, not XNA API surface
- Main related tests: self-contained `assert`-based `__main__` check; consumed directly by
  `validate_gltf.py` (bpy-free import of `BONES`)

## Purpose
Defines the canonical 19-bone CNA-original avatar skeleton (`BONES`) and builds it as a real
Blender armature (`build_skeleton()`), plus `build_bones()` for Task 11.13's height/shoulder-width
parametric scaling.

## Executive Verdict
Correct. The lazy `import bpy` inside `build_skeleton()` (not at module level) is a deliberate,
well-motivated fix — the file's own docstring and the README (Task 11.8 section) explain this is
specifically so `validate_gltf.py` can import `BONES`/`ARMATURE_NAME` as pure data from plain
`python3`, outside Blender, without triggering a `sys.exit()`.

## Checklist Results
- `build_bones()`'s `scale_point()` correctly applies `height_scale` to all three coordinates
  uniformly, then `shoulder_width_scale` *additionally* to the X coordinate of arm-chain bones only
  (`_ARM_CHAIN_BONE_NAMES`) — matches the README's documented semantics exactly.
- `build_skeleton()` correctly removes any pre-existing same-named armature first (`bpy.data.armatures.remove(existing.data, do_unlink=True)`), making it safe to call repeatedly in
  one Blender session (as `generate_avatar.py` does across gender/style variants).
- The two-pass bone-creation loop (create all bones by name first, then assign parents) correctly
  avoids a forward-reference problem — a bone's parent must already exist in `edit_bones` before
  `eb.parent = edit_bones[parent]` can succeed, and the first loop guarantees this.

## Detailed Findings
None.

## Cross-File Observations
This is the single source of truth every other script in this batch keys bone names/positions off
of — confirmed consistent with the README's own bone table and with every consumer script's own
`bones_by_name`/`BONE_RADII` lookups (all keyed by these exact 19 names).

## Missing or Weak Tests
The `__main__` block's own `assert`-based self-check (bone count, name/parent match) is a
reasonable, real regression test, run manually via `blender --background --python generate_skeleton.py`
rather than through an automated CI harness (consistent with this being explicitly offline,
one-time content tooling per its own docstring).

## Positive Findings
The lazy-import fix for `validate_gltf.py`'s bpy-free consumption is a clean, minimal, correctly-
scoped solution to a real cross-tool dependency problem.

## Final Assessment
No findings.
