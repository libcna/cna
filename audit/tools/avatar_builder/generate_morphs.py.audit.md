# Audit: tools/avatar_builder/generate_morphs.py

## Metadata
- Source file: `tools/avatar_builder/generate_morphs.py` (154 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-avatar-builder` shard
- File type: Python script (Blender `bpy`)
- XNA/FNA relevance: content-generation tooling for CNA's avatar rendering system, not XNA API surface
- Main related tests: self-contained `assert`-based `__main__` check

## Purpose
Adds `Smile`/`Blink` shape keys to the body mesh's placeholder sphere-head, selecting vertices by
latitude-ring ratio (not absolute position, so it stays correct across `head_scale`) since there is
no separate modeled eye/mouth geometry yet.

## Executive Verdict
Correct, explicitly and honestly scoped as crude/placeholder — the docstring is clear that this
ring-selection approach should be replaced entirely (not incrementally refined) once real facial
geometry exists, matching the README's own "Adding more morphs later" guidance.

## Checklist Results
- `_at_latitude_ratio()`'s ratio-based (not absolute-position) selection correctly generalizes
  across `head_scale`, per its own docstring's reasoning — confirmed the UV-sphere-latitude math
  (fixed `z/radius` ring positions for a `segments=8`/`ring_count=6` sphere) is consistent with
  `generate_body.py`'s own head-sphere construction.
- `_add_shape_key()` correctly replaces an existing same-named shape key rather than stacking
  duplicates (`shape_key_remove` before `shape_key_add`), and correctly transforms the displacement
  from world space back to local mesh space via `matrix_inv` before writing `key.data[i].co`.
- The `__main__` block's `_max_displacement()` check verifies each shape key actually displaces at
  least one vertex by a non-trivial amount (`> 1e-4`) — a real, meaningful assertion, not just
  "the shape key object exists."

## Detailed Findings
None.

## Cross-File Observations
`_head_center_and_radius()` here is a near-duplicate of the identically-named helper in
`generate_hair.py` — both independently derive the same head center/radius from the same bone data,
consistent with each other's math but duplicated rather than shared from one place. A minor,
low-priority DRY opportunity, not a correctness issue (both were verified to compute the same
formula).

## Missing or Weak Tests
None beyond the general pattern noted in sibling files.

## Positive Findings
The explicit, upfront acknowledgment that this approach is a placeholder to be replaced (not
refined) once real facial geometry exists is a good example of scoping a known-crude solution
honestly rather than implying it's more capable than it is.

## Final Assessment
No findings.
