# Audit: tests/Microsoft/Xna/Framework/Graphics/GraphicsExceptionTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Graphics/GraphicsExceptionTests.cpp` (109 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-graphics` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `DeviceLostException.hpp`, `DeviceNotResetException.hpp`,
  `NoSuitableGraphicsDeviceException.hpp`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises all three graphics device exception types' default/custom messages and base-class
catchability.

## Executive Verdict
**MEDIUM finding: this test file explicitly and directly bakes in the already-confirmed MEDIUM
production defect as the intended, tested contract.** The sibling `device_core` production-code
fork confirmed `DeviceLostException`/`DeviceNotResetException`/`NoSuitableGraphicsDeviceException`
all incorrectly derive from `std::runtime_error` instead of `System::Exception` — this file's own
tests (`InheritsFromRuntimeError`, `CatchableAsRuntimeError`, for all three types) directly assert
exactly that inheritance relationship as correct, by name, in the test name itself.

## Checklist Results
- `DeviceLostExceptionTest.InheritsFromRuntimeError` (line 18-25):
  `const std::runtime_error& base = e;` — only compiles/passes because the class derives from
  `std::runtime_error`. If this were fixed to derive from `System::Exception` per the project's own
  convention (and the sibling fork's own suggested fix), this test would fail to compile.
- `DeviceLostExceptionTest.CatchableAsRuntimeError` (line 39-44): `EXPECT_THROW({ throw
  DeviceLostException(); }, std::runtime_error);` — same issue, one level more subtle (a runtime
  assertion rather than a compile-time one, so a base-class change would cause a runtime test
  failure instead of a build failure, but the effect is the same: this test actively resists a
  correct fix).
- Identical shape repeated for `DeviceNotResetExceptionTest` and `NoSuitableGraphicsDeviceExceptionTest`
  — all three exception types share the identical "inherits from/catchable as `std::runtime_error`"
  test pair, matching the identical production defect the sibling fork found in all three.
- The message-content tests (`DefaultMessage`, `CustomMessage`) are otherwise correct and
  independent of the base-class question — they would continue to pass unchanged after a correct
  fix to the exception hierarchy, since `System::Exception` (like `std::runtime_error`) exposes
  `what()`/an equivalent message accessor.

## Detailed Findings

### MEDIUM — Test suite explicitly asserts the wrong exception base class as the intended contract, for all three graphics device exceptions
See Checklist Results above. Unlike `GraphicsDeviceValidationTests.cpp` (audited separately, which
merely *reflects* current behavior for one specific throw site without asserting on the base-class
relationship as a named contract), this file's test names and bodies make the `std::runtime_error`
inheritance an explicit, intentional-looking assertion — six tests total (two per exception type)
would need to be rewritten, not merely left alone, if the production code is corrected to derive
from `System::Exception` as the project's established convention and the sibling fork's own
suggested fix recommend.

**Suggested fix** (report-only; no source changes made per this audit's scope): once
`DeviceLostException`/`DeviceNotResetException`/`NoSuitableGraphicsDeviceException` are corrected to
derive from `System::Exception`, update all six `InheritsFrom.../CatchableAs...` tests in this file
to assert against `System::Exception` instead of `std::runtime_error`.

## Cross-File Observations
Directly corroborates and strengthens the sibling `device_core` production-code fork's own MEDIUM
finding (`include/Microsoft/Xna/Framework/Graphics/DeviceLostException.hpp.audit.md` and its two
siblings) — this test file is independent evidence that the wrong base class is not merely an
unnoticed implementation detail but has been explicitly locked in as a tested contract, making a
future fix a two-file (production + test) change, not a one-file change.

## Missing or Weak Tests
Once fixed, the corrected tests should also verify the standard `(message, innerException)`
constructor the sibling fork noted is missing from all three types (currently untestable since it
doesn't exist).

## Positive Findings
The message-content tests (`DefaultMessage` containing "lost"/"reset" substrings, `CustomMessage`
exact-match) are correct and forward-compatible with a base-class fix.

## Final Assessment
One MEDIUM finding: this test file explicitly bakes in the wrong (`std::runtime_error`) exception
base class as the intended, tested contract for all three graphics device exception types — the
strongest form of "test actively resists a correct fix" found in this batch.
