# Audit: tools/avatar_builder/generate_avatar.py

## Metadata
- Source file: `tools/avatar_builder/generate_avatar.py` (138 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-avatar-builder` shard
- File type: Python script (Blender `bpy`)
- XNA/FNA relevance: content-generation tooling for CNA's avatar rendering system, not XNA API surface
- Main related tests: self-contained `assert`-based `__main__` check

## Purpose
Top-level orchestrator: builds one full avatar (skeleton, body, materials, morphs, clothes, hair,
animations) in a clean scene and exports it via `export_gltf.py`.

## Executive Verdict
Correct. Confirms — via its own explicit import aliasing (`import generate_body_meshcraft as
generate_body`, `import generate_clothes_meshcraft as generate_clothes`, lines 39, 46) — that the
production avatar-generation path is the Phase 7 mesh-craft CSG pipeline, not the original
`generate_body.py`/`generate_clothes.py` (which remain standalone-runnable but are not what this
orchestrator actually calls).

## Checklist Results
- `build_avatar()`'s scene-clearing loop (`for obj in list(bpy.data.objects): bpy.data.objects.remove(obj, do_unlink=True)`, lines 83-84) correctly makes this safe to call repeatedly
  for multiple gender/style exports in one Blender process — no leftover state from a prior call.
- `GENDER_PRESETS`'s female preset (`height_scale=0.93, shoulder_width_scale=0.85,
  head_scale=0.97`) is explicitly documented as "coarse starting points, not measured/researched
  proportions" — an honest disclosure, not a claim of anatomical accuracy.
- The build order (skeleton → body → materials → morphs → clothes → hair → animations) matches the
  README's own documented Task 11.1-11.6 sequence exactly.

## Detailed Findings
None.

## Cross-File Observations
Directly confirms this file is the entry point that actually exercises the Phase 7 mesh-craft
pipeline (and therefore the already-fixed "infinite slab" weight-blending logic) for every real
avatar export — not the original `generate_body.py`/`generate_clothes.py` path.

## Missing or Weak Tests
The `__main__` block's own assertions only check the export file exists and is non-empty — it does
not independently re-verify vertex/weight-group counts the way `generate_body.py`'s own `__main__`
does (that verification happens one layer down, inside each `build_*()` call it makes... actually,
note: `build_avatar()` itself does NOT call each module's own `__main__`-style self-checks, only
their `build_*()` functions — so a regression in e.g. zero-weight-vertex handling would only be
caught by running `generate_body_meshcraft.py`/`generate_clothes_meshcraft.py`'s own standalone
`__main__` blocks directly, not by running `generate_avatar.py`). Not flagged as a defect (each
module's own standalone verification remains available and documented in the README's own
"Verify:" instructions per script), but worth noting for anyone assuming `generate_avatar.py`'s own
successful run alone constitutes full verification.

## Positive Findings
The explicit aliasing comments make the Phase 7 migration path (mesh-craft over bpy-primitives)
unambiguous to a future reader, rather than requiring them to trace import statements to discover
which implementation is actually active.

## Final Assessment
No findings.
