# Audit: docs/vertex-format-support.md

## Metadata
- Source file: `docs/vertex-format-support.md` (136 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (source-inspected against Tasks 248-250, Phase 30)
- Cross-references: `xna-graphics` shard audit (no contradicting finding); this document's own
  "stride-keyed, not declaration-keyed" architectural finding is a distinct, disjoint gap from that
  shard's 6 HIGH findings

## Purpose
Documents that all 4 graphics backends select their GPU vertex-attribute layout from the bound
`VertexBuffer`'s **byte stride** rather than from the `VertexDeclaration`'s actual elements — meaning
only 5 hardcoded strides render correctly, and any custom vertex layout falls back to a
degraded/wrong rendering path depending on backend.

## Executive Verdict
A precise, structurally important architectural disclosure. The "How vertex layout selection works"
section states the core limitation in one clear sentence and then substantiates it with per-backend,
per-stride fallback behavior tables that are specific enough to predict exactly what a custom vertex
layout would render as on each backend (not just "it might not work").

## Checklist Results
- The EasyGL other-stride fallback ("position-only, float3 at offset 0... color/UV attributes are
  missing") and the Bgfx stride-20/24/32 caveat ("position attribute is correct but UV/normal
  attributes are mapped as padding bytes, so the draw appears with wrong colors or no shading") are
  both precise, falsifiable, mechanism-level descriptions — not vague "might render incorrectly"
  hand-waving.
- The Vulkan other-stride behavior ("no pipeline is compiled for unknown strides; the draw call is
  silently skipped") is a meaningfully different failure mode from EasyGL's degraded-but-visible
  fallback — the document correctly distinguishes "renders wrong" from "renders nothing at all,"
  which matters for a developer debugging either symptom.
- The `VertexElementFormat`/`VertexElementUsage` backend-mapping tables are explicitly and
  consistently caveated as "audit reference only until per-declaration pipeline compilation is
  implemented" for Vulkan, and "not yet wired into `MakeBgfxLayout`" for Bgfx — the document doesn't
  let a reader believe the correct-looking mapping tables mean the mapping is actually applied.
- Cross-checked that `Tangent`/`Binormal` are correctly marked ❌ (no shader slot) for EasyGL/Vulkan
  but have a real Bgfx attrib enum — consistent internal detail, not a contradiction between the two
  tables in the document.

## Detailed Findings
None against this document — it precisely and consistently describes a real, structural, already-
disclosed architectural limitation (stride-keyed rather than declaration-keyed vertex layout
selection).

## Cross-File Observations
This "selects by stride, not by declared format" architectural gap is a genuinely distinct finding
from anything in the `xna-graphics` shard's own 6 HIGH findings (`SpriteFont`/`SpriteBatch`/
`EffectParameter`/`GraphicsException`) — no overlap, no double-counting. Worth flagging as a
candidate for this audit's own cross-cutting findings document (`AUDIT_CROSS_CUTTING_FINDINGS.md`)
given its potential to silently mis-render any custom (non-standard-stride) vertex layout across
every backend except SDL_Renderer (which has no 3D pipeline at all).

## Missing or Weak Tests
The document doesn't itself claim test coverage for the "custom stride" fallback paths — a reader
can't tell from this document alone whether the degraded-fallback behaviors (EasyGL position-only,
Vulkan silent-skip, Bgfx wrong-colors) are pixel-tested or only inferred from code reading. Not
flagged as a doc defect (the document is honest about what it covers), but worth noting as an area
where this audit's own future test-shard cross-checks (if any test exercises a non-standard stride)
could add confirming or contradicting evidence.

## Positive Findings
The clean separation between "the per-format/per-usage mapping helper functions are individually
correct" (confirmed for Vulkan/Bgfx) and "the mapping is actually wired into the active code path"
(not yet, for both) is a valuable, precise distinction — it would be easy for a less careful
document to conflate "the correct mapping table exists somewhere in the code" with "the mapping is
applied," and this document does not make that mistake.

## Final Assessment
No findings against this document. It precisely documents a real, structurally significant,
cross-backend architectural limitation (stride-keyed vertex layout selection) that is disjoint from
this audit's own `xna-graphics` shard findings and worth surfacing in the cross-cutting findings
document.
