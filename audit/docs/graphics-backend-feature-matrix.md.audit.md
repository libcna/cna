# Audit: docs/graphics-backend-feature-matrix.md

## Metadata
- Source file: `docs/graphics-backend-feature-matrix.md` (375 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (master cross-backend feature matrix)
- XNA/FNA relevance: master status document for SDL_Renderer/EasyGL/Vulkan/Bgfx/D3D9/D3D11/D3D12
  feature/parity coverage
- Related audit: `xna-graphics` shard (191 files) and all 16 `backend-*` shards (this session)

## Purpose
The authoritative, explicitly-current cross-backend feature matrix (2D SpriteBatch/SpriteFont,
RenderTarget/MSAA/mip/depth, Texture2D/3D/Cube, GraphicsDevice state objects, OcclusionQuery,
Model), superseding the dated `docs/coverage.md` for Graphics-namespace content.

## Executive Verdict
Exceptionally well-maintained and internally self-correcting — the single most rigorous status
document read in this docs-shard batch. It explicitly distinguishes ✅ (real GPU-facing pixel/
behavior proof) from 🟨 (implemented, not independently verified) from ⬜ (not attempted) from ❌
(tested and found not to work), and repeatedly documents its own prior overclaims being caught and
corrected in place (e.g. the `OcclusionQuery` D3D11/D3D12 rows explicitly note a self-contradiction
within the same file being caught: "this row's 'no query support exists' contradicted the very next
table up... already correctly described `D3D12OcclusionQueryBackend` as real"). No claim in this
document was found to contradict this session's own `xna-graphics`/`backend-*` shard findings.

## Checklist Results
- Cross-checked against this session's confirmed HIGH findings in `xna-graphics` (`EffectParameter`
  Matrix Get/Set/Transpose inversion; `SpriteFont`/`SpriteBatch` default-character UB and
  `SpriteEffects` table sizing; `GraphicsExceptionTests.cpp` wrong exception types): none of these
  are the kind of claim this matrix's own scope covers (it tracks per-backend GPU-feature
  implementation completeness, not individual shared-code correctness bugs), so their absence here
  is not a gap — genuinely out of this document's stated scope, not an oversight.
- The Vulkan `BlendState` (Task 868, fixed 2026-07-09) and `ReferenceStencil` (fixed as an
  "undocumented side effect of Task 870") entries are consistent with this session's own confirmed
  understanding of the Vulkan backend's maturity trajectory.
- The `D3D9` column's stricter "byte-identical to real XNA 4.0 via oracle" bar, and its explicit
  distinction from "verified through Wine+DXVK on this dev machine," is a clearly-stated,
  non-overclaiming methodology note.
- Its own "Note: several per-effect `docs/*-support.md` files... predate Tasks 885-900's fixes...
  this matrix reflects current, post-fix state" is an accurate, useful cross-reference — consistent
  with this batch's own findings that `docs/dualtextureeffect-support.md`/
  `docs/environmentmapeffect-support.md` are in fact both already current (not stale examples of
  the pattern this note warns about).
- The "Known pre-existing test-failure baseline, per backend" section's specific counts (EasyGL 3,
  Bgfx 2, Vulkan 1, SDL_Renderer 13) are presented with explicit dates and are self-consistent with
  the rest of the document's own narrative of fixes over time.

## Detailed Findings
None.

## Cross-File Observations
- This document's own text explicitly says `docs/coverage.md` "is kept for its still-accurate
  non-Graphics namespace estimates" — that document was not part of this fork's assigned batch;
  a future audit pass should verify whether `docs/coverage.md`'s remaining scope (Audio/Media/
  Content/Net/GamerServices estimates) is still accurate given how much has changed elsewhere.
- Consistent with `docs/graphics-compatibility-report.md` (audited alongside this file) on the
  Task 921/868/918/916/922 "5 confirmed bugs" list and the 447/686/687/725/732 "5 BLOCKED tasks"
  list — both documents cite the same task numbers with the same fix/open status.

## Missing or Weak Tests
N/A — this is a status document, not source under test.

## Positive Findings
The discipline of a 4-tier status legend (✅/🟨/⬜/❌) applied consistently across 7 backend columns
and dozens of feature rows, combined with visible in-document self-correction of prior overclaims,
is a genuinely exemplary practice — this is the kind of living status document that stays trustworthy
specifically because it is willing to mark its own past claims wrong when a later pass finds they
were.

## Final Assessment
No findings. The most rigorous and internally self-correcting status document encountered in this
docs-shard audit batch.
