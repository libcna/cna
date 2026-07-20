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
| P0 | 11 | 2 | 0 | 0 | 9 |
| P1 | 21 | 0 | 0 | 0 | 21 |
| P2 | 44 | 0 | 0 | 0 | 44 |
| P3 | 28 | 0 | 0 | 0 | 28 |
| **Total** | **104** | **2** | **0** | **0** | **102** |

## Wave 0 — make the tests trustworthy

| ID | Status | Owner | Branch | Notes |
|---|---|---|---|---|
| REMED-BUILD-001 | DONE | | feature/audit | `WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"` added to `gtest_discover_tests(CnaTests ...)` in `cmake/UnitTests.cmake` (one line, matching `EasyGLTests.cmake`/`VulkanTests.cmake`'s existing pattern). Full unfiltered `ctest` run against `cmake-build-debug` (EASYGL) and the direct `./CnaTests` binary now **agree exactly** on the gtest-discovered subset: 5507 tests, 5503 passed / 4 skipped (Accelerometer/Gyroscope hardware skips) / 0 failed, both ways. See "Wave 0 triage" below for the 5 additional failures ctest reports outside that subset (4 already-tracked, 1 newly discovered — recorded below). Completion + verification criteria met. |
| REMED-BUILD-002 | DONE | | feature/audit | Decision: `XactFileGen.hpp` makes the copy obsolete (see Decisions log) — POST_BUILD `copy_directory` removed from `cmake/Examples.cmake`. Verified: unpiped, unfiltered `cmake --build` exits 0 on 3 backends (HEADLESS, SOFTWARE, VULKAN — all rebuilt from a reconfigure of the changed `Examples.cmake`), `cna_demo_xact` itself linking cleanly on all three. Completion + verification criteria met. |

### Wave 0 triage — full post-`REMED-BUILD-001` `ctest` baseline (`cmake-build-debug`, EASYGL)

Total registered CTest tests: 5754 = 5507 gtest-discovered (governed by the `WORKING_DIRECTORY` fix) + 247
separately-registered tests (backend smoke tests, `CnaInputTests`, `easy-gl-*` submodule tests, etc. — these
already had correct `WORKING_DIRECTORY` before this task, per `EasyGLTests.cmake`/`VulkanTests.cmake`, so they
are unaffected by the fix either way). Result: **5749 passed, 5 failed, 4 skipped** (`Total Test time (real) =
583.90 sec`).

The 5 failures are **all outside the 5507 gtest-discovered set** (confirmed by cross-referencing the ctest log
against the direct-binary run — the discovered subset is 5503/4/0 both ways, exactly matching). Triage:

| Failing CTest | Classification | Evidence |
|---|---|---|
| `EasyGL_AvatarRenderer_TintRouting` | Already known — tracked in `MASTER_REMEDIATION_PLAN.md` (REMED-BUILD-003 evidence list; interacts with `REMED-GFX-006`). Not fixed by this task; real production tint-doubling bug (observed left=(81,51,31) vs expected (40,25,15) — almost exactly 2×+clamp). | `AUDIT_CROSS_CUTTING_FINDINGS.md` §CI-masking-risk |
| `EasyGL_MRT_TwoAttachments` | Already known — `REMED-GFX-016`. | `MASTER_REMEDIATION_PLAN.md` line 1238 |
| `EasyGL_GraphicsDevice_ReferenceStencil` | Already known — disclosed in-source (Task 319/872), confirmed still-open by the audit, tracked under REMED-BUILD-003's evidence list. | `AUDIT_CROSS_CUTTING_FINDINGS.md` §2176 |
| `easy-gl-resource-smoke-tests` | Already known and **out of scope** — `REMED-NA-011`, external `easy-gl` sibling repo's own test suite (reference-only per decision D-6), not a CNA finding. | `MASTER_REMEDIATION_PLAN.md` line 3081 |
| `EasyGL_RealWindowResize` | **Genuinely new** — see `REMED-BUILD-010` below. | This session |

None of these 5 are newly *introduced* by the `WORKING_DIRECTORY` fix or by `REMED-BUILD-002`'s cmake edit —
4 are pre-existing findings the audit already tracked (their own CTest registrations already had correct
`WORKING_DIRECTORY`, so the bug being fixed here never hid them), and the 5th is a pre-existing hang whose
trigger (a real desktop compositor on the test `DISPLAY`) is orthogonal to both Wave 0 tasks. No opportunistic
production fixes were made for any of the 4 already-tracked findings, per instruction.

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
| REMED-BUILD-010 | `EasyGL_RealWindowResize` hangs the full 60s CTest `TIMEOUT` under a real desktop compositor (`DISPLAY` = a real logged-in GNOME/Mutter session, not an isolated Xvfb) | MEDIUM | P2 | REMED-BUILD-001 (full unfiltered `ctest` baseline run) | NOT STARTED — recorded, not fixed (out of scope for Wave 0) |

#### REMED-BUILD-010 detail

- **Root cause:** `examples/easygl_real_window_resize_test.cpp` (Task 348) drives a real
  `SDL_SetWindowSize()` and polls up to 300 draw frames for the async X11 resize event to propagate,
  with its own internal timeout that logs a `FAIL` and exits cleanly. Confirmed by isolated re-run:
  under `DISPLAY=:99` (Xvfb, headless) it completes in well under a second with `4/4 PASS`; under
  `DISPLAY=:0` (this machine's real, logged-in GNOME/Mutter session — the value baked into every
  build dir's cached `CNA_TEST_DISPLAY`) it produces **zero output past backend init** and is killed
  by CTest's 60s `TIMEOUT` (confirmed reproducible in isolation, not a parallel-build resource-
  contention artifact — re-ran alone on an otherwise idle machine, same result). The window resize
  or its `SDL_EVENT_WINDOW_RESIZED` delivery appears to never complete against a real compositor,
  most plausibly because Mutter defers/never delivers a `ConfigureNotify` to a window that isn't
  focused/mapped the way a bare Xvfb server does — not verified further, out of scope for this task.
- **Not a stale/incorrectly-authored test:** the test's own logic was independently re-verified
  (matches `audit/examples/easygl_real_window_resize_test.cpp.audit.md`'s "Healthy" verdict) and it
  passes cleanly given the environment it was clearly designed for (an isolated Xvfb, matching every
  other `CNA_TEST_DISPLAY`-driven test's actual runtime environment during the original audit).
- **Not a production (GameWindow/GraphicsDevice) bug:** the 4/4 PASS under Xvfb confirms the actual
  resize/viewport/event-firing logic this test checks is correct; only the interaction with a real
  desktop compositor on the test `DISPLAY` is at fault.
- **Suggested remediation directions (not implemented — recording only):** (a) point
  `CNA_TEST_DISPLAY` at an isolated Xvfb display in every build dir rather than the login session's
  real `DISPLAY`, and/or (b) give this specific test a hard wall-clock watchdog independent of its
  frame-count loop so a stalled resize can't consume the full CTest `TIMEOUT` regardless of which
  display it's pointed at.
- **Scope note:** only this one CTest is affected project-wide (grep confirms
  `easygl_real_window_resize_test.cpp` is EasyGL's only real-OS-resize test; no equivalent test
  exists for other backends).

## Decisions log

Owner decisions required by the plan, plus any scope calls made during implementation.

| Date | Task | Decision | Decided by |
|---|---|---|---|
| _(none yet)_ | `REMED-GFX-035` | Which GLSL dialect is the contract? | pending |
| _(none yet)_ | `REMED-CORE-011` | Implement `CNA::Runtime` or delete it? | pending |
| _(none yet)_ | `REMED-BUILD-007` | Is `CNA::Internal::Net`'s MIT licensing deliberate? | pending |
| 2026-07-20 | `REMED-BUILD-002` | **Yes — the copy step is obsolete, deleted outright (not guarded, not backfilled with a real `Content/` dir).** Investigated `examples/demo_xact/src/XactFileGen.hpp` + `XactDemo.cpp`: `XactDemo::LoadContent()` calls `GenerateXactFiles("Content/Audio")`, which itself calls `std::filesystem::create_directories(audioDir)` and then `XactFileGen::SaveFile()`s a freshly synthesized `Waves.xwb`/`Demo.xgs`/`Sounds.xsb` (sine-wave PCM + minimal XGS/XWB/XSB binaries matching `XactParser.cpp`'s expected layout) directly into that runtime-relative directory — no pre-existing `.xwb`/`.xgs`/`.xsb` asset is ever read from disk. `examples/demo_xact/` contains only `src/` (confirmed: no `Content/` anywhere in the repo, matching the audit finding). The POST_BUILD `copy_directory` in `cmake/Examples.cmake` therefore copied a directory that never existed and could never usefully exist — deleting it is strictly correct, not a stopgap. | Claude (autonomous remediation session, user-directed) |

## Findings that did not reproduce

Record disproved findings here with evidence. This is a real result and improves the audit baseline's
accuracy for future work.

| ID | Investigated | Evidence it did not reproduce | Recorded by |
|---|---|---|---|
| _(none yet)_ | | | |
