# Audit: docs/xna-4-api-coverage.md

## Metadata
- Source file: `docs/xna-4-api-coverage.md` (1019 lines — the largest single document in this
  `docs` shard by a wide margin)
- Audit status: AUDITED (full read, both halves)
- Subsystem: `docs` shard
- File type: Markdown documentation (the master XNA 4.0 API coverage tracker, maintained and
  re-corrected across many dated updates from 2026-06-26 through 2026-07-17)
- Cross-references: virtually every other document in this `docs` shard points here or is pointed to
  from here (`docs/graphics-backend-feature-matrix.md`, `docs/migration-guide.md`,
  `docs/occlusionquery-support.md`, `docs/avatar-real-rendering-ext.md`, `docs/model-content-pipeline-support.md`,
  `docs/sdl-renderer-2d-completeness.md`); `xna-graphics`/`xna-gamerservices`/`xna-net` shard audits
  (this session, no contradicting finding for the areas cross-checked)

## Purpose
The authoritative, namespace-by-namespace, then per-class, then per-backend XNA 4.0 API coverage
tracker: what's Present/Implemented/Tested/FNA-compatible/Intentionally-unsupported (Task 482's
5-axis framework), dense per-class and per-backend Graphics tables (Tasks 483/484), a consolidated
known-deviations list (Task 485), a milestone-claim gating checklist (Task 490), and full
GamerServices/Net per-feature support matrices (added 2026-07-17 correcting a long-stale
"not planned" framing).

## Executive Verdict
An exceptionally well-maintained living document, distinguished by a rare and valuable practice:
**each major update explicitly names what its own prior version got wrong or let go stale**, rather
than silently rewriting history. Task 482's "Coverage axes" framework (Present/Implemented/Tested/
FNA-compatible/Intentionally-unsupported as 5 genuinely independent questions) is a strong piece of
methodological infrastructure this whole document, and several sibling documents in this shard,
build on. However, this document — the single most likely place a reader would look for
`Microsoft::Xna::Framework::Graphics`'s known gaps — omits the two confirmed HIGH-severity findings
from this session's `xna-graphics` shard audit (the `EffectParameter` Matrix Get/Set/Transpose
inversion; the 3 `GraphicsException` subclasses deriving from `std::runtime_error` instead of the
project's own `System::Exception` hierarchy), the same omission already flagged in
`docs/migration-guide.md.audit.md`.

## Checklist Results
- Task 482's "Coverage axes" table's own worked example — `IndexElementSize`'s numeric values being
  Present+Implemented+Tested for years while silently NOT FNA-compatible (`16`/`32` vs. FNA's real
  `0`/`1`), caught only by literally running FNA and diffing output — is a genuinely valuable,
  self-critical illustration of why "looks reasonable" review is insufficient and why an independent
  FNA-vs-CNA runtime comparison harness (`tools/fna-reference/`, cross-referenced and independently
  audited earlier this session) is necessary infrastructure, not a nice-to-have.
- §3's per-class Graphics table (Task 483) and §"Per-backend Graphics support" (Task 484) are both
  internally cross-checked in this pass against the `xna-graphics` shard's own 6 HIGH findings
  (`SpriteFont`/`SpriteBatch`/`EffectParameter`/`GraphicsException`) — the `SpriteFont` row ("✅
  across the board... `MeasureString(StringBuilder)` overload added; glyph placement/spacing/flip
  pixel-verified") does not surface either of the two confirmed HIGH `SpriteFont`/`SpriteBatch`
  findings from this session (the default-character-fallback UB, or the `SpriteEffects` axis-table
  4th-entry gap) — consistent with `docs/spritefont-support.md`'s own documented §7 disclosure of the
  latter (so it's a real, if narrower, known limitation this master table simply doesn't surface at
  this level of granularity) but genuinely silent on the former.
- §7's dated update trail (rewritten 2026-07-09 after Phases 71-73 closed; further updated the same
  day and the next as Bgfx/Vulkan phases closed one after another) is precisely, incrementally
  correct — each update names exactly which specific claim from the prior version is now stale and
  why, rather than a blanket rewrite. Cross-checked against `docs/rendertarget-support.md`/
  `docs/sampler-state-support.md`/`docs/rasterizerstate-support.md` (all independently audited
  earlier in this pass) — no contradiction found between this document's summary claims and those
  documents' own more granular findings.
- §9's GamerServices/Net matrix is consistent with the `xna-gamerservices`/`xna-net` shard audits
  (both fully completed earlier this session) — the `FriendCollection`/`FriendGamer` "documented
  stub for population" entry (`GetFriends()` always returns empty) matches this session's own
  `xna-gamerservices` shard finding almost exactly, and the `GamerPresence.presenceModeStrings_`
  misindexing MEDIUM finding from that shard is (correctly) not claimed as fixed anywhere in this
  document — no false "done" claim contradicting a real open finding.
- §10's Audio full per-member cross-reference (Task 482's own worked "gaps found and their
  disposition" list) explicitly documents 5 small doc/test-naming corrections made *during* this
  audit pass itself (e.g. `Cue::IsCreated`/`IsPreparing` permanently unreachable, previously
  undocumented; a stale test-name/comment corrected; a Doxygen range typo corrected) — a genuinely
  valuable self-referential audit-of-the-audit-doc practice.

## Detailed Findings

### MEDIUM — This master coverage document omits the two confirmed HIGH-severity `xna-graphics` findings from this session
Same underlying gap already flagged in `docs/migration-guide.md.audit.md`, restated here because
this document is the more authoritative, more likely-to-be-consulted source for exactly this kind
of claim: the `EffectParameter` Matrix Get/Set/Transpose semantics inversion (confirmed HIGH, and
confirmed **baked into the test suite** as asserted-correct via `EffectParameterTests.cpp`) and the
3 `GraphicsException` subclasses' wrong base-exception-type (confirmed HIGH, also baked into
`GraphicsExceptionTests.cpp`) are not mentioned anywhere in this 1019-line document's Graphics
sections, despite the document's own stated purpose of tracking exactly this class of FNA-compatible-
axis divergence. Given how carefully this document tracks and dates every other Graphics-area
finding (down to individual task numbers for narrow per-effect secondary-feature gaps), this is a
real, currently-live gap in the single most comprehensive coverage-tracking document in the project
— not a hypothetical future risk.

## Cross-File Observations
This document's own Task 482 "FNA-compatible axis" worked example (`IndexElementSize`) is precisely
the class of bug the two omitted findings above also represent — a member that is Present+
Implemented+Tested but not independently verified against FNA's actual runtime behavior. The
document's own framework predicts exactly why these two findings would be missed (no automated
FNA-vs-CNA comparison exists for `EffectParameter`'s Matrix semantics or `GraphicsException`'s
inheritance chain — the `tools/fna-reference/`/`scripts/compare-fna-reference.py` harness's own
4 categories are enums/state-presets/PackedVector/Viewport, not Effect parameters or exception
hierarchies) — this document's own stated "100% compatibility" checklist item ("The FNA-compatible
axis has been independently verified... via the harness or an equivalent direct comparison") would,
if actually run today, need to add these two areas to its scope.

## Missing or Weak Tests
N/A directly (a documentation file), but the document's own self-audit (§10's "Gaps found by this
audit and their disposition" for Audio) models exactly the kind of test-currency check this pass
recommends be extended to the two omitted Graphics findings above.

## Positive Findings
Task 482's 5-axis coverage framework (Present/Implemented/Tested/FNA-compatible/Intentionally-
unsupported) is one of the most valuable pieces of methodological infrastructure in this entire
documentation shard — it gives every other document in this shard (and this audit's own findings) a
shared, precise vocabulary for exactly what kind of gap something is, rather than a single blended
"X% done" number. The dated, incremental, self-correcting update trail (rewriting only what's
actually stale, naming the specific prior claim being corrected) is a model practice this audit has
observed as a genuine project-wide convention, not unique to this file.

## Final Assessment
One MEDIUM finding: this document — the single most comprehensive Graphics/GamerServices/Net/Audio
coverage tracker in the project — omits the two confirmed HIGH-severity `xna-graphics` findings from
this session (`EffectParameter` Matrix transpose inversion; `GraphicsException` wrong base type),
the same gap already independently flagged in the sibling `docs/migration-guide.md`. Otherwise this
is an exceptionally well-maintained, self-critical, and internally consistent document — no
contradictions found against this session's own `xna-graphics`/`xna-gamerservices`/`xna-net` shard
audits anywhere else.
