# Audit: docs/web-emscripten-graphics-limitations.md

## Metadata
- Source file: `docs/web-emscripten-graphics-limitations.md` (153 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (Task 459 status write-up, with a 2026-07-15 CANVAS-backend
  update note)
- Cross-references: `docs/viewport-displaymode-adapter-support.md` (its §5 Android/Web section,
  cross-referenced by this doc for the canvas-as-display model rather than duplicated)

## Purpose
Documents the EasyGL-over-WebGL2 Emscripten graphics path's status: real, substantial CMake
scaffolding exists, but **zero actual execution has ever occurred** — no `emcc` build of this
specific path, no browser run, and the entire graphics pixel-test suite is explicitly excluded on
Emscripten.

## Executive Verdict
An unusually disciplined "design-time only, not verified" document — its own headline section title
is literally "Status headline: real build scaffolding, zero verified execution," and every single
technical claim about WebGL2/GLES3 capability gaps is explicitly labeled "design-time expectation,"
not empirical fact. The dated 2026-07-15 update note is a model example of scoped self-correction:
it identifies exactly which of its own prior claims ("no one has ever actually run `emcc` here") no
longer holds project-wide (a *different* backend, `CANVAS`, has now been built and run under
Emscripten) while explicitly stating this does NOT extend to the EasyGL path this document is about.

## Checklist Results
- The claim "the graphics-specific integration/pixel-readback test suite... is **explicitly
  excluded** on Emscripten (`CMakeLists.txt`: `if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
  ...)`, 4 call sites)" is a precise, falsifiable, source-cited claim — not a vague "probably
  doesn't run there."
- The `CnsTests`-links-but-can't-meaningfully-run-graphics-tests analysis is a careful, multi-step
  argument: Emscripten Asyncify tuning was "confirmed empirically," but that confirmation was in the
  context of the *networking* test suite, not graphics — and Node.js (the usual local runner) has no
  WebGL implementation at all, so even a successful link doesn't imply a meaningful graphics-test run
  is possible. This is precise reasoning, not an assumption transferred from an unrelated context.
- The WebGL context-loss handling section (`CNA_DebugLoseWebGLContext`/`InstallEmscriptenContextLossCallbacks`)
  correctly distinguishes "real, thought-through code addressing a genuine WebGL-specific concern —
  not a stub" from "never exercised against a real browser's WebGL implementation" — a precise,
  non-conflated pair of claims about the same code.
- The WebGL-version-pin inconsistency finding (`cna_house3d_demo` pins `MIN/MAX_WEBGL_VERSION=2`;
  `cna_demo_2d`/`cna_demo_sound` do not) is a real, specific, actionable pre-emptive-bug flag —
  correctly framed as "worth resolving before any real in-browser testing begins," not asserted as a
  confirmed bug (since it has never been tested either way).

## Detailed Findings
None against this document — every claim is precisely scoped, falsifiable, and consistently labeled
by its actual verification status (real/present vs. design-time/anticipated).

## Cross-File Observations
Correctly cross-references, rather than duplicates, `docs/viewport-displaymode-adapter-support.md`'s
own Android/Web §5 for the canvas-as-display-model concern — a clean division of concerns between the
two documents (rendering-backend-specific vs. device/adapter-model-specific), with no contradiction
between them on the shared "nothing has actually been verified on Web/Emscripten for this path"
theme.

## Missing or Weak Tests
The document's own account is itself the primary finding: the entire graphics pixel-test suite is
excluded on Emscripten, a real, large, and honestly-disclosed coverage gap — not remediated by this
audit's own scope (no source changes made), but clearly flagged as the single most important thing
for "whoever eventually does the first real Emscripten build" to address first.

## Positive Findings
The dated, narrowly-scoped 2026-07-15 self-correction note is an excellent example of exactly the
kind of "old bookmarks find stale claims, so update in place with a dated note rather than silently
rewriting" discipline this audit has observed as a recurring, deliberate project convention across
multiple documents in this shard (`docs/model-content-pipeline-support.md`,
`docs/sampler-state-support.md`, `docs/location-future-plans/plan.md`). The explicit "Recommendation for
whoever eventually does the first real Emscripten build" closing section is genuinely actionable
guidance, not just a status dump.

## Final Assessment
No findings. An exemplary "design-time only, explicitly not verified" document with a well-executed,
narrowly-scoped self-correction note.
