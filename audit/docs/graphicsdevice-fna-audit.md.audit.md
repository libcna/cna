# Audit: docs/graphicsdevice-fna-audit.md

## Metadata
- Source file: `docs/graphicsdevice-fna-audit.md` (229 lines)
- Audit status: AUDITED (full read + direct source cross-check)
- Subsystem: `docs` shard
- File type: Markdown documentation (dated FNA-parity audit, 2026-06-26)
- XNA/FNA relevance: compares CNA's `GraphicsDevice` public API surface against FNA's
  `GraphicsDevice.cs`
- Cross-checked directly: `include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp` (current
  source, via targeted `grep`)

## Purpose
Enumerates `GraphicsDevice`'s constructors, events, properties, `Present`/`Reset`/`Clear`/`Dispose`
methods, back-buffer readback, render-target methods, vertex/index-buffer methods, draw methods,
and FNA extension methods against FNA, flagging missing XNA API and missing `NOXNA` tags.

## Executive Verdict
**Genuinely well-aged: despite being nearly a month old (2026-06-26, predating dozens of later
Graphics-namespace fixes elsewhere in this project), every specific claim in this document's own
"Summary of Gaps" section was independently re-verified against current source in this pass and
found still accurate.** This is a notable contrast to several other dated documents in this batch
(`docs/easygl_bugs.md`, `docs/graphics-resource-lifetime.md` vs. its sibling) whose specific claims
have since drifted from the current codebase. This document's narrower scope (one header's public
API surface, not a behavioral/behavior-correctness claim) appears to be exactly why it has aged so
well — the header hasn't been touched in the ways that would invalidate these specific findings.

## Checklist Results (direct source re-verification)
- **"Present(Rectangle?, Rectangle?, IntPtr) — Missing"**: confirmed still true — only
  `void Present();` exists in current `GraphicsDevice.hpp`; the three-argument overload is still
  absent.
- **"Clear(ClearOptions, Vector4, float, int) — Missing"**: confirmed still true — current source
  has only the `Color`-based `Clear` overloads (`Clear(const Color&)`, `Clear(ClearOptions, const
  Color&, float, int)`), no `Vector4` overload.
- **"`Clear(float, float, float, float)`/`Clear(const Color&, float)` — Not in XNA API, missing
  `NOXNA`"**: confirmed still true — both overloads still exist in current source at their own
  declarations with no `NOXNA` tag anywhere nearby (the file's `NOXNA` markers are concentrated in a
  clearly-separate `// --- NOXNA helpers (not in XNA 4.0) ---` section starting much later in the
  file).
- **"`Reset(const PresentationParameters&, GraphicsAdapter*)` — pointer overload not in XNA, missing
  `NOXNA`"**: confirmed still true — the 4th `Reset` overload (pointer-adapter variant) still
  exists, untagged.
- **"`SetIndexBuffer(const IndexBuffer*)`/`GetIndexBuffer()` — aliases, missing `NOXNA`"**: confirmed
  still true — both declared with no `NOXNA` tag, outside the `NOXNA` helpers section.
- **"`GetVertexBuffer()` — not in FNA API, missing `NOXNA`"**: confirmed still true — same pattern.
- **"`Indices()`/`Indices(const IndexBuffer*)` — duplicate of property methods, missing `NOXNA`"**:
  confirmed still true, and this is the most notable case — these two methods live *inside* the
  file's own `// --- NOXNA helpers ---` section (surrounded by other members that DO carry the
  literal `NOXNA` macro), yet the two `Indices()` overloads themselves have no `NOXNA` prefix on
  their own declarations — a genuinely easy-to-miss inconsistency, since a reader skimming the
  section header could reasonably assume every member inside it is tagged.
- **"`GetRenderTargetsNoAllocEXT(RenderTargetBinding[])` — Missing"**: confirmed still true — zero
  matches for this name anywhere in the current header.

## Detailed Findings

### LOW — This project's own CLAUDE.md `NOXNA` tagging rule is still violated at 6 confirmed call sites in `GraphicsDevice.hpp`, exactly as this document described a month ago
CLAUDE.md states: "If implementing functionality that is NOT part of the XNA 4.0 API within the
`Microsoft::Xna` namespace, you MUST wrap it with the `NOXNA` macro." This document's own
"Incorrect visibility / missing NOXNA tags" table names 7 specific members; this audit's direct
`grep`-based re-verification confirms all 7 are still un-tagged in current source (see Checklist
Results). Not independently discovered by this pass — this is a re-confirmation of an
already-documented, still-open, low-severity compliance gap, worth surfacing because it demonstrates
the document itself remains reliable enough to act on directly, a month after it was written.

## Cross-File Observations
None of this document's claims were found to conflict with any other document read in this batch or
with this session's own `xna-graphics` shard audit of `GraphicsDevice.cpp`/`.hpp` (that audit's own
findings were about `EffectParameter`/`SpriteFont`/`SpriteBatch`/exception types — a different file
set — not this specific header's `NOXNA`-tagging gaps).

## Missing or Weak Tests
Not separately assessed — the missing-`NOXNA`-tag findings are a compile-time/convention concern,
not something a runtime test would catch; there is no compile-time guard analogous to the Input
subsystem's `PublicApiInputCompileTests.cpp`/`PublicApiInputSignatureFreezeTests.cpp` enforcing
`NOXNA` tagging for `GraphicsDevice` specifically (worth a future task, following the Input
subsystem's own precedent, audited in this same batch).

## Positive Findings
This document is the single best-aged dated audit read in this entire docs-shard batch — every one
of its 7 specific "missing NOXNA tag" claims and its 3 "missing API" claims independently
re-verified true against current source nearly a month after the document was written, with zero
drift. This is a strong positive signal about both the document's original rigor and the stability
of this specific header since.

## Final Assessment
No new findings beyond re-confirming this document's own already-documented gaps are still open
and still accurate. Recommend treating this document as directly actionable (not needing a refresh
pass) — the rare case in this batch where a month-old audit doc did not go stale.
