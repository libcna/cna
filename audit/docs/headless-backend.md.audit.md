# Audit: docs/headless-backend.md

## Metadata
- Source file: `docs/headless-backend.md` (179 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (backend how-to + status)
- XNA/FNA relevance: documents the CI/testing-only Headless graphics backend
  (`CNA_GRAPHICS_BACKEND=HEADLESS`)
- Related audit: `backend-headless` shard (2 files, this session)

## Purpose
Explains what the Headless backend is for (fast, GPU-free, window-free logic testing) and isn't
(no pixel-correctness proof), its three runtime strictness modes (`Fast`/`Validation`/`Trace`), how
to write a headless test, debug labels + trace-log export/diffing, and real-world validation via a
third-party game (`mobile-eggbert`/`WindowsPhoneSpeedyBlupi`).

## Executive Verdict
Accurate, precise, and appropriately scoped — its "What this backend is for (and isn't)" framing is
exactly right and consistently honored throughout the rest of the document (it never overclaims
pixel-correctness proof anywhere else in the text). Consistent with `docs/graphics-backend-feature-matrix.md`'s
own explicit statement (audited alongside this file) that Headless is deliberately excluded from
that matrix's pixel/correctness columns for exactly the reason this document itself states.

## Checklist Results
- The three-mode dial (`Fast`/`Validation`/`Trace`) is described with specific, differentiated
  behavior per mode (bookkeeping-only vs. full argument validation vs. validation-plus-call-log) —
  not a vague "verbosity setting" description.
- The "Known limitations" section is honest and specific: "disposed state object" validation cannot
  be implemented because `IGraphicsBackend`'s `ApplyBlendState()`/etc. take raw scalar parameters,
  not object references, so there's no object identity left by the time a call reaches the backend —
  correctly characterized as "a real, permanent interface constraint, not a temporary gap," rather
  than left ambiguous about whether it's fixable.
- The "Resolved since the first commit" changelog-in-prose is a real, specific, and traceable account
  of exactly what was fixed (viewport/scissor now cross-references actual bound-target size; the
  `Headless_CoverageGaps`/`Headless_Effects`/`Headless_ModeDial`/`Headless_TraceDiff` CTests each get
  named with what they specifically close) — the same excellent "self-correcting, dated, and
  specific" pattern found throughout the best documents in this shard.
- The `Headless_Effects` CTest's own surfaced finding — `DualTextureEffect`/`EnvironmentMapEffect`/
  `SkinnedEffect` unconditionally set `TextureEnabled` regardless of whether a texture was actually
  assigned, so they genuinely throw under `HeadlessValidation` if a texture is never set, while
  `AlphaTestEffect` degrades gracefully — is presented as "worth knowing when writing a real game
  against these effects, not just a Headless test-coverage note," correctly recognizing this as a
  real, generally-relevant behavioral fact discovered incidentally by a testing-infrastructure task,
  not filed away as a Headless-only curiosity.
- The `Headless_TraceDiff` CTest's own account of catching a real bug ("`SetViewport` had never
  actually been wired into `RecordTrace()` despite an earlier commit message claiming it was") is a
  concrete, specific, and honestly-disclosed self-correction — exactly the kind of "commit message
  claimed X, later verification found X wasn't true, and this document says so plainly" pattern this
  audit values highly wherever it appears.
- The `mobile-eggbert`/`WindowsPhoneSpeedyBlupi` real-world-validation claim (a complete third-party
  game building clean and running 20+ seconds crash-free under Headless with `DISPLAY`/
  `WAYLAND_DISPLAY` unset) is a specific, falsifiable claim distinguishing "synthetic test passes"
  from "real, unrelated game code exercises this and survives" — a meaningfully stronger validation
  claim than most backend docs in this shard offer.

## Detailed Findings
None.

## Cross-File Observations
Consistent with `docs/graphics-backend-feature-matrix.md`'s own explicit exclusion of Headless from
its pixel-correctness columns (both audited in this batch) — the two documents agree on Headless's
scope and neither oversells what the backend proves.

## Missing or Weak Tests
The document's own "Known limitations" section already identifies the one real, permanent test-
coverage gap (disposed-state-object validation, blocked by the `IGraphicsBackend` interface's
scalar-parameter design) and the one still-open, not-yet-asserted-on data path (`SpriteBatch`'s
captured `LastBatch()` draw-call data) — both self-disclosed, not newly found by this audit.

## Positive Findings
The `mobile-eggbert` third-party-game validation claim and the `Headless_TraceDiff` self-correction
account are both genuinely strong evidence of real engineering rigor beyond synthetic
self-testing — this document does not just claim the backend works, it points to independent,
falsifiable proof.

## Final Assessment
No findings. An accurate, appropriately-scoped, and well-evidenced backend document.
