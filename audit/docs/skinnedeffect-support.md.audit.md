# Audit: docs/skinnedeffect-support.md

## Metadata
- Source file: `docs/skinnedeffect-support.md` (174 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (closes Phase 46, `plans/plan_graphics.md` Tasks 401-410)
- Cross-references: `xna-graphics` shard audit (`SkinnedEffect` not among that shard's 6 HIGH
  findings — a disjoint area); the "infinite slab" bone-weight-blending investigation from
  persistent memory (avatar/skinning-adjacent, but a different subsystem —
  `tools/avatar_builder/generate_body.py`, not this `SkinnedEffect` GPU-skinning path)

## Purpose
Documents `SkinnedEffect`'s property/default audit, a `Clone()` bug (specular fields not
re-threaded to fresh `EffectParameter`s), bone-count bounds-checking, and progressively more complex
pixel verification (identity palette → single bone → two-bone weighted blend → multi-quad
composition) across EasyGL/Vulkan/Bgfx.

## Executive Verdict
A methodologically exemplary phase-closing document. §6 (two-bone weighted blend)'s test-design
reasoning — deliberately choosing a non-trivial 50/50 split between two bones with *different*
translation amounts specifically so a bug that silently picked only one bone would produce a
*visibly wrong*, not coincidentally-correct, result — is precisely the kind of discriminating-power
engineering this audit values, and the document goes further by empirically demonstrating that
discriminating power (temporarily changing the weights and confirming the predicted change appears).

## Checklist Results
- §1's `Clone()` bug description explicitly identifies it as "the identical architectural bug shape
  Task 392 already fixed for `FogColor`" across 4 other stock effects — correctly recognizing a
  recurring pattern (stale `EffectParameter` cache after copy-construction) rather than treating each
  occurrence as a novel, unrelated bug.
- §4's independent-copy-semantics test for `GetBoneTransforms` (mutating the first call's returned
  vector and confirming a second call is unaffected) is a real, falsifiable aliasing check — "an
  alias-based implementation would have let the mutation corrupt the underlying storage," a precise
  statement of what the test would catch.
- §4's Bgfx test-harness pitfall discovery (reading 3 distinct screen rectangles within one
  retry-loop iteration only reliably reflects the first read) is disclosed as a **test-methodology**
  finding, not a `SkinnedEffect` or backend bug — correctly scoped, and the fix (a `renderAndRead()`
  helper) is described as becoming the new standard pattern reused by every subsequent multi-point
  Bgfx test in the phase, a genuine methodology improvement with forward propagation.
- §6's discriminating-power self-check (temporarily reverting weights to `(1,0)`, confirming the
  predicted `-0.5` shift instead of `+0.5` appears, then restoring the real test) is one of the
  clearest "we proved our test would actually catch the bug it claims to catch" demonstrations in
  this entire documentation shard.

## Detailed Findings
None. All specific claims are precise, falsifiable, and either directly verified in-document or
consistent with this audit's own `xna-graphics` shard review (no `SkinnedEffect`-specific HIGH
finding there, and no contradiction with this document's own findings).

## Cross-File Observations
`SkinnedEffect`'s GPU bone-palette blending path documented here (up to 4 weighted bones via
`WeightsPerVertex`) is a genuinely separate subsystem from the `tools/avatar_builder/generate_body.py`
"infinite slab" bone-weight-blending defect this audit tracked from persistent cross-session memory
(that defect was in the *avatar-mesh-authoring tool's* automatic vertex-weight assignment heuristic,
not in `SkinnedEffect`'s own GPU consumption of already-assigned weights) — no overlap, no shared
root cause; flagging only to note the two are easily confusable by name/domain but are unrelated
code paths, already independently confirmed fixed/closed in the `tools-avatar-builder` shard audit
earlier this session.

## Missing or Weak Tests
None identified — the document's own account of test coverage (52 tests added from a zero-coverage
starting point, per §2) appears thorough for the scope described.

## Positive Findings
The two-bone-weighted-blend discriminating-power self-check (§6) and the Bgfx test-harness-pitfall
discovery-and-forward-fix (§4) are both genuinely strong examples of rigorous test-engineering
practice, worth highlighting as models for the rest of the codebase.

## Final Assessment
No findings. Methodologically exemplary; explicitly and correctly distinguished from the unrelated
avatar-mesh "infinite slab" bone-weighting defect tracked elsewhere in this audit's memory.
