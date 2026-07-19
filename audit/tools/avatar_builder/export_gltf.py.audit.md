# Audit: tools/avatar_builder/export_gltf.py

## Metadata
- Source file: `tools/avatar_builder/export_gltf.py` (43 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-avatar-builder` shard
- File type: Python script (Blender `bpy`)
- XNA/FNA relevance: content-generation tooling for CNA's avatar rendering system, not XNA API surface
- Main related tests: N/A (thin wrapper, no own `__main__` self-check)

## Purpose
Selects a given set of Blender objects and exports them to `.glb` via Blender's built-in glTF2
exporter, with animations/morphs/skinning included.

## Executive Verdict
Correct, minimal, single-purpose. `output_path.parent.mkdir(parents=True, exist_ok=True)` (line 26)
correctly ensures the output directory exists before the exporter writes to it.

## Checklist Results
- `export_animation_mode="ACTIONS"` correctly matches the README's own documented requirement that
  each Blender Action (`Stand0`, `Wave`, etc.) export as a separate glTF animation rather than
  being merged into one.
- `export_yup=True` correctly triggers Blender's standard Y-up glTF convention on export — the
  README's own claim that reimporting reopens cleanly and lands back in the same Z-up frame is
  consistent with Blender's own importer performing the inverse remap automatically.

## Detailed Findings
None.

## Cross-File Observations
Sole caller is `generate_avatar.py`/`generate_wardrobe.py` — confirmed both pass exactly the
armature + its mesh children, matching this function's own documented expected input shape.

## Missing or Weak Tests
This file has no own `__main__` block; its correctness is only exercised transitively through
`generate_avatar.py`'s/`generate_wardrobe.py`'s own `__main__` assertions (file exists, non-empty)
and `validate_gltf.py`'s separate, more thorough post-export checks.

## Positive Findings
Kept deliberately small and single-purpose per its own docstring ("kept in its own module so the
export step's chosen options live in one place"), consistent with the rest of this codebase's
established anti-premature-abstraction stance.

## Final Assessment
No findings.
