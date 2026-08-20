# Audit: docs/cnatests-mingw-setenv-proposal.md

## Metadata
- Source file: `docs/cnatests-mingw-setenv-proposal.md` (148 lines)
- Audit status: AUDITED (full read + cross-file contradiction check)
- Subsystem: `docs` shard
- File type: Markdown proposal document, marked fully implemented
- XNA/FNA relevance: build/test infrastructure (MinGW-w64 cross-compilation), not XNA API surface

## Purpose
Documents a proposal (now implemented) to replace 62 POSIX `::setenv()`/`::unsetenv()` call sites
across 13 test/tool files with the portable `System::Environment::SetEnvironmentVariable()` wrapper,
unblocking `CnaTests` from building under MinGW-w64 (`D3D9`/`D3D11`/`D3D12`), plus a follow-up
`gtest_discover_tests` CTest-registration fix.

## Executive Verdict
**Confirmed a real, direct contradiction with a sibling document dated the same day.** This document's
own status banner states: *"Status: FULLY IMPLEMENTED 2026-07-15... `CnaTests` now compiles cleanly
under `CNA_GRAPHICS_BACKEND=D3D9` (first time ever for any Windows-cross backend)... `ctest -L D3D9`
runs clean, 4383 tests are registered."* However, `docs/d3d9-backend.md`'s own "Known limitations
(2026-07-15)" section — the identical date — states: *"**`CnaTests` does not build under D3D9**
(`D9-123`) — same POSIX `::setenv()` wall `D3D11` already documents."* Both documents cite the exact
same task ID (`D9-123`) for opposite conclusions.

## Checklist Results
- This document's own implementation-result section (numbered steps 1-5) is detailed and specific
  (exact test-suite names, exact pass counts — "491/491 pass," "4383 tests registered," a described
  `CROSSCOMPILING_EMULATOR`/Wine-wrapper mechanism with a stated rationale for why per-test Wine
  spawning was rejected) — this level of specificity reads as a genuine completed-work record, not
  a speculative proposal being marked done prematurely.
- The correction note ("`SoundEffectTests.cpp` is 17 `setenv` sites, not 18... 60 `setenv` + 2
  `unsetenv` = 62 total, not 65") is the kind of small, honest self-correction consistent with this
  project's documentation style elsewhere.

## Detailed Findings

### MEDIUM — Direct contradiction with docs/d3d9-backend.md's "Known limitations" section, same date, same task ID
See Executive Verdict for the full description. Given this document's own step-by-step implementation
narrative is far more detailed and specific (exact commands, exact test counts, a described and
justified `CROSSCOMPILING_EMULATOR` fix for a second, previously-hidden blocker) than
`d3d9-backend.md`'s single unelaborated bullet point, the far more likely explanation is that
**`d3d9-backend.md`'s "Known limitations" bullet is the stale one** — probably copy-forwarded from an
earlier, pre-fix draft of that document's "Known limitations" section and never removed once `D9-123`
actually closed the gap this document describes. This is not resolvable with certainty from either
document's text alone; whoever next touches either file should reconcile them (most likely by
deleting the stale bullet from `d3d9-backend.md`).

## Cross-File Observations
Directly contradicts one specific bullet in `docs/d3d9-backend.md`'s "Known limitations" section —
see `docs/d3d9-backend.md.audit.md` for the paired finding. No other cross-file inconsistency found;
this document's claim about `D3D11`/`D3D12` independently having hit and deferred the identical
`::setenv()` wall (`plans/plan_dx.md` `DX-15`/`DX-115`) is consistent with `docs/d3d11-backend.md`'s own
"Known limitations" section, which does not claim `CnaTests` builds for D3D11 and does not contradict
this document.

## Missing or Weak Tests
N/A — a build-infrastructure proposal/implementation record, not describing testable application
code directly.

## Positive Findings
The `gtest_discover_tests` follow-up finding (a real, previously-invisible second blocker — a Windows
PE32+ binary cannot be directly executed by CMake's test-discovery step on a Linux host — only
surfaced once the first blocker was cleared) and its cost-aware fix (measuring that per-test Wine
spawning would cost ~87 minutes before rejecting that approach in favor of
`CROSSCOMPILING_EMULATOR`) is a strong example of "measure before choosing an approach" engineering
discipline.

## Final Assessment
One MEDIUM finding: a same-day, same-task-ID direct contradiction with `docs/d3d9-backend.md`'s
"Known limitations" section. This document's own detailed implementation record is significantly more
credible than the single, unelaborated contradicting bullet in the sibling file — the sibling
document is the more likely candidate for correction.
