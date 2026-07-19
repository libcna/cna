# Audit: tools/avatar_builder/generate_clothes.py

## Metadata
- Source file: `tools/avatar_builder/generate_clothes.py` (195 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-avatar-builder` shard
- File type: Python script (Blender `bpy`)
- XNA/FNA relevance: content-generation tooling for CNA's avatar rendering system, not XNA API surface
- Main related tests: self-contained `assert`-based `__main__` check

## Purpose
Builds the original (pre-Phase-7) placeholder Shirt/Pants/Shoes garment shells as offset
cylinder+joint-sphere primitives over the body's own bone radii, with per-slot style variants
(Task 11.14: `TShirt`/`LongSleeve`, `Pants`/`Shorts`, `Shoes`).

## Executive Verdict
Correct. Reuses `generate_body.fix_automatic_weights()` unchanged (line 160) — confirmed to inherit
the "infinite slab" fix documented in `generate_body.py.audit.md` rather than risk a divergent
reimplementation.

## Checklist Results
- The `Shoes` style's own comment (lines 78-92) documents a real, measured, previously-fixed defect
  (a shoe shell covering only `Foot` bones geometrically crossed the leg's own capsule at the ankle,
  confirmed via "18.9% of shoe vertices were strictly inside the body, worst -74mm") and its fix
  (extending coverage to `LowerLeg.L/R` so the shell strictly encloses rather than crosses the leg,
  while staying smaller than the Pants shell at the same bone so the added shaft stays hidden) —
  a concrete, quantified fix, not just a description.
- `build_clothes()`'s `styles` parameter correctly defaults missing slots to `DEFAULT_STYLES` via
  dict-merge (`{**DEFAULT_STYLES, **(styles or {})}`), so partial overrides (e.g. only `Shirt`)
  don't accidentally reset other slots to `None`.
- The `__main__` block's independent re-verification of zero-weight/over-limit vertex counts (lines
  187-191) mirrors `generate_body.py`'s own good practice of not trusting `fix_automatic_weights()`'s
  return value alone.

## Detailed Findings
None.

## Cross-File Observations
Reuses `generate_body.fix_automatic_weights()` — see `generate_body.py.audit.md` for the "infinite
slab" fix this inherits. `generate_clothes_meshcraft.py` is the production drop-in replacement for
this module (this original remains standalone-runnable but is no longer what `generate_avatar.py`
actually calls).

## Missing or Weak Tests
Same gap as `generate_body.py`: no automated cross-body-part weight-bleeding check in this file's
own `__main__` self-test.

## Positive Findings
The `Shoes`-style fix comment's quantified before/after measurement (18.9% strictly-inside,
-74mm worst) is a strong example of a real, verified fix rather than a guessed adjustment.

## Final Assessment
No findings.
