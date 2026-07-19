# Audit: docs/webgpu-backend.md

## Metadata
- Source file: `docs/webgpu-backend.md` (511 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (living capability-boundary doc for the experimental WebGPU
  backend, per `CLAUDE.md`'s own explicit reference: "See `docs/webgpu-backend.md` for the current
  capability boundary")
- Cross-references: `CLAUDE.md`'s "WebGPU Is Active (Experimental)" section

## Purpose
Tracks the WebGPU backend's implementation status: build/setup instructions, verified smoke-test
history, and a running log of each major feature landing (SpriteBatch, GPU readback, first 3D draw,
PbrEffect, SkinnedEffect/SkinnedPbrEffect, RenderTarget2D, EnvironmentMapEffect + instancing,
RenderTargetCube, mip generation), each with its own "Important limitations" honesty section.

## Executive Verdict
Correct and disciplined about scope — never claims parity with Vulkan/EasyGL/Bgfx, and the
"Important limitations" section is kept current with each new feature landing (crossing items off
as they're implemented, e.g. `RenderTargetCube`/instancing/MSAA all correctly moved from "open" to
"now implemented, see above" as the doc's own history shows them landing). One notable
cross-document observation: `CLAUDE.md`'s own summary of this backend ("the current baseline
implements native surface/device setup, clear/present, Texture2D, buffer uploads and WGSL
SpriteBatch") is now substantially behind what this doc itself documents (PbrEffect, SkinnedEffect,
SkinnedPbrEffect, EnvironmentMapEffect, RenderTarget2D, RenderTargetCube, real instancing, MSAA,
GPU readback, and a first 3D draw path are all documented here as implemented and tested) — this
doc is NOT the source of the staleness; `CLAUDE.md` is describing an earlier snapshot of this same
backend's progress.

## Checklist Results
- Every major feature section ends with an explicit, itemized "not yet implemented"/"deliberately,
  honestly NOT implemented" callout (e.g. RenderTargetCube: "mip regeneration... and MSAA...
  deliberately, honestly NOT implemented (documented scope cuts, not silently under-delivered)") —
  a consistent, disciplined pattern throughout the entire document, not just in the summary section.
- The `QueueSprite()` backbuffer-relative-clip-space finding (found while testing
  `RenderTargetCube`, documented as "a genuinely new, previously-untested finding... documented, not
  fixed") is disclosed with its exact empirical reproduction (a 32×32 cube face bound under a 64×64
  backbuffer only had one quadrant painted) — a real, honestly-flagged defect kept visible rather
  than glossed over.
- The `WebGPU_Msaa` investigation (RenderTarget2D section) is a genuine "found it wasn't actually a
  bug" story, correctly and precisely attributing the original failure to a **test-authoring**
  defect (`examples/webgpu_msaa_test.cpp` relying on `BasicEffect`'s default
  `CullCounterClockwiseFace` without the `RasterizerState::CullNone` override every other WebGPU 3D
  test sets) rather than a backend defect — an honest correction rather than either silently hiding
  the original "failure" or wrongly crediting a backend fix that didn't happen.
- The "Important limitations" master list (end of file) is consistent with each section's own
  narrower claims — cross-checked no section claims something the master list still lists as open
  (e.g. `Texture2D.GetData()` is correctly still listed as open, and no earlier section claims it
  works).

## Detailed Findings
None against this document itself. See Cross-File Observations for the `CLAUDE.md` staleness
observation, which is a `CLAUDE.md` issue, not a defect in this doc.

## Cross-File Observations
`CLAUDE.md`'s own "WebGPU Is Active (Experimental)" section states: "The current baseline
implements native surface/device setup, clear/present, Texture2D, buffer uploads and WGSL
SpriteBatch. Do not describe it as Vulkan-level or full XNA 3D parity until the remaining shader,
state, effect, render-target, readback and test tasks are actually complete." This document's own
much more extensive, dated feature history (RenderTarget2D/RenderTargetCube, PbrEffect,
SkinnedEffect/SkinnedPbrEffect, EnvironmentMapEffect, real instancing, MSAA, GPU readback all
documented as implemented, several as recently as 2026-07-18) shows real progress substantially
beyond `CLAUDE.md`'s own summary — `CLAUDE.md` itself has become the stale artifact here, not this
document. This doc still correctly avoids claiming full parity (custom effects, full BlendState,
compressed textures, MRT, and browser/Emscripten remain honestly listed as open), so the doc is not
in violation of `CLAUDE.md`'s directive — but `CLAUDE.md`'s own capability-boundary summary should be
refreshed to reflect how much has landed since it was last written, since `CLAUDE.md` is itself
consulted as project-wide governing instruction (out of scope for this audit to edit directly, but
worth flagging).

## Missing or Weak Tests
N/A for a documentation file — the doc's own extensive per-feature CTest citations (`WebGPU_Msaa`
6/6, `WebGPU_Skinned3D` 9 checks, `WebGPU_RenderTargetCube` 12/12, etc.) suggest strong empirical
backing for each claim, consistent with what this audit observed in the `build-cmake-tests` shard's
own (separately, concurrently audited) `WebGpuTests.cmake` review.

## Positive Findings
The consistent "deliberately, honestly NOT implemented" framing throughout, plus the
`QueueSprite()`/`WebGPU_Msaa` self-correcting investigation narratives, make this one of the more
trustworthy living-status documents reviewed in this audit — it reads as a genuine, dated
engineering log rather than a marketing-style capability claim.

## Final Assessment
No findings against this document. One cross-file observation: `CLAUDE.md`'s own WebGPU capability
summary is now stale relative to this doc's much more extensive, accurately-dated feature history —
`CLAUDE.md` should be refreshed, not this file.
