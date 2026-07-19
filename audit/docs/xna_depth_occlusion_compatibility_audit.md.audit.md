# Audit: docs/xna_depth_occlusion_compatibility_audit.md

## Metadata
- Source file: `docs/xna_depth_occlusion_compatibility_audit.md` (266 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (a dated incident investigation, RESOLVED, 2026-07-11)
- Cross-references: `docs/xna_culling_compatibility_audit.md` (the related-but-independent prior
  investigation on the same sample, audited alongside this one)

## Purpose
Documents the root cause and fix for a depth-occlusion defect discovered immediately after the
culling-audit's own asset fix was applied: `GraphicsDevice`'s constructor never pushed its own
default `BlendState`/`DepthStencilState` to the real backend (only `RasterizerState` was synced, a
prior Task 896 fix that was correct but incomplete in scope) — leaving EasyGL's raw OpenGL
depth-test state disabled by default for any game (like the real XNA `Tank.cs` this sample ports)
that never explicitly sets `DepthStencilState` itself.

## Executive Verdict
An exceptionally rigorous root-cause narrative, distinguished by a genuinely elegant diagnostic
isolation methodology (§5): a 3-line diagnostic override (all 3 state setters) is progressively
narrowed to a 1-line override (`DepthStencilState` alone), confirmed **pixel-identical** at every
step via `compare -metric AE` returning exactly `0`, before the real constructor-level fix is applied
and shown to reproduce that same pixel-identical result with zero sample-side code. This is about as
strong a "we know exactly which single line matters" proof as a documentation narrative can offer.

## Checklist Results
- §3's per-backend table cross-referencing each backend's own hardcoded state defaults against real
  XNA's constructor defaults is precise and, notably, **not uniformly bad** — it correctly identifies
  that Vulkan was "never actually affected" (its own member initializers happened to already match
  XNA) while EasyGL's depth-test default and Bgfx's blend default were each independently wrong, for
  the same root cause but manifesting differently per backend. This nuance (not every backend is
  equally affected by the same missing sync) is precisely and correctly reported, not glossed over
  as "all 3 backends were broken."
- §4's "no tank mesh or part was found entering a transparent/alpha-blended path" claim is verified,
  not assumed — by isolating the diagnostic fix to *only* `DepthStencilState` and confirming a
  pixel-identical result to fixing all 3 states, ruling out any contribution from `BlendState`
  specifically.
- §7's new regression test (`graphicsdevice_default_state_occlusion_test.cpp`) is explicitly and
  correctly described as "deliberately unlike every other depth/blend test in this project" — because
  it never calls any of the 3 state setters at all, exercising only the untouched constructor
  defaults, which is exactly why the bug evaded the existing test suite's structural blind spot
  (every other test explicitly sets state before drawing, a reasonable isolation habit that happens
  to also make the suite structurally unable to catch a constructor-default bug). This meta-level
  self-diagnosis of *why* the existing suite missed the bug is a genuinely valuable piece of
  test-design reasoning.
- §7's discriminating-power verification (via `git stash` of the actual 1-line-added-to-3-lines fix,
  confirming Check A fails exactly as predicted when reverted) is a real, falsifiable proof the new
  test would have caught the original bug.
- §8's incidentally-found `SpriteBatch`-blend-state-leak bug is correctly separated from this
  document's own root cause, explicitly marked "deliberately not fixed here," with its own eventual
  resolution (Task 956, "fixed 2026-07-11") tracked and cross-referenced — the document was updated
  to close this loop rather than leaving a stale "not fixed" claim.

## Detailed Findings
None against this document — its own findings (the `GraphicsDevice` constructor state-sync gap;
Vulkan unaffected; Bgfx's own independent blend-default bug; the `SpriteBatch` blend-leak found
incidentally and separately tracked/fixed) are all precisely evidenced and internally consistent.

## Cross-File Observations
Explicitly and correctly distinguished from `docs/xna_culling_compatibility_audit.md` (both
documents state, in their own words, that the two investigations are related-but-independent — one
an asset-data winding bug, the other a `GraphicsDevice`-construction framework bug) — a clean,
mutually-reinforcing pair of incident write-ups rather than a confusing overlap. The Bgfx
blend-default bug this document identifies (`BGFX_STATE_BLEND_ALPHA` as the hardcoded default) is a
genuinely separate finding from the `EasyGL_MRT_TwoAttachments`/`ReferenceStencil` pre-existing
failures cited elsewhere in this documentation shard (`docs/rasterizerstate-support.md`,
`docs/xna-4-api-coverage.md`'s per-backend table) — no double-counting.

## Missing or Weak Tests
None identified — the new `graphicsdevice_default_state_occlusion_test.cpp` is explicitly designed
to close the exact structural blind spot (every other test sets state explicitly) that let this bug
ship undetected, and its discriminating power is independently verified via a real revert-and-observe
step.

## Positive Findings
The progressive diagnostic-narrowing methodology (§5, 3 lines → 1 line → constructor fix, each step
confirmed pixel-identical via `compare -metric AE`) is one of the cleanest, most rigorous root-cause
isolation narratives in this entire audit. The explicit self-diagnosis of why the existing test suite
structurally couldn't have caught this class of bug (§7) is exactly the kind of test-design
retrospective this audit's own methodology values.

## Final Assessment
No findings. An exemplary root-cause investigation with a genuinely elegant, quantitatively-verified
diagnostic isolation methodology, correctly and explicitly distinguished from its related sibling
investigation (`docs/xna_culling_compatibility_audit.md`).
