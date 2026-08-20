# Audit: docs/environmentmapeffect-support.md

## Metadata
- Source file: `docs/environmentmapeffect-support.md` (177 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (phase-closure support matrix)
- XNA/FNA relevance: documents `EnvironmentMapEffect` conformance across EasyGL/Vulkan/Bgfx
- Related audit: `xna-graphics` shard (this session)

## Purpose
Closes Phase 45 (`plans/plan_graphics.md` Tasks 391-400): property/default audit, cube-map blend-formula
fix (additive → `mix()`/lerp), `EnvironmentMapSpecular` alpha scaling, Fresnel edge-weighting,
`EyePosition`/reflection-vector correctness, `World`/normal-matrix correctness, and cross-backend
consistency.

## Executive Verdict
Accurate and internally consistent, including with `docs/dualtextureeffect-support.md` (audited
alongside this file) on the shared Task 868/899 timeline. A genuinely valuable methodological
throughline: Task 394's cube-map blend fix explicitly reuses the "0/1-saturation blind spot" lesson
`docs/dualtextureeffect-support.md`'s Task 383 first identified, and deliberately designs a
non-saturating gray-cubemap test case to avoid repeating it — a real demonstration of institutional
learning across phases, not just a coincidental parallel structure.

## Checklist Results
- The `Clone()` `FogColor` bug (affecting 4 stock effects: `AlphaTestEffect`/`DualTextureEffect`/
  `EnvironmentMapEffect`/`SkinnedEffect`) is a specific, plausible, and cross-checkable claim — a
  test written for one effect (`EnvironmentMapEffectTests.cpp`'s `Clone()` case) surfacing a
  shared-code bug affecting 3 sibling effects is a realistic bug-discovery shape consistent with
  this project's general architecture (shared `CacheEffectParameters()` logic).
- Task 398's finding that EasyGL and Bgfx both transformed normals incorrectly under non-uniform
  scale (raw `World` upper-left 3x3 / `mul(u_world, vec4(normal,0))`) while Vulkan was already
  correct (`transpose(inverse(mat3(world)))` in-shader) is specific and technically sound — the
  cofactor/determinant-shortcut fix described is the standard, correct technique for this class of
  bug.
- Task 398's own forward reference opening Task 892 ("Bgfx's sibling `BasicEffect` lit-textured
  shader has a worse bug — transforms the normal by the full World×View×Projection matrix, not even
  World alone") is presented as deliberately out-of-scope for this document, not silently dropped —
  correctly disclosed as a new, distinct, tracked follow-up.
- The "Open, tracked follow-up work" section's disposition of Task 890 (fixed same day) and Task 891
  (fixed) as struck-through, versus Task 892 (still open), correctly distinguishes closed from open
  items rather than presenting everything as uniformly resolved.

## Detailed Findings
None.

## Cross-File Observations
- Consistent with `docs/dualtextureeffect-support.md` (audited alongside this file) on Task
  868/899's fix dates.
- Task 892 (Bgfx `BasicEffect` normal-transform bug, opened here) was not independently
  cross-checked against `docs/basiceffect-support.md` in this batch (not part of this fork's 24
  assigned files) — a natural follow-up for whoever audits that document, to confirm whether Task
  892 is reflected there or remains only tracked in this cross-reference.

## Missing or Weak Tests
N/A for this document itself — it describes test authoring (42 new tests for
`EnvironmentMapEffectTests.cpp`) rather than needing its own tests.

## Positive Findings
The deliberate reuse of the "0/1-saturation blind spot" lesson from `DualTextureEffect`'s own phase,
and the explicit non-saturating gray-cubemap test design that resulted, is a strong example of
methodological knowledge actually propagating between phases rather than each phase rediscovering
the same class of testing mistake independently.

## Final Assessment
No findings. An accurate, well-cross-referenced phase-closure document that demonstrates real
cross-phase methodological learning.
