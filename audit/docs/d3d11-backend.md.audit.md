# Audit: docs/d3d11-backend.md

## Metadata
- Source file: `docs/d3d11-backend.md` (177 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown backend status document
- XNA/FNA relevance: describes the native Direct3D 11 graphics backend

## Purpose
Documents the D3D11 backend's status, Wine+DXVK dev-loop, test-writing conventions, and known
limitations as of 2026-07-14.

## Executive Verdict
A precise, appropriately-scoped status document. Its central claim — "verified 2026-07-14... via
Windows cross-compilation + Wine+DXVK... real hardware verification is a separate, still-open gate"
— is consistently maintained throughout (every "what's real" claim is qualified with "via Wine+DXVK,"
never silently implying real-hardware parity). The `cna_reference_dump`/`cna_demo_2d` link-failure
fix narrative (a genuine circular-dependency root cause, plus a `Game1.cpp` SDL-direct-call fix) is a
specific, well-evidenced bug-and-fix account.

## Checklist Results
- The claim that `D3D11`/`D3D12` are the only backends needing an explicit
  `target_link_libraries(... PRIVATE CNA)` cycle-breaking declaration is consistent with this
  session's own `build-cmake` shard audit (`cmake/CnaLibrary.cmake`, already AUDITED) — no
  contradiction found.
- Consistently distinguishes "proven via Wine+DXVK" from "verified on real Windows hardware"
  throughout every claim in the "What this backend is for" and "Known limitations" sections — no
  overclaim detected.
- The known-limitations list (device-lost recovery untested, debug-layer-missing fallback
  unexercised, 5 combo `Clear*` variants untested by dedicated pixel test) reads as an honest,
  specific accounting rather than a vague catch-all.

## Detailed Findings
None found directly against this document's own claims.

## Cross-File Observations
This document's own "Known limitations" list does **not** contain the `CnaTests`/`::setenv()` MinGW
build-blocker claim that `docs/d3d9-backend.md`'s equivalent section does (see that document's own
audit report for the direct contradiction found there against `docs/cnatests-mingw-setenv-proposal.md`).
This document predates (2026-07-14) `cnatests-mingw-setenv-proposal.md`'s 2026-07-15 fix, so its
silence on the topic is simply time-ordering, not an inconsistency — worth noting only because the
sibling D3D9 document's *later*-dated "Known limitations" section still incorrectly repeats the
now-fixed blocker as current, which this document (being older) cannot be faulted for.

## Missing or Weak Tests
N/A — a backend status document; the document itself accurately catalogs what is and isn't
pixel-tested (e.g. specular highlights implemented in HLSL but not pixel-verified).

## Positive Findings
The "Wine proves the logic, not real-hardware parity" framing, explicitly borrowed from and
consistent with the identical bar `SDL_RENDERER`'s own documentation set, shows a coherent,
project-wide verification-standard vocabulary rather than each backend inventing its own claims
language.

## Final Assessment
No findings against this document directly. (See `docs/d3d9-backend.md.audit.md` for a related
cross-file contradiction involving the sibling D3D9 document, not this one.)
