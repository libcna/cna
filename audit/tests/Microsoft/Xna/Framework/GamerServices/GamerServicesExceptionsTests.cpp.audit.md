# Audit: tests/Microsoft/Xna/Framework/GamerServices/GamerServicesExceptionsTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/GamerServices/GamerServicesExceptionsTests.cpp` (154 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-gamerservices` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `NetworkException`, `NetworkNotAvailableException`,
  `GamerPrivilegeException`, `GamerServicesNotAvailableException`, `GameUpdateRequiredException`,
  `GuideAlreadyVisibleException`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises the standard constructor set (default, message, message+inner) for all six
`GamerServices` exception types, plus base-class-catchability checks.

## Executive Verdict
Correct and complete for what it tests, but its `GuideAlreadyVisibleException` coverage only ever
exercises the type's own standalone construction/properties — never its (non-existent, per the
production audit) real usage at `Guide`'s actual already-pending guard sites. This is exactly
consistent with, and directly corroborates, this session's confirmed `xna-gamerservices` shard
finding that `GuideAlreadyVisibleException` is "fully implemented and unit-tested but dead code in
production" (cross-check item 4).

## Checklist Results
- `DefaultCtor`'s own comment for `NetworkException` explains it was "confirmed against FNA's own
  real default constructor (`: base()`, no hardcoded message in any of the 6 GamerServices
  exception types)" — a genuine cross-check against the reference behavior, not an assumption.
- Every one of the six types gets the same three-constructor coverage consistently.

## Detailed Findings
None. This file's `GuideAlreadyVisibleExceptionTest` suite (lines 137-153) tests only
`DefaultCtor`/`MessageCtor`/`MessageAndInnerCtor` — the exact same three-constructor pattern every
other exception type in this file gets — with no test asserting this exception type is actually
thrown by any real `Guide` code path. Combined with `GamerServicesServiceTests.cpp`'s own
`BeginShowKeyboardInputThrowsWhileAnotherIsPending`/`BeginShowMessageBoxThrowsWhileAnotherIsPending`
tests (which correctly assert `System::InvalidOperationException`, not this type), this confirms
the test suite accurately reflects — rather than incorrectly bakes in an assumption about —
`GuideAlreadyVisibleException`'s real dead-code status in production.

## Cross-File Observations
See `GamerServicesServiceTests.cpp.audit.md` for the corroborating evidence from the actual guard-
site tests.

## Missing or Weak Tests
None identified — the absence of a "thrown by a real Guide call" test for
`GuideAlreadyVisibleException` is itself accurate given the type's genuinely unused status in
production; adding such a test would require the production code to actually throw it first.

## Positive Findings
Consistent, complete constructor coverage across all six exception types, with the
`NetworkException`-default-message test explicitly cross-checked against real FNA behavior.

## Final Assessment
No findings — this file's test coverage accurately, if implicitly, reflects the confirmed
production finding that `GuideAlreadyVisibleException` is currently dead code.
