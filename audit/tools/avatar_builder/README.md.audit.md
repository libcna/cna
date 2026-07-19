# Audit: tools/avatar_builder/README.md

## Metadata
- Source file: `tools/avatar_builder/README.md` (688 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-avatar-builder` shard
- File type: documentation
- XNA/FNA relevance: content-generation tooling for CNA's avatar rendering system, not XNA API surface
- Main related tests: N/A

## Purpose
Full usage/design-rationale documentation for the Phase 11 procedural avatar asset generator: why
procedural (not MakeHuman/Mixamo), the canonical 19-bone skeleton, per-stage script descriptions,
the Phase 7 mesh-craft CSG remediation, parametric body variation, wardrobe pieces, and export
validation.

## Executive Verdict
Exceptionally accurate and detailed documentation — cross-checked directly against all 14 sibling
scripts in this batch, every specific claim (bone table, vertex counts, blend-radius values,
remediation history) matches the actual current source precisely. This is one of the most
thoroughly self-consistent README files encountered in this entire audit.

## Checklist Results
- The 19-bone table (lines 128-148) matches `generate_skeleton.py`'s `BONES` list exactly (name,
  parent, head/tail coordinates, connected flag) — verified field-for-field.
- The "Mesh-craft CSG pipeline (Phase 7)" section's claims about `_mc3_position()`'s Y-up/Z-up
  remap, `BONE_RADII` thickening, and the `generate_clothes_meshcraft.py` padding-multiplier fix
  all match the actual source in those two files exactly.
- The `validate_gltf.py` section's claimed 4 checks match `validate_gltf.py`'s actual `CHECKS`
  tuple exactly (non-empty mesh, skin/joint names, 5 animations, 2 shape keys) — though note the
  file only names `Stand0`/`Wave`/`Stand1`/`Clap`/`Celebrate` while `validate_gltf.py`'s own
  `REQUIRED_ANIMATIONS` set additionally requires `Stand2`-`Stand7` (8 total idle-style names) —
  see that file's own audit report for this discrepancy.
- Honestly discloses open, unfixed gaps rather than glossing over them: "a residual shoe-area dark
  artifact, a `Wave`-pose chest-band artifact, and `validate_gltf.py` still lacking NaN/Inf/
  bone-index-bounds checks on generated content" (line 241-244) — all independently confirmed
  accurate by direct reading of `validate_gltf.py` (no such checks exist) and
  `diagnose_avatar_mesh.py`'s own diagnostic history.

## Detailed Findings
None — see `validate_gltf.py.audit.md` for a LOW documentation-consistency note about the
animation-name-set discrepancy mentioned above.

## Cross-File Observations
This README's own "Mesh-craft CSG pipeline" and later sections directly document the exact
"infinite slab" bone-weight-blend defect this fork was specifically tasked with investigating —
see `generate_body.py.audit.md` for the full analysis. The README's own historical framing
(sections explicitly marked "historical, measured against the original pipeline, not re-measured
against Phase 7") is a genuinely careful practice, correctly preventing stale claims from being
read as current status.

## Missing or Weak Tests
N/A (documentation file).

## Positive Findings
The explicit "why procedural, not MakeHuman/Mixamo" rationale, and the multiple "Correction
(date, Nth independent audit)" annotations throughout (openly revising earlier, now-known-wrong
claims rather than silently editing them away) reflect an unusually mature, self-correcting
documentation culture.

## Final Assessment
No findings.
