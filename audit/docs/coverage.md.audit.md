# Audit: docs/coverage.md

## Metadata
- Source file: `docs/coverage.md` (196 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown one-time dated snapshot (XNA 4.0 coverage report)
- XNA/FNA relevance: namespace-by-namespace XNA 4.0 API coverage estimate

## Purpose
A one-time (2026-06-21), statically-derived coverage estimate across every XNA namespace
(Framework/Graphics/Input/Audio/Media/Content/Storage/GamerServices/Net), with per-backend Graphics
capability tables and a task-roadmap overview.

## Executive Verdict
Correctly self-labeled by `docs/README.md` as "superseded for Graphics... kept for its non-Graphics
namespace estimates," but this session's audit finds that framing understates how stale the file now
is even outside Graphics. The document does contain several inline "stale row" self-corrections
(Net/GamerServices, XACT audio) dated 2026-07-04/07-17, which is good practice — but those
corrections are now themselves further superseded by this session's own confirmed findings.

## Checklist Results
- **Net/GamerServices rows**: already marked "stale (2026-06-21) — corrected `feature/net`" with a
  2026-07-17 update noting real implementations exist. Consistent with this session's own `xna-net`
  (42 files) and `xna-gamerservices` (89 files) shard audits, both fully AUDITED with no HIGH findings
  contradicting "real, tested implementations."
- **Audio row** ("~90%... XACT/Microphone stub status... predates that branch's work"): this
  document's own self-correction is now itself outdated relative to this session's discovery of
  `docs/cna_audio_deep_audit_2026-07-17.md` and the subsequent 80+-commit `AUD-XX` remediation series
  (Phase 11 closed 28/28, Phase 15 in progress) — the "~90% functional, minor documented deviations"
  framing here does not reflect the P0/P1-severity findings (XNB format narrowness, dynamic
  format-switching races, XWB parsing issues) that 2026-07-17 audit found and which triggered that
  large remediation effort. Not necessarily wrong in aggregate percentage terms, but materially
  incomplete next to what's now known.
- **Content row** ("no .xnb binary support"): contradicted by this session's own `xna-content`/
  `tests-xna-content`/`cna-internal-core` (Xnb) shard audits, which directly examined a substantial,
  real `.xnb` binary content-type-reader pipeline (`SoundEffectContentTypeReader.cpp`,
  `Texture2DContentTypeReader`, `ModelContentTypeReader`, etc. — all audited, all real
  implementations, not absent). This is the single most clearly stale claim in the document: `.xnb`
  binary support is not "entirely absent" as of this session's own direct source review.

## Detailed Findings

### MEDIUM — "No .xnb binary support" claim is now false; a real, substantial .xnb reader pipeline exists
The document states plainly: "the XNA binary `.xnb` format is entirely absent" and "**A game
depending on `.xnb` content loading will break immediately**." This session's own direct audits of
`src/CNA/Internal/Xnb/*ContentTypeReader.cpp` (11+ files, `cna-internal-core` shard) and
`tests/CNA/Internal/Xnb/*Tests.cpp` (14 files, `tests-cna-internal` shard, exceptional quality per
that shard's own report) confirm a real, substantial, well-tested `.xnb` binary reader pipeline
exists — covering Sound, Texture2D/3D/Cube, Model, SpriteFont, Song, Video, Curve, primitives, and
stock effects. This document's Content row and "Biggest gaps" table both need correction; whether
this reflects genuine subsequent implementation work after 2026-06-21, or a scope this document's
original analysis simply missed, was not determined in this pass (out of scope for a docs-only
audit) — but the current claim is confirmed false against current source.

### LOW — Audio row's "~90% functional, minor deviations" framing predates and undersells the 2026-07-17 deep audit's P0/P1 findings
See Checklist Results above. Not technically false (the percentage itself isn't re-litigated here),
but materially incomplete next to `docs/cna_audio_deep_audit_2026-07-17.md`'s own findings and the
`AUD-XX` remediation series it triggered.

## Cross-File Observations
- Directly contradicted by this session's own `xna-content`, `tests-xna-content`, and
  `cna-internal-core` (Xnb) shard audits regarding `.xnb` support.
- Its Audio-row self-correction chain is now one link behind `docs/cna_audio_deep_audit_2026-07-17.md`
  — see that file's own audit report for the fuller picture.
- `docs/README.md`'s own characterization of this file ("kept for its non-Graphics namespace
  estimates") should itself be revisited given the Content/Audio findings above — the non-Graphics
  estimates are not uniformly reliable either.

## Missing or Weak Tests
N/A — a coverage-estimate document, not describing testable code directly.

## Positive Findings
The document's own honest "How to read the estimates" section (distinguishing "API surface %" from
"Functional %" from the unlisted-but-acknowledged "XNA accuracy %") is a clear, useful framework, even
though some of the specific numbers/claims built on it are now outdated.

## Final Assessment
One MEDIUM finding (the ".xnb entirely absent" claim is now false, confirmed against current source
by this session's own shard audits) and one LOW finding (the Audio row is one correction-cycle behind
the 2026-07-17 deep audit). This document is a dated snapshot that has accumulated more staleness than
its own self-corrections and `docs/README.md`'s characterization currently acknowledge.
