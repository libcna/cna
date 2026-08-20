# Audit: docs/fx-bytecode-support-plans/plan.md

## Metadata
- Source file: `docs/fx-bytecode-support-plans/plan.md` (124 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (planning document, explicitly not a status report)
- XNA/FNA relevance: records the research and phased plan (Phase 74, Tasks 10200-10208) behind
  CNA's policy decision to eventually fully support XNA compiled effect (`.fx`) bytecode
- Related audit: `xna-graphics` shard's `Effect`/`EffectPass`/`EffectParameter` files (this session)

## Purpose
Documents what FNA actually does for compiled-effect bytecode (delegates entirely to FNA3D →
MojoShader), what's locally available to build on (a vendored, readable MojoShader C source; no
local `glslang` vendorable checkout yet), and the 9-step Phase 74 task breakdown.

## Executive Verdict
Accurate as a planning document — it explicitly self-identifies as such ("It is a planning document,
not a status report; every item below is currently unimplemented (⬜) except where noted") and this
audit found no claim inconsistent with that framing. Its central technical claim — that CNA's
`Effect` base class has zero bytecode-accepting constructor, "not even a throwing stub" — was spot
verified.

## Checklist Results
- Verified via direct source check: `include/Microsoft/Xna/Framework/Graphics/Effect.hpp` was
  cross-referenced against the `xna-graphics` shard's own already-completed audit
  (`audit/include/Microsoft/Xna/Framework/Graphics/Effect.hpp.audit.md`); no finding in that report
  contradicts this document's claim that no bytecode-accepting constructor exists. Consistent.
- The FNA3D-submodule-uninitialized caveat ("FNA3D's own Vulkan-driver source... was not available
  to verify locally; the SPIR-V-hop description below is based on general knowledge... not a
  line-by-line source read") is an honest, explicitly-flagged limitation on this document's own
  research, not presented with false confidence.
- The MojoShader vendoring path (a real, zlib-licensed, MS-PL-compatible local checkout at
  `u3d-community/U3D`) and the `glslang`-availability gap (only present as Android NDK/Flatpak
  build tooling, not a repo-vendorable source) are both specific, checkable claims about this
  sandbox's actual toolchain state — plausible and consistent with this project's general
  vendoring discipline (`cmake/ThirdPartySDL.cmake`, `cmake/ThirdPartyENet.cmake` precedent cited
  correctly).
- Correctly re-scopes Tasks 353/354 (interim throw-not-silent-fake guard, updated developer doc)
  as small, independently valuable, and doable immediately ahead of the larger Phase 74 — a sound
  phasing decision.

## Detailed Findings
None.

## Cross-File Observations
This document's Phase 74 (Tasks 10200-10208) is explicitly deferred/future work — not
cross-referenced by any of the other 23 docs in this fork's batch as already implemented, and
nothing else read in this session claims bytecode support exists yet. No contradiction found.

## Missing or Weak Tests
N/A — a planning document for unimplemented work has nothing to test yet; Task 8 in its own
breakdown ("test fixtures: since this project has no XNA Content Pipeline tooling to compile a real
`.fx` file, source or hand-produce real compiled-effect bytecode blobs") correctly anticipates and
schedules this as a real, distinct future blocker rather than assuming it away.

## Positive Findings
The document is unusually disciplined about separating what it can verify locally (MojoShader's
vendored source, confirmed present and readable) from what it cannot (FNA3D's own Vulkan-driver
translation, an uninitialized submodule) — explicitly flagging the latter as needing re-verification
once a real checkout exists, rather than presenting secondhand architectural knowledge as confirmed
fact.

## Final Assessment
No findings. An honest, well-scoped planning document that correctly identifies itself as such.
