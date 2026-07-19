# Audit: docs/shader-effect-vs-fx-bytecode.md

## Metadata
- Source file: `docs/shader-effect-vs-fx-bytecode.md` (123 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (Task 354, developer-facing custom-shader guide)
- Cross-references: `docs/migration-guide.md` (cites the compiled-`.fx`-bytecode gap as one of "the
  two gaps that actually matter to most ports" — consistent with this doc's own framing)

## Purpose
A practical guide distinguishing what works today for custom shaders (`ShaderEffect`, `NOXNA`,
hand-written GLSL/SPIR-V source) from what doesn't (loading a real compiled XNA `.fx` bytecode
blob), plus the Phase 74 roadmap for eventually supporting the latter via a vendored MojoShader.

## Executive Verdict
Correct and consistent with `docs/migration-guide.md`'s own framing of this exact gap as one of the
two most porting-relevant limitations. The interim guard's exact thrown message is quoted verbatim
and is itself a model example of a good error message — it names the exact API, the exact reason,
the tracking doc, and the two concrete alternatives (stock effect or `ShaderEffect`), not just "not
implemented."

## Checklist Results
- `Bgfx's ShaderEffect backend is currently a no-op stub` is a strong, specific claim (both source
  strings accepted but ignored) — not contradicted by anything in this audit's own `backend-bgfx`
  shard review (which was fully audited earlier this session; no note found there disputing this
  characterization, though this pass did not re-cross-check `BgfxGraphicsBackend::CreateEffectBackend`
  directly against this specific claim).
  - Correction note: `docs/xna-4-api-coverage.md`'s own 3D compatibility checklist (surfaced via
    `docs/migration-guide.md`, audited earlier in this pass) independently confirms this exact same
    claim ("`ShaderEffect` (NOXNA custom shader) | ✅ (constructor exists on all 3) | ✅ / ✅ / ❌ |
    Bgfx's `CreateEffectBackend` returns `nullptr` for it") — a second, independent document
    corroborating the same fact.
- The roadmap's step-by-step Phase 74 plan (vendor MojoShader → wrap its effect-parsing API →
  EasyGL via direct GLSL transpile → Vulkan via a second GLSL→SPIR-V hop → Bgfx needs its own
  feasibility investigation) is a realistic, appropriately-uncertain plan — correctly flags Bgfx as
  needing separate investigation rather than assuming the same recipe applies uniformly.
- The practical porting guidance (hand-port HLSL to GLSL/SPIR-V and use `ShaderEffect` directly,
  since "the original `.fx`/HLSL source is usually available") is sound, actionable advice — not an
  empty "this doesn't work yet" dead end.

## Detailed Findings
None.

## Cross-File Observations
Independently corroborated by `docs/xna-4-api-coverage.md`'s 3D compatibility checklist (surfaced via
`docs/migration-guide.md`) on the exact same Bgfx `ShaderEffect` no-op claim — convergent evidence
from two separately-authored documents.

## Missing or Weak Tests
N/A for a documentation file.

## Positive Findings
The thrown `NotImplementedException` message quoted in this document is genuinely exemplary error-
message design: names the exact constructor, states the exact reason, points to the tracking
document, and offers two concrete alternatives — this is worth citing as a model for other
"not yet implemented" guards in the codebase.

## Final Assessment
No findings. Accurate, well-cross-referenced, and independently corroborated by a second document
on its central Bgfx-specific claim.
