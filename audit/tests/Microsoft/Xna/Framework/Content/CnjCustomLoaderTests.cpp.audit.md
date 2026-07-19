# Audit: tests/Microsoft/Xna/Framework/Content/CnjCustomLoaderTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Content/CnjCustomLoaderTests.cpp` (264 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-content` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `ContentManager::RegisterCnjLoader<T>()` (NOXNA content pipeline
  extension, no FNA/XNA equivalent — games registering custom `.cnj` type handlers)
- Main related tests: N/A (this IS a test file)

## Purpose
Tests the game-registered `.cnj` "type"-string-keyed custom loader table: multiple type names
producing the same `T` via different factories, unregistered-type/missing-version/unsupported-
version rejection, recursive content loading from within a factory, and registration-guard
validation (empty name/factory, duplicate registration, already-owned type).

## Executive Verdict
Correct, and a genuinely well-organized fixture split: `CnjCustomLoaderGraphicsTest` (a
`GraphicsDevice`-owning subclass) is used only for the one test that actually needs graphics
(`FactoryCanRecursivelyLoadReferencedTexture`), with an explicit comment (lines 71-75) explaining
why the other 8 tests avoid paying for window/SDL-video-subsystem creation they never use — a
real, deliberate performance/environment-robustness consideration, not an oversight.

**MEDIUM finding, confirming a recurring production-code pattern via test observation**: three
registration-guard tests (`RegisteringForAlreadyOwnedTypeThrowsLogicError`,
`EmptyTypeNameThrowsInvalidArgument`, `EmptyFactoryThrowsInvalidArgument`,
`DuplicateTypeNameForSameTThrowsLogicErrorNotSilentReplace`) all assert on raw `std::logic_error`/
`std::invalid_argument` — meaning `ContentManager::RegisterCnjLoader<T>()`'s production
implementation itself throws these raw `std::` exception types rather than this project's own
`System::ArgumentException`/`System::InvalidOperationException`, consistent with the recurring
exception-type pattern flagged repeatedly elsewhere in this audit. This test file correctly and
faithfully verifies the production code's actual (if non-idiomatic-for-this-project) behavior; the
underlying issue is in `ContentManager.cpp`, not this test.

## Checklist Results
- `MissingCnjVersionThrowsEvenWithRegisteredType`/`UnsupportedCnjVersionThrowsEvenWithRegisteredType`:
  both correctly assert `factoryInvoked` remains `false`, proving the version check happens
  *before* dispatching to the factory, not merely that an exception is eventually thrown somewhere.
- `DuplicateTypeNameForSameTThrowsLogicErrorNotSilentReplace`: correctly verifies the *first*
  factory remains registered after a rejected duplicate registration attempt (not silently
  replaced) — a genuinely meaningful assertion beyond just "the second registration throws."

## Detailed Findings

### MEDIUM — `RegisterCnjLoader<T>()`'s registration-guard exceptions use raw `std::` types, not this project's own `System::` exception types
See Executive Verdict. This is a production-code finding (in `ContentManager.cpp`, not audited
directly in this pass — flagged here since this test file is the evidence) consistent with the
same recurring pattern already documented extensively elsewhere in this audit (e.g. the
`xna-graphics`/`xna-gamerservices` shards' own findings).

## Cross-File Observations
None beyond the exception-type pattern noted above.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The fixture split to avoid unnecessary graphics-device creation for graphics-independent tests is
a genuinely good, deliberate test-performance/robustness design choice.

## Final Assessment
One MEDIUM finding (surfaced via this test file, but rooted in `ContentManager.cpp`'s production
code): `RegisterCnjLoader<T>()`'s guard exceptions use raw `std::` types instead of this project's
own `System::` exception convention.
