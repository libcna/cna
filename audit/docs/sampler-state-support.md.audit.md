# Audit: docs/sampler-state-support.md

## Metadata
- Source file: `docs/sampler-state-support.md` (173 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (closes Phase 35, `plans/plan_graphics.md` Tasks 291-300, with a
  2026-07-11 status-update banner for later fixes)
- Cross-references: `xna-graphics` shard audit (no `SamplerState`-specific HIGH finding)

## Purpose
Documents `SamplerState`'s API/preset audit, the central Phase 35 finding (per-slot sampler binding
silently ignored by all 3D stock-effect draws, 3 different backend-specific root causes), address-
mode/filter/mipmap/anisotropic-filtering verification, with a dated update banner tracking later
fixes (Tasks 918/924-926).

## Executive Verdict
One of the strongest single findings in this documentation shard: §3's three genuinely distinct
root causes (EasyGL missing a call site across 18 draw overloads; Vulkan hardcoding a default
sampler into a cached descriptor set; Bgfx indexing the wrong sampler-flags slot) for what looks
like "one bug" from the outside is a real, valuable piece of engineering analysis — three unrelated
mechanisms producing the same symptom, each requiring its own distinct fix.

## Checklist Results
- The dated 2026-07-11 status banner correctly and specifically identifies which of the document's
  own ❌ markers are now historical (Tasks 867/918 and their splits 924-926), consistent with this
  audit's own observation elsewhere (`docs/model-content-pipeline-support.md`,
  `docs/rendertarget-support.md`) that this project favors dated update banners over silent rewrites.
- §6's mip-aware-filter-on-non-mipmapped-texture finding (GL-incompleteness → solid black) is
  precisely mechanistic: it correctly distinguishes "the filter selection logic is right" from "GPU
  state (`GL_TEXTURE_MAX_LEVEL`) wasn't set to match," and cross-references the identical symptom
  independently re-discovered while building the anisotropic-filtering test (§7) — a real, useful
  cross-reference within the same document rather than treating them as two separate mysteries.
- The summary table's 🔍 legend for Bgfx is applied consistently (no GPU pixel-readback API in this
  project) — matches this audit's own observation of the same convention in
  `docs/rasterizerstate-support.md`.

## Detailed Findings
None. All specific technical claims are precise, task-cited, and consistent with the `xna-graphics`
shard audit's own findings (no contradiction, no overlapping/duplicated bug).

## Cross-File Observations
Consistent in methodology and update-banner convention with `docs/rasterizerstate-support.md`/
`docs/rendertarget-support.md` (same `plans/plan_graphics.md` phase-sequence family) — all three documents
share the same disciplined "dated update note, not silent rewrite" and "🔍 = not verified this
phase, not silently omitted" conventions, reinforcing that this is a deliberate project-wide
documentation practice, not incidental to any one file.

## Missing or Weak Tests
The doc's own "Open, tracked follow-up work" section discloses Bgfx's anisotropic `maxAnisotropy`
level is still ignored (only on/off) — an honestly disclosed, real, still-open gap.

## Positive Findings
§3's three-distinct-root-causes-for-one-symptom analysis is exactly the kind of rigor this entire
audit tries to model in its own findings — resisting the temptation to file "sampler state doesn't
work" as one bug and instead tracing each backend's own genuinely different failure mechanism to its
specific fix.

## Final Assessment
No findings. Rigorous, well-cross-referenced, and consistent with sibling Phase 35/38/39 documents
and this audit's own `xna-graphics` shard review.
