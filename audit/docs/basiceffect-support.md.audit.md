# Audit: docs/basiceffect-support.md

## Metadata
- Source file: `docs/basiceffect-support.md` (147 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown feature-support matrix
- XNA/FNA relevance: describes `BasicEffect` conformance across EasyGL/Vulkan/Bgfx

## Purpose
Summarizes Phase 42's audit and fix history for `BasicEffect`: property defaults, no-lighting shader
paths, one-directional-light lighting, ambient+emissive, and cross-backend consistency, across all
three graphics backends of that era.

## Executive Verdict
Exemplary self-maintenance: the document opens with an explicit **"Status update, 2026-07-11"** banner
stating that Tasks 885/886 (listed as open later in the same document's own §4 and support matrix)
are now implemented, and directs the reader to `docs/graphics-backend-feature-matrix.md`/
`docs/xna-4-api-coverage.md` as current instead. This is the single best example in this batch of a
document correctly flagging its own internal staleness rather than leaving a silent contradiction
between its banner and its body.

## Checklist Results
- The support matrix's own strikethrough entries (`~~Task 885~~ — fixed`, `~~Task 886~~ — fixed`)
  match the top-of-file status banner exactly — no drift between the two self-correction mechanisms
  within the same document.
- The Bgfx `MakeBgfxLayout()` vertex-attribute-layout bug (Task 368) — a real, wide-reaching bug
  affecting `TexCoord0` interpolation for multiple strides, not just the one effect being audited — is
  disclosed with its full blast radius ("the same root cause silently broke `TexCoord0` interpolation
  for strides 20/24 too — invisible in every earlier task's tests because they all use 1×1 solid-color
  textures"), a genuinely valuable piece of root-cause documentation.

## Detailed Findings
None — the document is internally consistent (banner matches body) and no claim contradicts this
session's own `xna-graphics` shard audit of `BasicEffect.cpp` and the Bgfx backend shard.

## Cross-File Observations
Same Task 885/886/899-era fix history as `docs/alphatesteffect-support.md` (sibling stock-effect
document, same phase family) — consistent cross-referencing of the same fixes across both documents.

## Missing or Weak Tests
N/A — describes already-completed test work (Tasks 362-370); no coverage gap identified in the
document's own account.

## Positive Findings
This document's self-correcting status banner is the strongest example in this entire docs batch of
the "don't leave a stale claim uncorrected" discipline this project's documentation generally
practices well — worth using as the template for how `docs/coverage.md` (found genuinely more stale
in this same pass) ought to have been maintained.

## Final Assessment
No findings. A model example of proactive documentation self-correction.
