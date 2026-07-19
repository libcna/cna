# Audit: tools/avatar_builder/validate_gltf.py

## Metadata
- Source file: `tools/avatar_builder/validate_gltf.py` (142 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-avatar-builder` shard
- File type: Python script (plain `python3`, `pygltflib` — no Blender needed)
- XNA/FNA relevance: content-generation tooling for CNA's avatar rendering system, not XNA API surface
- Main related tests: N/A (this IS a validation/test tool)

## Purpose
Post-export sanity check for a generated avatar `.glb`: non-empty mesh, all 19 canonical bones
present as skin joints, all required animations present, both `Smile`/`Blink` shape keys present —
fails loudly with a specific `FAIL: <reason>` message and nonzero exit on the first miss.

## Executive Verdict
Correct, and genuinely fails loudly rather than silently accepting a broken export — confirmed by
the README's own account of having been tested against a nonexistent path, a garbage file, a
Wave-stripped copy, and a Blink-removed copy, each producing a distinct correct `FAIL:` message.
One LOW documentation-consistency note: `REQUIRED_ANIMATIONS` (line 41-44) requires 11 names
(`Stand0`-`Stand7`, `Wave`, `Clap`, `Celebrate`), but the README's own "Placeholder animations"
section (and `generate_animations.py`, presumably) only documents 5 as actually authored
(`Stand0`, `Stand1`, `Wave`, `Clap`, `Celebrate`) — `Stand2`-`Stand7` are required by this check
but not described anywhere in the README as scripts that generate them. This is either (a) a
forward-looking requirement anticipating animations not yet built by
`generate_animations.py` (in which case a real `male_avatar.glb`/`female_avatar.glb` export would
currently FAIL this check), or (b) `generate_animations.py` (not in this fork's assigned batch —
audited by a sibling fork or not yet covered) does build all 8 `Stand*` variants and the README is
simply out of date. Flagged as worth resolving — if (a), this is a real, currently-broken
validation gate for the documented workflow; if (b), only the README needs updating.

## Checklist Results
- `_check_skin_joints()` correctly treats extra joints beyond the 19 canonical bones (e.g. a
  synthetic `neutral_bone`) as informational, not a failure — matching the README's own documented
  acceptance of this Blender-exporter artifact.
- `_check_shape_keys()` correctly reads `mesh.extras.get("targetNames", [])`, the documented
  location Blender's glTF exporter records shape-key names — not guessing at a different, wrong
  glTF extension field.
- The confirmed missing NaN/Inf/bone-index-bounds checks (per the README's own "Smaller gaps
  remain open" note) are an honestly-disclosed, known gap, not a silent omission this audit is the
  first to notice.

## Detailed Findings

### LOW — `REQUIRED_ANIMATIONS` requires 8 `Stand*` names, but only 2 (`Stand0`/`Stand1`) are
documented as actually built anywhere in this batch
See Executive Verdict above for the full analysis. Worth a follow-up check: does
`generate_animations.py` (outside this fork's assigned file list) actually build `Stand2`-`Stand7`?
If not, running `validate_gltf.py` against a real, current `male_avatar.glb`/`female_avatar.glb`
export would fail this specific check, contradicting the README's own claim that "Both real
`male_avatar.glb`/`female_avatar.glb` pass clean" (line 687).

## Cross-File Observations
`REQUIRED_BONE_NAMES` is correctly derived from `generate_skeleton.BONES` via import (not
duplicated) — the one cross-file dependency this bpy-free script has, confirmed working as intended
per `generate_skeleton.py`'s own lazy-bpy-import design specifically enabling this.

## Missing or Weak Tests
The NaN/Inf/bone-index-bounds checks the README itself flags as still missing would be a genuine,
valuable addition — malformed float data or an out-of-range joint index in a generated asset could
currently pass this validator undetected.

## Positive Findings
The "fails loudly with a specific message" design, verified against multiple distinct malformed-
input scenarios per the README's own account, is exactly the right shape for a content-validation
gate.

## Final Assessment
One LOW finding: a possible mismatch between `REQUIRED_ANIMATIONS`'s 8 `Stand*` entries and what's
actually documented as built — worth a quick follow-up check against `generate_animations.py`.
