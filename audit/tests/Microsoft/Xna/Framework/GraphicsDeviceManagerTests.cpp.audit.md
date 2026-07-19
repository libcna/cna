# Audit: tests/Microsoft/Xna/Framework/GraphicsDeviceManagerTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/GraphicsDeviceManagerTests.cpp` (2 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-framework-core` shard
- File type: C++ test file (Google Test) — effectively empty
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::GraphicsDeviceManager`
- Main related tests: N/A (this IS a test file)

## Purpose
Nominally the test file for `GraphicsDeviceManager`.

## Executive Verdict
**HIGH finding: this file contains zero tests.** Its entire content is:
```cpp
// SPDX-License-Identifier: MS-PL
// No tests: GraphicsDeviceManager requires a live Game, SDL window, and graphics backend.
```
This project's own already-committed `xna-framework-core` shard audit confirmed a real
HIGH-severity defect: `GraphicsDeviceManager` never subscribes to its own `GraphicsDevice`'s
`DeviceResetting`/`DeviceReset` events (see
`include/Microsoft/Xna/Framework/GraphicsDeviceManager.hpp.audit.md`/`.cpp.audit.md`), meaning a
device-lost-then-reset cycle triggered from inside the backend never reaches any code listening on
`GraphicsDeviceManager`'s own public `DeviceReset`/`DeviceResetting` events — the conventional
`IGraphicsDeviceService` surface most resource-reload code subscribes to. With this test file
containing no tests at all, that HIGH bug has **zero chance of being caught**.

## Checklist Results
Not applicable — no tests exist to check against the project's coverage rules.

## Detailed Findings

### HIGH — Zero test coverage for `GraphicsDeviceManager`, despite a confirmed real event-forwarding defect
See Executive Verdict. **Suggested fix** (report-only; no source changes made per this audit's
scope): a test that resets `Graphics::GraphicsDevice` directly (bypassing
`GraphicsDeviceManager::ApplyChanges()`/`CreateDevice()`) and asserts whether
`GraphicsDeviceManager::DeviceReset` fires would directly catch this — already recommended in
`GraphicsDeviceManager.hpp.audit.md`'s own "Missing or Weak Tests" section, independently confirmed
here as still entirely absent.

## Cross-File Observations
Shares the identical "no tests, requires live X" pattern with `GameTests.cpp` (same shard, also
covering a class with a confirmed HIGH defect) — see that file's own report for the broader
observation that this codebase's own `GameWindowTests.cpp` already demonstrates a working pattern
(real SDL init + `GTEST_SKIP()` fallback) that neither `Game` nor `GraphicsDeviceManager` were
given the equivalent attempt to use.

## Missing or Weak Tests
The entire file. See Detailed Findings for the specific test that would catch the known regression.

## Positive Findings
None — the file has no content to evaluate positively.

## Final Assessment
One HIGH finding: this test file provides zero coverage for `GraphicsDeviceManager`, leaving a
confirmed real defect (missing device-event forwarding) with no regression test anywhere in the
suite.
