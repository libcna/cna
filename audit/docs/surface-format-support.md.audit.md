# Audit: docs/surface-format-support.md

## Metadata
- Source file: `docs/surface-format-support.md` (260 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (source-inspected against Tasks 174/281-284, Phase 32-ish)
- Cross-references: `xna-graphics` shard audit (no contradicting finding); `docs/texture3d-texturecube-support.md`
  (same "format parameter silently ignored" pattern, audited in this same batch)

## Purpose
Documents the canonical 27-value `SurfaceFormat` enum audit (a real ordinal-mismatch bug found and
fixed), per-format CPU-size helpers, a real Vulkan sRGB/gamma bug found via mid-range-value testing,
and the current state that only `SurfaceFormat::Color` is genuinely GPU-supported — every other
format silently becomes RGBA8.

## Executive Verdict
An unusually candid gap-disclosure document — the "How format selection works (current state)"
section states plainly that "every texture, regardless of the requested `SurfaceFormat`, is stored
on the GPU as RGBA8 unorm," with a full 26-row format table showing ❌ for everything except
`Color`/DXT-via-CPU-decompress. The Task 284 Vulkan sRGB bug narrative is a standout piece of
verification methodology: recognizing that saturated (0/255) test colors are fixed points of the
sRGB transfer curve and therefore *cannot* reveal a linear-vs-sRGB bug, then deliberately choosing a
mid-range (128) test value to expose it.

## Checklist Results
- Task 281's finding (CNA's enum previously had 7 *invented* "Srgb" variants at ordinals 20-26 with
  no FNA equivalent, while omitting FNA's real values at those same ordinals) is a serious,
  correctly-identified violation of this project's own "must match XNA/FNA exactly" rule — the fix
  (real FNA values 20-26) and the blast-radius assessment (only one example test referenced the old
  names, confirmed small) are both precise and verifiable.
- The Task 284 Vulkan sRGB bug's "two bugs partially masked each other for textured content" analysis
  (wrong decode + wrong encode ≈ approximate inverse, so textured content coincidentally read back
  correctly while non-textured content — most of the actual rendering pipeline — was off by 60 units)
  is a sophisticated, correctly-reasoned root-cause account, not just "found a bug, fixed it."
- The verification methodology (`Vulkan_Texture2D_ColorFormat_Linear` ctest comparing a plain-vertex-
  color quad against a textured quad of the identical value) is a real before/after quantified proof
  (188→128 and 128→128), not merely "looks right now."
- Cross-checked the format table's own internal consistency: every row marked ❌ is consistent with
  the "How format selection works" section's blanket claim (no per-row exception contradicts the
  overall "every format becomes RGBA8" statement).

## Detailed Findings
None against this document. The gaps it documents (non-Color formats not implemented) are real,
already-disclosed, cross-backend limitations — not contradicted by this audit's own `xna-graphics`
shard review.

## Cross-File Observations
The same "format parameter accepted but silently ignored, GPU always uses RGBA8" pattern this
document describes for `Texture2D` is independently confirmed, for `Texture3D`/`TextureCube`
specifically, in the sibling `docs/texture3d-texturecube-support.md` (audited in this same batch) —
consistent, convergent evidence of one cross-backend, cross-texture-type architectural limitation
rather than three unrelated gaps.

## Missing or Weak Tests
N/A for a documentation file — the doc's own account of "27 new/existing ordinal-value unit tests"
pinning the corrected enum values suggests solid regression coverage for the one bug this document
describes as fixed (the ordinal mismatch).

## Positive Findings
The Task 284 mid-range-value testing methodology (recognizing that saturated colors are fixed points
of the sRGB curve and therefore blind to a linear-vs-sRGB bug) is one of the sharpest single pieces
of test-design reasoning in this entire documentation shard — it explains not just what was tested
but *why the previous, more extensive test suite couldn't have caught this*, which is a genuinely
valuable meta-observation about test design.

## Final Assessment
No findings. An honestly-scoped gap disclosure (only `Color` format truly works) combined with one
of the most methodologically sophisticated bug-discovery narratives (the Vulkan sRGB masking bug)
in this documentation shard.
