# Audit: docs/xna_culling_compatibility_audit.md

## Metadata
- Source file: `docs/xna_culling_compatibility_audit.md` (440 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (a dated incident investigation, RESOLVED, 2026-07-11)
- Cross-references: `docs/xna_depth_occlusion_compatibility_audit.md` (an explicitly-related but
  independently-root-caused follow-up investigation on the same sample, audited alongside this one);
  `docs/rasterizerstate-support.md` (Phase 38's own `CullMode` pixel-verification work, audited
  earlier in this pass, cited as the pre-existing test precedent this investigation extends)

## Purpose
The authoritative record of an investigation into a visual defect (a visible disc-shaped surface
under a tank turret) first suspected as a CNA culling-framework bug, and its resolution: CNA's
`CullMode` framework is correct (independently confirmed against real XNA 4.0 on real hardware); the
actual root cause was a systematic triangle-winding reversal across all 12 mesh parts of a
third-party asset (`cna-samples`), introduced by an FBX-to-model conversion tool.

## Executive Verdict
One of the most rigorous single pieces of engineering investigation documentation in this entire
audit. Its central, explicitly-called-out lesson — that the investigation's own *first* conclusion
(§5.4: "the defect is isolated to `turret_geo`'s underside sub-region") was **wrong in scope, though
correct in kind**, and that the error was only caught because the project owner pushed back on a
still-wrong-looking screenshot — is a genuinely valuable, honestly-recorded methodological failure
and correction, not retrofitted into looking clean.

## Checklist Results
- §5.1/§5.2 draw a precise, correct methodological distinction between two superficially similar
  checks: "does this triangle's winding agree with its own stored normal" (a check that a uniformly
  reversed mesh would ALSO pass, since normals may have been derived from the same reversed winding
  during export) versus "does this mesh's edge-adjacency orientation invariant hold" (a real,
  non-circular manifold-consistency check, independent of stored normals). The document explicitly
  states it followed its own task instructions not to rely on the former as evidence — a real,
  disciplined constraint self-imposed and honored.
- §5.4's own retrospective account of why the first conclusion was too narrow (a uniformly-reversed
  mesh's exterior silhouette barely changes, since only concave/interior-facing detail exposes a
  global reversal — exactly why the turret's dome looked fine while its one exposed interior surface
  did not) is a sharp, correct piece of geometric reasoning about *why* the investigation initially
  missed the true scope, not just a description of what was eventually found.
- §6's "decisive test" is the single strongest piece of methodology in the whole document: rather
  than resolving "is CNA's CullMode convention correct?" by re-reading FNA's own source a second time
  (already done, and explicitly noted as "an independent reimplementation... involves an
  easy-to-invert detail," i.e., not fully trustworthy as the final word), the investigation built and
  ran a minimal reproducer on **real XNA 4.0, on real Windows 7 hardware** — settling the question
  with independent, non-FNA-derived evidence. This is exactly the kind of "verify against the real
  thing, don't just re-read source" discipline this audit's own methodology tries to model.
- §8's incidentally-found Bgfx `startIndex`/`baseVertex` bug is correctly and explicitly separated
  from the investigation's own root cause ("a real, separate bug... NOT the root cause above"), with
  its own attempted-but-reverted fix honestly disclosed (the fix "produced a worse regression on
  retest," and was reverted rather than shipped half-working) — a genuine "we tried, it didn't work,
  here's the current state" disclosure rather than silently dropping the attempt.
- §10's file-change inventory explicitly lists diagnostic-only changes that were made and reverted
  (4 separate temporary edits to `Tank.hpp`/`SimpleAnimationGame.hpp`), each confirmed via `git diff`
  showing zero net change — a real, verifiable claim about clean investigation hygiene, not just an
  assertion.

## Detailed Findings
None against this document. Its own findings (CNA's CullMode framework is correct; the asset's
winding was reversed; a separate Bgfx indexed-draw bug exists) are all precisely evidenced,
cross-verified, and consistent with the sibling `docs/rasterizerstate-support.md`'s own independent
Phase 38 `CullMode` pixel-verification (which found `RasterizerState` conformance "already solid" —
consistent with this document's own conclusion that the framework itself was never the problem).

## Cross-File Observations
Directly and explicitly related to `docs/xna_depth_occlusion_compatibility_audit.md` (a separate,
later investigation on the same sample) — both documents explicitly and correctly warn readers not
to conflate the two ("this is unrelated to `docs/xna_culling_compatibility_audit.md`'s own
winding/CullMode investigation... that one was a `cna-samples` asset data bug; this one is a
`cna_graphics` framework bug"). Consistent with `docs/rasterizerstate-support.md`'s own Phase 38
findings — no contradiction between "CullMode conformance is solid" (that document) and "CullMode
conformance is confirmed correct, twice over" (this document, a later and even more rigorous
confirmation via real XNA hardware).

## Missing or Weak Tests
The two new permanent regression tests this investigation added
(`rasterizerstate_cullmode_camera_test.cpp`, `rasterizerstate_cullmode_indexed_basiceffect_test.cpp`,
registered on all 3 runnable backends) are themselves a strong positive — extending the pre-existing
identity-transform-only CullMode tests to a real `CreateLookAt` camera and the real indexed/
`BasicEffect` dispatch path `ModelMesh::Draw()` actually uses, closing a real gap in prior test
coverage (every pre-existing CullMode test used the simpler `DrawUserPrimitives` path).

## Positive Findings
This document's honest, first-person account of its own initial wrong (too-narrow) conclusion, the
precise geometric reasoning for why that conclusion looked locally correct despite being globally
wrong, and the decisive real-hardware verification step, together make this one of the strongest
single artifacts of engineering rigor in this entire audit — a genuine model for how to investigate
and document a "which layer is actually broken" question without guessing.

## Final Assessment
No findings. An exemplary incident investigation: methodologically rigorous, honestly self-critical
about its own initial narrow conclusion, and resolved with independent real-hardware verification
rather than assumption.
