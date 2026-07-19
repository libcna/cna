# Audit: tools/avatar_builder/diagnose_avatar_mesh.py

## Metadata
- Source file: `tools/avatar_builder/diagnose_avatar_mesh.py` (255 lines)
- Audit status: AUDITED (full read) — directly relevant to this fork's "infinite slab" bone-weight
  investigation, since this is the tool that originally located and measured that defect
- Subsystem: `tools-avatar-builder` shard
- File type: Python script (Blender `bpy`/`bmesh`/`mathutils.bvhtree`)
- XNA/FNA relevance: content-generation tooling for CNA's avatar rendering system, not XNA API surface
- Main related tests: N/A (this IS a diagnostic tool)

## Purpose
Mesh-level (not visual-render-level) diagnostics for a generated avatar `.glb`: `crossings`
(surface-to-surface intersection detection via BVH nearest-point + signed distance),
`normals` (CSG normal-singularity detection at coincident vertex positions), and `weights`
(per-bone skin-weight influence summary) — explicitly built to measure the *cause* of a rendering
defect numerically, not just visualize the symptom.

## Executive Verdict
Correct, and this file's own `weights` check (`check_weights()`) is confirmed, by direct
cross-reference in `generate_body.py`'s own fix comment, to be the actual tool that measured the
"infinite slab" defect's real symptom (`CNAAvatarPants` weighted to `Shoulder.L`/`Shoulder.R`, 108/107
vertices) — this is genuinely the diagnostic instrument behind that fix, not just a plausible
candidate.

## Checklist Results
- `PosedGeometry`'s own docstring (lines 62-69) documents a real, previously-found blind spot: the
  original `crossings` check only ever read bind-pose (`obj.data` directly), missing any
  pose-dependent defect (a Wave-pose shoulder/chest fragment invisible in T-pose measurements) —
  fixed by evaluating the depsgraph to get genuinely deformed, posed geometry when a `pose=<Clip>`
  argument is given.
- `check_crossings()`'s sign convention (`(p - loc).dot(bnormals[idx]) < 0.0`) is a standard,
  correct "is this point on the interior side of that surface's nearest point's normal" test.
- `_apply_pose()` correctly handles Blender's glTF-import action-naming convention
  (`"<ClipName>_<ArmatureName>"`, not an exact match) via a documented prefix-match fallback — a
  real, previously-discovered import quirk, not an assumption.

## Detailed Findings
None. This file is itself the diagnostic instrument, not a defect site — its own logic is sound for
its stated purpose (measuring symptoms, not rendering them for human judgment).

## Cross-File Observations
`check_weights()`'s output format (per-bone vertex counts) exactly matches the numbers cited in
`generate_body.py`'s "infinite slab" fix comment (108/107 vertices for `CNAAvatarPants` near
`Shoulder.L`/`Shoulder.R`) and `generate_clothes_meshcraft.py`'s blend-radius-mismatch comment
(referencing this same check) — confirming this tool is the actual, working measurement instrument
those fix comments cite, not a hypothetical or since-removed one.

## Missing or Weak Tests
This is itself a diagnostic/test tool; not applicable in the usual sense. It has no automated
pass/fail exit code of its own (it prints diagnostic output for human/script interpretation, by
design, per its docstring's framing as measuring cause rather than judging a symptom).

## Positive Findings
This tool's design philosophy — measure the numeric cause (vertex counts, penetration depths, bone
names) rather than only visualize a symptom a human must judge — is explicitly and correctly
credited (in this file's own docstring) as what actually let each of this batch's several
avatar-mesh defects be pinned down and verified, and the cross-references from other files' fix
comments back to this tool's specific check names (`weights`, `crossings`) confirm that claim is
not just self-praise.

## Final Assessment
No findings.
