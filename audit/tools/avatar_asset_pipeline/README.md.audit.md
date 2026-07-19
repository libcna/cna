# Audit: tools/avatar_asset_pipeline/README.md

## Metadata
- Source file: `tools/avatar_asset_pipeline/README.md` (155 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-avatar-asset-pipeline` shard
- File type: documentation
- XNA/FNA relevance: documents an offline asset-preparation workflow for `AvatarRenderer`'s
  real-rendering extension (NOXNA, no FNA equivalent — Avatar real-rendering is a CNA extension)
- Main related tests: N/A

## Purpose
Documents `convert_avatar.py`'s purpose, its verification history against both a synthetic fixture
and real generated content (CNA's own procedural Blender pipeline), the manual (non-automatable)
MakeHuman/Mixamo steps it depends on for an alternative content source, and a clip-name-to-Mixamo-
source mapping table.

## Executive Verdict
Exceptionally thorough, accurate root-cause documentation — this is one of the more detailed
"here's exactly what broke and why" write-ups found in this audit's tooling sweep. It documents
four distinct real bugs found and fixed via actual end-to-end verification (not just static
review): a data-block-vs-object naming leak, a CLI assumption mismatch between the MakeHuman/Mixamo
workflow and CNA's own bundled-`.glb` pipeline, a joint-index-remap omission, and a matrix-
transpose bug — plus a fifth bug identified as living in the C++ engine itself
(`ContentManager.cpp`'s `SkinnedModelTypeReader`), correctly attributed there rather than
mis-attributed to this Python tool.

## Checklist Results
- The matrix-transpose bug's explanation (lines 57-64) is precise about *why* the fix was
  "stop transposing," not "transpose the other way": glTF's column-major and CNA's row-major
  conventions for the *same* transform are byte-identical, so transposing was actively wrong, not
  just backwards — a subtle point correctly reasoned through rather than fixed by trial and error.
- The "Manual steps" section (lines 73-102) honestly documents exactly why this can't be automated
  (MakeHuman GUI, Mixamo's account-gated browser download flow) rather than leaving that unstated,
  and includes a specific verification instruction for an assumption ("Verify this assumption once
  both bodies exist... if MakeHuman's male and female base meshes turn out to have divergent
  rest-pose bone orientations...").
- The clip-mapping table (lines 111-143) explicitly flags the 2 presets with no exact Mixamo match
  ("**Documented substitute needed**") rather than silently picking an arbitrary clip, and instructs
  using `AvatarAnimation::SetRealClipNameEXT(...)` for any substituted clip — connecting this
  offline tooling correctly back to the real C++ API.

## Detailed Findings
None in this file itself; see `convert_avatar.py.audit.md` for one LOW/plausible-edge-case note
about `build_node_hierarchy()`'s topological-sort logic that this README's own bug history doesn't
appear to cover.

## Cross-File Observations
Documents `convert_avatar.py` (audited alongside this file) precisely — every specific bug
described here (naming leak, `--embedded-clips` flag, `joint_index_remap`, `bind_pose_local`
derivation) is directly traceable to a specific, still-present comment in that script.

## Missing or Weak Tests
N/A (documentation file); see `convert_avatar.py.audit.md` for the corresponding tool's own test
coverage note.

## Positive Findings
This is a model example of "root cause every bug, don't just patch the symptom" documentation —
each of the four bugs' write-ups explains not just what was wrong but how it was isolated (e.g. "a
forced-identity-bones diagnostic render... proved the camera/mesh/shader path was already correct,
isolating the bug to exactly this matrix convention question").

## Final Assessment
No findings.
