# Audit: docs/d3d9-divergence-report.md

## Metadata
- Source file: `docs/d3d9-divergence-report.md` (323 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown measurement report
- XNA/FNA relevance: the D3D9 backend's pixel-diff-vs-real-XNA measurement, plus a cross-backend
  (EasyGL) comparison

## Purpose
The evidentiary deliverable behind the D3D9 backend's "indistinguishable from XNA 4.0" claim: a
31-scene oracle corpus at `tolerance=0`, its two real bugs found and fixed along the way, an explicit
"not yet measured" gap table, and a cross-backend (EasyGL) measurement showing 10/31 pixel-perfect
with three named divergence patterns for the rest.

## Executive Verdict
One of the most rigorous, self-critical measurement documents in the entire `docs` corpus. Its
explicit framing — "a short divergence list produced by not looking hard enough... would not be [a
triumph]" — and its dedicated "Not yet measured" table (naming *why* each gap isn't closed, not just
that it exists) exemplify the standard this entire audit has been applying to source code, here
applied by the project to its own measurement methodology.

## Checklist Results
- The two "real backend bugs this measurement found" (SpriteSortMode Z-clipping via `zFarPlane=1`;
  `GraphicsProfile` never reaching the real device) are both specific, root-caused, and consistent
  with `docs/depthstencilstate-support.md`'s own account of an unrelated but similarly-shaped
  Vulkan-clip-space gotcha ("a negative Z silently clips away on Vulkan only... keep Z within [0,1]")
  — a plausible, recurring class of clip-space-convention bug across this project's backends, not a
  one-off.
- The EasyGL cross-backend measurement's three-pattern classification (A: rasterization-boundary-only,
  B: whole-primitive-but-imperceptible rounding, C: real large divergence) is methodologically sound —
  histogramming per-scene differing-pixel counts into small/large buckets before drawing conclusions
  is exactly the discipline needed to avoid over- or under-stating a diff tool's "X pixels differ"
  raw count.
- `fog_gradient_quad`'s Pattern-C finding (EasyGL renders solid black where XNA produces a gradient)
  is a genuinely new, concrete, actionable bug report — consistent with, and a useful supplement to,
  this session's own graphics-backend shard audits, which did not specifically flag EasyGL's
  negative-`FogEnd` handling.

## Detailed Findings
None against this document's own claims — it is a measurement report, and its self-disclosed
boundaries (six project-wide divergences, four unmeasured/partially-measured) are exactly the kind of
honest scope statement this audit values.

## Cross-File Observations
- Directly and consistently corroborated by `docs/d3d9-backend.md`'s own "Current result: 0/31 scenes
  diverge" headline claim.
- The `fog_gradient_quad`/`envmap_fresnel_quad` EasyGL findings are new, unreconciled candidate bugs
  this session's own `xna-graphics`/backend-easygl shard audits (already AUDITED, no HIGH findings for
  fog specifically) did not separately surface — worth flagging as a candidate follow-up item for a
  future EasyGL-focused audit pass, since this document explicitly states neither was "investigated
  further or fixed, per this task's own explicit rule."

## Missing or Weak Tests
The document itself is the test/measurement record; its own "Not yet measured" table is the honest
disclosure of what remains uncovered (render-targets-as-texture, non-`Color` surface formats,
`SpriteSortMode.Immediate`/`.Texture`, NPOT-wrap-on-Reach, hardware-instancing's HiDef gate).

## Positive Findings
The explicit test-design principle stated in this document — measuring both "small" and "large"
per-pixel deltas separately rather than reporting one flat "N pixels differ" number — is a genuinely
transferable methodology lesson that prevented a false "EasyGL is 21/31 broken" headline from masking
the real signal (17 of those 21 are imperceptible rasterization-boundary noise, only 2 are real bugs).

## Final Assessment
No findings against this document. It surfaces two concrete, unreconciled EasyGL bug candidates
(`fog_gradient_quad`'s negative-FogEnd handling, `envmap_fresnel_quad`'s Fresnel interpolation) worth
flagging for a future EasyGL-focused follow-up, consistent with this document's own explicit
"logged here, not fixed" scope boundary.
