# Audit: docs/d3d9-backend.md

## Metadata
- Source file: `docs/d3d9-backend.md` (200 lines)
- Audit status: AUDITED (full read + cross-file contradiction check)
- Subsystem: `docs` shard
- File type: Markdown backend status document
- XNA/FNA relevance: describes the D3D9 backend, whose explicit goal is pixel-for-pixel XNA 4.0
  authenticity (not just feature parity)

## Purpose
Documents the D3D9 backend's unique goal (bit-for-bit XNA 4.0 authenticity via a real-XNA-runtime
oracle comparison), what's real, what's not, the DXVK-authenticity caveat, and known limitations.

## Executive Verdict
An excellent, precisely-scoped document overall — the distinction it draws between "feature parity"
(every other backend) and "pixel-for-pixel indistinguishability" (this backend specifically) is a
genuinely useful framing, and its "0/31 scenes diverge" headline claim is directly corroborated by
the companion `docs/d3d9-divergence-report.md`. **However, this session's audit found one confirmed
MEDIUM finding: a stale bullet in its own "Known limitations" section directly contradicts
`docs/cnatests-mingw-setenv-proposal.md`, a sibling document dated the identical day.**

## Checklist Results
- The oracle-methodology description (both sides through the same DXVK D3D9-over-Vulkan path,
  `--tolerance 0`) is consistent with, and directly corroborated by, `docs/d3d9-divergence-report.md`'s
  own "How this was measured" section — no discrepancy between the two documents on this point.
- The "61 of 66 compiled shader variants are byte-identical to Microsoft's own shipped `.fxb`"
  claim is a specific, falsifiable claim consistent with the level of rigor this session found
  elsewhere in the D3D9-related backend shard audits (`backend-d3d9`, 50 files, already AUDITED).

## Detailed Findings

### MEDIUM — "Known limitations" section's `CnaTests`/D9-123 bullet directly contradicts docs/cnatests-mingw-setenv-proposal.md, same date
This document's "Known limitations (2026-07-15)" section states: *"**`CnaTests` does not build under
`CNA_GRAPHICS_BACKEND=D3D9`** (`D9-123`) — same POSIX `::setenv()` wall `D3D11` already documents."*
`docs/cnatests-mingw-setenv-proposal.md`, dated the identical day (2026-07-15), states the opposite for
the identical task ID: *"D9-123 row for the full implementation/verification result: `CnaTests` now
compiles cleanly under `CNA_GRAPHICS_BACKEND=D3D9`... AND... `ctest -L D3D9` runs clean, 4383 tests are
registered, and spot-checked individual tests genuinely execute through Wine and pass."* The
setenv-proposal document's account is far more detailed and specific (exact pass counts, a described
`CROSSCOMPILING_EMULATOR` mechanism, an explicit rationale for rejecting a naive alternative) —
strongly suggesting this document's "Known limitations" bullet is a stale holdover from a
pre-`D9-123`-fix draft that was never removed once the gap actually closed. Also directly relevant:
this document's own "What's not (yet)" section already lists `D9-11` (custom `ShaderEffect`) as
"explicitly ask-first... has not been started" — a genuinely current-sounding gap — right alongside
the apparently-stale `CnaTests` bullet, making the staleness easy to miss on a skim.

## Cross-File Observations
Same finding, reciprocally reported in `docs/cnatests-mingw-setenv-proposal.md.audit.md`. No other
inconsistency found between this document and `docs/d3d9-divergence-report.md` (which this document
correctly and consistently cross-references throughout).

## Missing or Weak Tests
N/A — a backend status document. The document's own "What's not (yet)" section is itself an honest
test/coverage-gap disclosure (render-targets-as-texture blocked on a DXVK crash, most
`SurfaceFormat`s besides `Color` unsupported by CNA's own `Texture2D` API).

## Positive Findings
The "one caveat every result above inherits" section (DXVK-synthesized `D3DCAPS9`, not a real
XNA-era driver) is a precise, honestly-scoped epistemic boundary — the document never lets its strong
"0/31 diverge" headline result imply more than it actually proves.

## Final Assessment
One MEDIUM finding: a stale "Known limitations" bullet contradicting a same-day sibling document about
whether `CnaTests` builds under D3D9. The sibling document's detailed implementation record is far
more credible; this document's bullet should be removed/updated to reflect the `D9-123` fix.
