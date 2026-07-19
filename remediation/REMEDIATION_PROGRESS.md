# REMEDIATION_PROGRESS.md — Live Status Tracker

**This is the only file in `remediation/` expected to change during implementation.**
`MASTER_REMEDIATION_PLAN.md`, `REMEDIATION_INDEX.md`, `REMEDIATION_DEPENDENCIES.md`, and
`REMEDIATION_TRACEABILITY.md` are the frozen plan; this file records what actually happened.

## Status vocabulary

| Status | Meaning |
|---|---|
| `NOT STARTED` | No work begun |
| `VERIFYING` | Reproducing the finding before implementing (tasks with `Verify: YES`) |
| `IN PROGRESS` | Implementation underway |
| `BLOCKED` | Waiting on a dependency or an owner decision — **record which, in Notes** |
| `IN REVIEW` | Implemented, awaiting review/CI |
| `DONE` | Merged, tests pass, completion + verification criteria met |
| `NOT REPRODUCED` | Investigated and the finding did not hold. **A valid outcome — record the evidence.** |
| `DEFERRED` | Consciously postponed. **Record who decided and why.** |

`NOT REPRODUCED` is not failure. Several tasks rest on static analysis that was never executed; a
disproved finding is a real result and should be recorded with the same rigor as a fix.

## Rules

1. Update the row when status changes. Do not batch updates at the end.
2. `DONE` requires **both** the completion and verification criteria from the master plan, not just
   "the code compiles and the old test passes."
3. If implementation reveals the root cause differs from the plan's, **record that in Notes and say
   so** — do not silently widen scope. A wrong root-cause hypothesis in the plan is worth knowing.
4. New findings discovered during remediation get a new ID in the same scheme; append them to the
   "Discovered during remediation" table at the bottom. Do not overload an existing task.
5. Nothing here edits `audit/`. The audit is the frozen evidence baseline.

## Overall progress

| Priority | Total | Done | In progress | Blocked | Not started |
|---|---|---|---|---|---|
| P0 | 11 | 0 | 0 | 0 | 11 |
| P1 | 21 | 0 | 0 | 0 | 21 |
| P2 | 44 | 0 | 0 | 0 | 44 |
| P3 | 28 | 0 | 0 | 0 | 28 |
| **Total** | **104** | **0** | **0** | **0** | **104** |

## Wave 0 — make the tests trustworthy

| ID | Status | Owner | Branch | Notes |
|---|---|---|---|---|
| REMED-BUILD-001 | NOT STARTED | | | **Start here.** Expect follow-on triage of ~220 newly-running tests. |
| REMED-BUILD-002 | NOT STARTED | | | Decide first whether `XactFileGen.hpp` makes the copy step obsolete. |

## Wave 1 — security and memory safety

| ID | Status | Owner | Branch | Notes |
|---|---|---|---|---|
| REMED-CONTENT-001 | NOT STARTED | | | CRITICAL. Fix in shared content code, **not** per-backend. |
| REMED-CONTENT-002 | NOT STARTED | | | Shared helper first, then 3 call sites, then repo-wide grep. |
| REMED-CONTENT-003 | NOT STARTED | | | Port the sibling readers' existing check verbatim. |
| REMED-CONTENT-006 | NOT STARTED | | | VERIFY first. Then audit all 7 `XnbReadLimits` fields for consumers. |
| REMED-GFX-001 | NOT STARTED | | | Serialize against all other EasyGL work. |
| REMED-GFX-002 | NOT STARTED | | | Sequence before GFX-003 (same file). |
| REMED-GFX-003 | NOT STARTED | | | After GFX-002. Resize tables **and** add flag operators together. |
| REMED-NET-001 | NOT STARTED | | | Land with NET-003 as one change. |
| REMED-NET-003 | NOT STARTED | | | Atomic with NET-001. |
| REMED-DEVICES-001 | NOT STARTED | | | Prefer `shared_ptr` over holding a mutex across a UI-blocking call. |
| REMED-MEDIA-001 | NOT STARTED | | | VERIFY on a 32-bit build first. |

## Wave 1 (parallel) — unblockers

| ID | Status | Owner | Branch | Notes |
|---|---|---|---|---|
| REMED-BUILD-004 | NOT STARTED | | | After BUILD-001. Budget triage time for the first full run. |
| REMED-BUILD-008 | NOT STARTED | | | **Blocks GFX-014 and GFX-015.** |
| REMED-TEST-002 | NOT STARTED | | | **Blocks CORE-006 and CORE-007.** Follow `GameWindowTests.cpp`'s pattern. |
| REMED-TEST-004 | NOT STARTED | | | Write each test **before** its production fix, so it fails first. |

## Waves 2-5 — remaining tasks

| ID | Pri | Status | Owner | Branch | Notes |
|---|---|---|---|---|---|
| REMED-AUDIO-001 | P3 | NOT STARTED | | | |
| REMED-AUDIO-002 | P3 | NOT STARTED | | | |
| REMED-BUILD-003 | P1 | NOT STARTED | | | |
| REMED-BUILD-005 | P2 | NOT STARTED | | | |
| REMED-BUILD-006 | P2 | NOT STARTED | | | |
| REMED-BUILD-007 | P3 | NOT STARTED | | | |
| REMED-BUILD-009 | P3 | NOT STARTED | | | |
| REMED-CONTENT-004 | P1 | NOT STARTED | | | |
| REMED-CORE-001 | P1 | NOT STARTED | | | |
| REMED-CORE-002 | P2 | NOT STARTED | | | |
| REMED-CORE-003 | P2 | NOT STARTED | | | |
| REMED-CORE-004 | P1 | NOT STARTED | | | |
| REMED-CORE-005 | P2 | NOT STARTED | | | |
| REMED-CORE-006 | P1 | NOT STARTED | | | |
| REMED-CORE-007 | P1 | NOT STARTED | | | |
| REMED-CORE-008 | P2 | NOT STARTED | | | |
| REMED-CORE-009 | P2 | NOT STARTED | | | |
| REMED-CORE-010 | P3 | NOT STARTED | | | |
| REMED-CORE-011 | P3 | NOT STARTED | | | |
| REMED-CORE-012 | P3 | NOT STARTED | | | |
| REMED-CORE-013 | P3 | NOT STARTED | | | |
| REMED-DEVICES-002 | P2 | NOT STARTED | | | |
| REMED-DEVICES-003 | P3 | NOT STARTED | | | |
| REMED-DOCS-001 | P2 | NOT STARTED | | | |
| REMED-DOCS-002 | P2 | NOT STARTED | | | |
| REMED-DOCS-003 | P3 | NOT STARTED | | | |
| REMED-GFX-004 | P1 | NOT STARTED | | | |
| REMED-GFX-005 | P1 | NOT STARTED | | | |
| REMED-GFX-006 | P1 | NOT STARTED | | | |
| REMED-GFX-007 | P1 | NOT STARTED | | | |
| REMED-GFX-008 | P1 | NOT STARTED | | | |
| REMED-GFX-009 | P1 | NOT STARTED | | | |
| REMED-GFX-010 | P2 | NOT STARTED | | | |
| REMED-GFX-011 | P1 | NOT STARTED | | | |
| REMED-GFX-012 | P1 | NOT STARTED | | | |
| REMED-GFX-013 | P1 | NOT STARTED | | | |
| REMED-GFX-014 | P1 | NOT STARTED | | | |
| REMED-GFX-015 | P2 | NOT STARTED | | | |
| REMED-GFX-016 | P1 | NOT STARTED | | | |
| REMED-GFX-017 | P1 | NOT STARTED | | | |
| REMED-GFX-018 | P1 | NOT STARTED | | | |
| REMED-GFX-019 | P1 | NOT STARTED | | | |
| REMED-GFX-020 | P2 | NOT STARTED | | | |
| REMED-GFX-021 | P2 | NOT STARTED | | | |
| REMED-GFX-022 | P1 | NOT STARTED | | | |
| REMED-GFX-023 | P2 | NOT STARTED | | | |
| REMED-GFX-024 | P2 | NOT STARTED | | | |
| REMED-GFX-025 | P2 | NOT STARTED | | | |
| REMED-GFX-026 | P2 | NOT STARTED | | | |
| REMED-GFX-027 | P2 | NOT STARTED | | | |
| REMED-GFX-028 | P2 | NOT STARTED | | | |
| REMED-GFX-029 | P2 | NOT STARTED | | | |
| REMED-GFX-030 | P2 | NOT STARTED | | | |
| REMED-GFX-031 | P3 | NOT STARTED | | | |
| REMED-GFX-032 | P3 | NOT STARTED | | | |
| REMED-GFX-033 | P2 | NOT STARTED | | | |
| REMED-GFX-034 | P2 | NOT STARTED | | | |
| REMED-GFX-035 | P2 | NOT STARTED | | | |
| REMED-GFX-036 | P2 | NOT STARTED | | | |
| REMED-GFX-037 | P2 | NOT STARTED | | | |
| REMED-GFX-038 | P2 | NOT STARTED | | | |
| REMED-GFX-039 | P2 | NOT STARTED | | | |
| REMED-GFX-040 | P2 | NOT STARTED | | | |
| REMED-GFX-041 | P2 | NOT STARTED | | | |
| REMED-GFX-042 | P2 | NOT STARTED | | | |
| REMED-GFX-043 | P1 | NOT STARTED | | | |
| REMED-GFX-044 | P2 | NOT STARTED | | | |
| REMED-GFX-045 | P3 | NOT STARTED | | | |
| REMED-GFX-046 | P3 | NOT STARTED | | | |
| REMED-GFX-047 | P3 | NOT STARTED | | | |
| REMED-GFX-048 | P3 | NOT STARTED | | | |
| REMED-GFX-049 | P3 | NOT STARTED | | | |
| REMED-GFX-050 | P3 | NOT STARTED | | | |
| REMED-GFX-051 | P2 | NOT STARTED | | | |
| REMED-MEDIA-002 | P1 | NOT STARTED | | | |
| REMED-MEDIA-003 | P2 | NOT STARTED | | | |
| REMED-MEDIA-004 | P2 | NOT STARTED | | | |
| REMED-NET-002 | P1 | NOT STARTED | | | |
| REMED-NET-004 | P2 | NOT STARTED | | | |
| REMED-NET-005 | P3 | NOT STARTED | | | |
| REMED-NET-006 | P3 | NOT STARTED | | | |
| REMED-NET-007 | P3 | NOT STARTED | | | |
| REMED-TEST-001 | P2 | NOT STARTED | | | |
| REMED-TEST-003 | P2 | NOT STARTED | | | |
| REMED-TEST-005 | P3 | NOT STARTED | | | |
| REMED-TEST-006 | P2 | NOT STARTED | | | |
| REMED-TEST-007 | P3 | NOT STARTED | | | |

## Discovered during remediation

New findings surfaced while implementing. Give each a new ID in the same scheme; do not overload an
existing task.

| ID | Title | Sev | Pri | Found while working on | Status |
|---|---|---|---|---|---|
| _(none yet)_ | | | | | |

## Decisions log

Owner decisions required by the plan, plus any scope calls made during implementation.

| Date | Task | Decision | Decided by |
|---|---|---|---|
| _(none yet)_ | `REMED-GFX-035` | Which GLSL dialect is the contract? | pending |
| _(none yet)_ | `REMED-CORE-011` | Implement `CNA::Runtime` or delete it? | pending |
| _(none yet)_ | `REMED-BUILD-007` | Is `CNA::Internal::Net`'s MIT licensing deliberate? | pending |
| _(none yet)_ | `REMED-BUILD-002` | Does `XactFileGen.hpp` make the Content copy obsolete? | pending |

## Findings that did not reproduce

Record disproved findings here with evidence. This is a real result and improves the audit baseline's
accuracy for future work.

| ID | Investigated | Evidence it did not reproduce | Recorded by |
|---|---|---|---|
| _(none yet)_ | | | |
