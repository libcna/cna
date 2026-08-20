# Audit: docs/viewport-displaymode-adapter-support.md

## Metadata
- Source file: `docs/viewport-displaymode-adapter-support.md` (150 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (closes Phase 40, `plans/plan_graphics.md` Tasks 341-350)
- Cross-references: `docs/devices-build.md` (referenced for the Android KVM-failure precedent, not
  independently re-audited by this pass)

## Purpose
Documents the `Viewport`/`DisplayMode`/`DisplayModeCollection`/`GraphicsAdapter` audit: a
property-convention fix, a real `DisplayModeCollection` indexer gap, four real `GraphicsAdapter`
bugs (including a dangling-reference severe finding), a real window-resize/viewport-reset bug, and a
carefully-scoped "anticipated, not verified" section for Android/Web platform behavior.

## Executive Verdict
Exceptionally disciplined about the boundary between "verified" and "anticipated" — §5's entire
structure is organized around this distinction (what actually exists today vs. what SDL3's own
public docs say vs. what would/wouldn't need to change), explicitly refusing to claim any Android/
Web-specific behavior has been confirmed when it hasn't. The `DefaultAdapter` dangling-reference bug
(§3) is a genuinely severe, well-diagnosed memory-safety finding.

## Checklist Results
- The `DefaultAdapter` dangling-reference bug ("a raw C++ reference bound once at static-init
  time... any later call [to `AdaptersChanged()`] left `DefaultAdapter` — and its 2 production call
  sites — dangling") is a real, serious use-after-free-shaped defect, correctly identified as "the
  most severe finding" in that section, with a precise fix (remove the field, use the already-correct
  accessor) rather than a superficial patch.
- §5's "What actually exists today" section is scrupulously precise about compile-vs-execute:
  Android cross-compiles but "no `GraphicsAdapter`/`Viewport`/`DisplayMode` code has ever
  **executed** on Android," and Web/Emscripten has real build scaffolding but "no actual `emcc`
  build of `CNA` has ever been performed" — this level of precision (compiles ≠ runs ≠ verified)
  is exactly the kind of confidence-calibration this audit's own methodology tries to model.
- §5's "FNA's own precedent: zero platform branching in this exact area" sub-section is a real,
  falsifiable claim about FNA's own source (confirmed via direct audit of `GraphicsAdapter.cs`/
  `DisplayMode.cs`/`SDL3_FNAPlatform.cs`) used to justify why CNA's own lack of Android/Web-specific
  branching in this exact area is *not* itself a gap — a well-reasoned, source-grounded argument
  rather than an assumption.
- The document explicitly states, in its own closing lines, "This section documents anticipated
  behavior only. No non-desktop `GraphicsAdapter`/`DisplayMode`/`Viewport` execution has been
  verified in this project, and no code changes are made by this task" — an unusually strong,
  explicit disclaimer against over-reading the anticipated-behavior analysis as verified fact.

## Detailed Findings
None against this document — its own findings (the 4 `GraphicsAdapter` bugs, the viewport-reset bug,
the `DisplayModeCollection` indexer gap) are real, precisely described, and its Android/Web
discussion is scrupulously hedged as unverified.

## Cross-File Observations
Cites `docs/devices-build.md` for the Android KVM-acceleration failure blocking any real device/
emulator verification — this pass did not independently re-open that document to verify the citation,
but the specific, falsifiable nature of the claim (a named failure mode, a named task ID) is
consistent with this project's general documentation-citation discipline observed throughout this
shard.

## Missing or Weak Tests
N/A for a documentation file — the doc's own account of test additions (non-identity-matrix
`Project`/`Unproject` tests, a genuine non-mocked "SDL video subsystem not initialized" regression
test) suggests real new coverage was added alongside the bug fixes described.

## Positive Findings
§5's tri-level precision (compiles vs. executes vs. independently verified) applied consistently to
both Android and Web/Emscripten, plus the explicit "FNA itself has zero platform branching here"
justification for why CNA's own lack of branching isn't itself suspicious, are both excellent
examples of well-reasoned, appropriately-hedged technical documentation. The `DefaultAdapter`
dangling-reference bug diagnosis is a genuinely valuable memory-safety finding, correctly triaged as
the section's most severe.

## Final Assessment
No findings against this document. Contains a genuinely important, well-diagnosed memory-safety fix
(`DefaultAdapter` dangling reference) and models exemplary confidence-calibration for its
Android/Web "anticipated, not verified" section.
