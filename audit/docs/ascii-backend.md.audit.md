# Audit: docs/ascii-backend.md

## Metadata
- Source file: `docs/ascii-backend.md` (112 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown feature-completeness status document
- XNA/FNA relevance: describes the ASCII graphics backend's inherited SDL_Renderer behavior

## Purpose
Documents the `ASCII` backend's completeness: what it inherits transparently from `SDL_RENDERER`,
its glyph/color quantizer, font atlas, `Present()` mechanics, and input passthrough.

## Executive Verdict
A precise, well-scoped document with two genuinely interesting engineering disclosures: (1) a real,
found-and-fixed alpha-blending bug in `Present()`'s background-fill compositing, and (2) a real,
non-obvious pixel-verification methodology finding (reading pixels immediately after a real
`Present()` swap returns stale/garbage data, requiring a dedicated test-only draw-without-swap path).
Both are exactly the kind of "real bug found, not just a checklist" disclosure this project's
documentation consistently favors.

## Checklist Results
- The claim that `ASCII` is "architecturally a thin decorator around `SDL_RENDERER`'s own
  `SdlGraphicsBackend`" and inherits its completeness table "row for row" is a strong, testable claim;
  this session's own `backend-ascii` shard audit (6 files, part of Task #2, already AUDITED) did not
  surface any contradiction to this inheritance claim.
- The dirty-cell-diffing "not implemented" row is honestly marked as a perf-only gap, not a
  correctness one — consistent framing with the rest of this document's ✅/⚠️/❌ discipline.

## Detailed Findings
None — no internal inconsistency or contradicted claim found.

## Cross-File Observations
This document's own `Present()` "reads pixels immediately after swap returns garbage" finding is a
generically useful piece of graphics-testing knowledge that isn't cross-referenced from any other
backend's own completeness doc in this shard batch — worth a future pass checking whether other
present-then-read-back tests elsewhere in the project (e.g. `docs/canvas-backend.md`'s own
`GetBackBufferData` claims) are vulnerable to the identical trap. Canvas's own document states its
`ctx.getImageData()` call is "genuinely synchronous," which if true would not share this exact risk —
not a contradiction, just an area worth double-checking given how easy this specific trap apparently
was to fall into once already.

## Missing or Weak Tests
N/A — the document describes its own test coverage (`Ascii_FontAtlas`, `Ascii_Quantizer`,
`Ascii_Present`, `Ascii_Input`, `Ascii_ThrowNo3D`) with specific pass counts; no gap identified in
what it claims.

## Positive Findings
The background-fill-compositing bug fix (forcing real `BlendState.AlphaBlend` factors independent of
the game's own `BlendState`) and its root-cause explanation (the internal `presentSpriteBatch_` never
went through `GraphicsDevice::BlendState`) is a clear, specific, verifiable bug-and-fix narrative —
exactly the standard this audit has been holding every source file to, met here in prose form.

## Final Assessment
No findings. A precise, internally consistent, well-evidenced completeness document.
