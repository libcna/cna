# Audit: tools/avatar_builder/generate_wardrobe.py

## Metadata
- Source file: `tools/avatar_builder/generate_wardrobe.py` (124 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-avatar-builder` shard
- File type: Python script (Blender `bpy`)
- XNA/FNA relevance: content-generation tooling for CNA's avatar rendering system, not XNA API surface
- Main related tests: self-contained `assert`-based `__main__` check

## Purpose
Builds and exports exactly one hair style or one clothing variant as a standalone, independently-
convertible `.glb` (armature + single piece mesh), for Task 11.14's "attachable wardrobe piece"
goal.

## Executive Verdict
Correct, and precisely scoped about what "attachable" does and doesn't mean yet — both this file's
own docstring and the README (cross-checked) are consistent and explicit that no runtime
attachment/engine support exists; this script only proves the *content* is modular and flows
through the existing, unmodified `convert_avatar.py` pipeline.

## Checklist Results
- Correctly imports `generate_clothes_meshcraft as generate_clothes` (line 41) — the production
  CSG-union clothes path, matching `generate_avatar.py`'s own aliasing convention, not the older
  `generate_clothes.py`.
- `build_piece()`'s gender-preset-resolution logic (lines 59-65) exactly mirrors
  `generate_avatar.build_avatar()`'s own preset-then-override pattern — confirmed consistent
  parameter semantics between the two entry points.
- The `__main__` block's assertions (`output_path.exists() and .stat().st_size > 0`,
  `len(piece_obj.data.vertices) > 0`, `piece_obj.vertex_groups`) are real, meaningful checks that
  the export actually contains real, weighted geometry — not just "the script didn't crash."

## Detailed Findings
None.

## Cross-File Observations
Depends on `generate_clothes_meshcraft.py`/`generate_hair.py` for actual piece construction —
inherits the already-fixed "infinite slab" weight-blending logic transitively via those modules'
own dependency on `generate_body.fix_automatic_weights()`.

## Missing or Weak Tests
None beyond the general pattern noted in sibling files.

## Positive Findings
The precise, repeated scoping of "attachable" (convertible independently, not runtime-attachable
yet) prevents this script from overclaiming capability it doesn't have.

## Final Assessment
No findings.
