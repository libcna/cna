# Audit: docs/depthstencilstate-support.md

## Metadata
- Source file: `docs/depthstencilstate-support.md` (174 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown feature-support matrix
- XNA/FNA relevance: describes `DepthStencilState` conformance across EasyGL/Vulkan/Bgfx

## Purpose
Summarizes Phase 37's audit and fix history for `DepthStencilState`: API surface, default state on
`GraphicsDevice`, depth testing, stencil testing (the central finding — Vulkan's stencil pipeline was
almost entirely fake), and `ReferenceStencil`.

## Executive Verdict
Another example of this project's disciplined self-correction pattern: opens with a status banner
stating Task 870 (Vulkan's fake stencil pipeline, the document's own central finding) is now fixed,
and consistently updates the summary table's Vulkan column to reflect the fix rather than leaving the
original ❌ markers uncorrected. The "Test-design lesson" callout (a test where every check expects the
same pass/fail outcome cannot distinguish "works" from "bypassed entirely") is a genuinely valuable,
transferable testing-methodology insight.

## Checklist Results
- The Vulkan clip-space-Z gotcha ("XNA/DirectX... use a `[0,+w]` clip-space Z range, not OpenGL's
  `[-1,+1]`... a negative Z silently clips away on Vulkan only") is independently corroborated by
  `docs/d3d9-divergence-report.md`'s own account of the D9-93 `zFarPlane=1`/`zFarPlane=-1` bug — the
  same class of clip-space-convention pitfall recurring across two independent documents, both
  correctly diagnosed.
- The open-items list (Task 866 `RasterizerState` `Name` gap, Task 869 by-value vs. FNA's
  reference-type state aliasing, Task 871 `Clear(ClearOptions::Stencil)` ignored on every backend,
  Task 872 `ReferenceStencil` independent-override gap on EasyGL/Bgfx) is consistent and precisely
  cross-referenced between the prose sections and the final summary table — no drift.

## Detailed Findings
None — internally consistent, and its central finding (Task 870) is properly marked fixed via a clear
status banner rather than left stale.

## Cross-File Observations
The EasyGL stencil-buffer-allocation fix (Task 315: no window ever requested `SDL_GL_STENCIL_SIZE`,
so the stencil test "trivially always passes... exactly mimicking a bypassed test") is the identical
root-cause shape as the Vulkan Task 870 finding (a feature whose *state-application code* is correct
but whose *actual hardware resource* was never provisioned) — the document itself draws this parallel
explicitly ("the same shape and severity as Task 868's BlendState finding"), a good example of
pattern-recognition across findings within one document.

## Missing or Weak Tests
The document itself flags Bgfx's physical-stencil-buffer-existence question as "not verified" —
worth checking whether this was resolved in a later phase; not independently re-verified in this
pass (would require reading Bgfx backend source directly, out of scope for a docs-only report).

## Positive Findings
The "Test-design lesson" section is one of the most transferable pieces of testing methodology found
anywhere in this docs corpus — explicitly generalizing a specific mistake (an early 3-column test that
coincidentally passed on Vulkan too, since a bypassed test also "always passes") into a reusable
principle for any future stencil test in the codebase.

## Final Assessment
No findings. A well-maintained, self-correcting, methodologically valuable document.
