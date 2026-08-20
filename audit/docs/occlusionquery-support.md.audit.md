# Audit: docs/occlusionquery-support.md

## Metadata
- Source file: `docs/occlusionquery-support.md` (159 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (closes Phase 50, `plans/plan_graphics.md` Tasks 441-450, plus the
  later Task 447/854 Vulkan resolution)
- Cross-references: `xna-graphics` shard audit (no `OcclusionQuery`-specific HIGH finding)

## Purpose
Documents `OcclusionQuery`'s FNA API-surface audit (notably: FNA has zero Begin/End sequence
validation), the per-backend support matrix (EasyGL/Vulkan fully correct, Bgfx fixed-with-caveats,
SDL_Renderer correctly unsupported), and the full Vulkan architecture fix (3 design questions
resolved for deferred-command-recording query correlation).

## Executive Verdict
A rigorous, mechanism-precise document. The Vulkan fix narrative (§"Vulkan — fixed, all 3 design
questions resolved") is one of the most technically specific pieces of documentation reviewed in
this shard — it explains not just what was fixed but the exact architectural reason the original gap
existed (deferred command recording separating `Begin()`/`End()`'s synchronous call from the actual
`vkCmdBeginQuery`/`vkCmdEndQuery` recording time) and the 3 non-obvious design questions that had to
be resolved to fix it correctly rather than superficially.

## Checklist Results
- The "Critical finding: FNA has ZERO C#-level validation of Begin/End call sequence" claim directly
  corrected the project's own prior task framing (Tasks 442-444 were titled "...Match FNA exception"
  before this task found there is no such exception) — a genuine, self-disclosed process correction,
  not hidden.
- The Bgfx section's "Caveat 1" (sandbox cannot distinguish fixed-vs-broken because Mesa Lavapipe's
  software GL 2.1 returns a non-`NoResult` value even for a query that was never submitted anywhere)
  is a precise, falsifiable, and honestly-scoped environment limitation — explicitly cross-referenced
  against "this project's own already-established `Bgfx_RenderTarget2D_MsaaResolve`/Vulkan-DRI3-
  unavailable precedent for this exact sandbox," i.e., not a novel excuse but consistent with a
  pattern already established elsewhere in the project's own test-limitation disclosures.
- The Vulkan multi-draw-span policy description (§ point 2) precisely states what is and isn't
  covered: a query may span multiple draws within the same render pass, but a query spanning a
  render-pass boundary is deliberately NOT re-opened, "avoiding a Vulkan validation error... rather
  than attempting to correctly sum results across multiple render passes (a real FNA capability this
  implementation doesn't fully cover, documented here rather than silently assumed)" — an honest,
  explicit scope-cut disclosure.

## Detailed Findings
None. Every specific technical claim in this document is precise, falsifiable, and either verified
directly (test names/pass counts cited) or explicitly hedged where a limitation exists.

## Cross-File Observations
No `OcclusionQuery`-specific finding exists in the `xna-graphics` shard audit's own 6 HIGH findings
— this document's own findings (all already fixed, or explicitly caveated where not) occupy a
disjoint, non-contradicting area.

## Missing or Weak Tests
The doc's own summary table honestly marks Bgfx's pixel-level correctness as unverifiable in this
sandbox — a self-disclosed gap, not contradicted by anything in this audit's own review.

## Positive Findings
The "Critical finding: FNA has ZERO C#-level validation" correction is a genuinely valuable
documentation practice — catching and disclosing that the project's own prior task titles were based
on a wrong premise (assuming FNA throws a specific exception when it doesn't), rather than silently
re-titling the tasks and losing that lesson. The multi-draw-span policy's explicit "this doesn't
fully cover X, documented here rather than silently assumed" framing is exactly the honesty this
audit values throughout its own findings methodology.

## Final Assessment
No findings. One of the more technically rigorous, mechanism-precise documents in this shard.
