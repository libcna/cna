# Audit: tools/avatar_builder/generate_materials.py

## Metadata
- Source file: `tools/avatar_builder/generate_materials.py` (96 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-avatar-builder` shard
- File type: Python script (Blender `bpy`)
- XNA/FNA relevance: content-generation tooling for CNA's avatar rendering system, not XNA API surface
- Main related tests: self-contained `assert`-based `__main__` check

## Purpose
Creates 5 flat-color Principled BSDF placeholder materials (`Skin`/`Hair`/`Shirt`/`Pants`/`Shoes`)
and assigns `Skin` to the body mesh; the other four are assigned by their respective
garment/hair-generating scripts.

## Executive Verdict
Correct. The `Shoes` color's own comment (lines 44-52) documents a real, precisely-diagnosed fix:
the original `(0.05, 0.05, 0.05)` base color was dark enough that "no amount of ambient/diffuse
light could make joint/seam shading read as anything but a featureless black blob," confirmed via
direct pixel sampling that neither an ambient-light fix nor a bend-joint weight fix changed a
single foot pixel — isolating the flat base color itself as the sole cause, not a lighting or
skinning issue as might otherwise have been assumed.

## Checklist Results
- `build_materials()` correctly reuses an existing same-named material if present
  (`bpy.data.materials.get(name)`) rather than unconditionally creating a duplicate — safe to call
  repeatedly in one Blender session.
- Sets both `mat.diffuse_color` and the Principled BSDF's `Base Color` input — covers both
  Blender's legacy viewport-shading color and the actual shader input used by the real glTF export
  path.

## Detailed Findings
None.

## Cross-File Observations
The `Shoes` color fix comment is a good example of ruling out two other plausible causes (ambient
light, bend-joint weighting) via direct measurement before concluding the base color itself was at
fault — methodologically similar to the rigor shown in `generate_body.py`'s "infinite slab"
diagnosis.

## Missing or Weak Tests
None beyond the general pattern noted in sibling files.

## Positive Findings
The `Shoes` color root-cause investigation (ruling out two other hypotheses via direct pixel
measurement before landing on the actual cause) is a strong example of rigorous debugging, not
guesswork.

## Final Assessment
No findings.
