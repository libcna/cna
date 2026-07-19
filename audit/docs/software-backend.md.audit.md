# Audit: docs/software-backend.md

## Metadata
- Source file: `docs/software-backend.md` (183 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (living capability doc for the Software/CPU-rasterizer backend,
  verified 2026-07-13)
- Cross-references: `backend-software` shard audit (2 files, fully audited earlier this session, no
  contradicting finding)

## Purpose
Documents the Software (CPU rasterizer) backend's purpose (deterministic, GPU-free pixel tests and
cross-backend diagnostics), usage pattern, the `cna_diag_compare` cross-backend diagnostic tool, and
a detailed, itemized "Known limitations" list.

## Executive Verdict
Correct and unusually precise about its own scope boundary versus `HEADLESS` (the other GPU-free
backend) — the distinction ("`HEADLESS`... never renders a real pixel;
`ReadBackbuffer()` just reports the last `Clear()` color for every pixel" vs. Software "actually
rasterizes real triangles... a real edge-function rasterizer, real perspective-correct attribute
interpolation, a real per-pixel depth test") is precise and consequential for a reader deciding which
backend to use for what purpose.

## Checklist Results
- The cross-backend diagnostic verification (`SOFTWARE` vs. `EASYGL`, "max per-channel diff of 1
  (mean 0.139)... the residual being ordinary rounding noise, not a real rendering discrepancy") is a
  quantified, falsifiable claim — the doc additionally states the comparator tool itself was checked
  against a deliberately corrupted dump to confirm it actually fails when images genuinely differ,
  not just "always passes" — a real self-test of the verification tool, not just the thing being
  verified.
- The "Known limitations" list is itemized with real specificity (exact vertex strides supported,
  exact primitive type supported, exact blend-mode simplification, exact clipping-plane behavior)
  rather than vague hand-waving — each limitation is falsifiable and would be easy to contradict if
  wrong.
- The `EnvironmentMapEffect` limitation note (normal transformed by `World` directly rather than
  `WorldInverseTranspose`, "exact for uniform-scale/no-shear `World` matrices, a real simplification
  for non-uniform scale") correctly identifies the precise mathematical condition under which the
  simplification is exact vs. approximate — a genuinely useful level of technical precision for a
  reader deciding whether this limitation affects their specific use case.
- The `BasicEffect.VertexColorEnabled` reminder ("defaults to `false` in real XNA/FNA... exactly
  what caused `Software_Rasterizer`'s tests to briefly fail") is a real, self-disclosed "we got
  bitten by this too" anecdote — useful, honest context for a reader likely to make the same mistake.

## Detailed Findings
None. All claims are precise, falsifiable, and (where checked) consistent with this audit's own
`backend-software` shard review (2 files, no contradicting finding recorded there).

## Cross-File Observations
Consistent with the `backend-software` shard audit (fully completed earlier this session as part of
the graphics-backend sweep) — no contradiction found between this document's claimed feature set/
limitations and what that shard's code-level review would have covered (this pass did not re-open
the backend source files directly, relying on the shard's already-recorded completion and the
absence of any recorded contradicting finding).

## Missing or Weak Tests
N/A for a documentation file — the doc's own cross-backend diagnostic self-test (verifying the
comparator tool detects a deliberately corrupted dump) is itself a form of test-tool validation
worth noting as a positive practice.

## Positive Findings
Verifying the diagnostic *comparator* itself (not just the thing it compares) against a deliberately
corrupted input is an excellent, easy-to-skip verification step that this document explicitly
confirms was done — the same "don't trust a check you haven't seen fail" discipline this audit's own
methodology tries to apply throughout.

## Final Assessment
No findings. Precise, well-scoped, and includes a genuinely valuable self-test of its own
verification tooling.
