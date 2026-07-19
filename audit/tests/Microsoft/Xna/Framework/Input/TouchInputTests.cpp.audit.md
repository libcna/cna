# Audit: tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp` (825 lines)
- Audit status: AUDITED (full read, 2 sequential reads)
- Subsystem: `tests-xna-input` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Input::Touch::TouchPanel`/
  `TouchCollection`/`TouchLocation`/`GestureSample`/`TouchPanelCapabilities`
- Main related tests: N/A (this IS a test file)

## Purpose
The largest file in this shard: covers `TouchPanel::GetState`'s pure-read/frame-advance semantics
(Pressed->Moved->retired lifecycle, deterministic ascending-id ordering), gesture
queue FIFO/availability/empty-queue-throw behavior, `GetCapabilities`'s connected-flag/fallback
logic, `EnabledGestures`/display/window-handle property round-trips, `TouchCollection`'s full
`IList`-like surface (indexer bounds-checking, `CopyTo` bounds/insertion, `Contains`/`FindById`/
`IndexOf`, iteration order, `IsReadOnly`'s advisory-only semantics), `TouchLocation`'s constructors
(including NOXNA pressure-carrying overloads), `TryGetPreviousLocation`'s false-path out-param
contract, equality/hash/`ToString` (including deliberate hash/equality asymmetry), and
`GestureSample`/`TouchPanelCapabilities`.

## Executive Verdict
No findings. `GetHashCodeMatchesFnaIdPlusPositionFormula` is a correctly-cited, genuinely
FNA-derived formula (`Id.GetHashCode() + Position.GetHashCode()`, deliberately excluding `State`)
— unlike the `GamePadState`/`MouseState` cases flagged elsewhere in this audit, this one is
real, verified FNA behavior, and the test goes further by explicitly proving the resulting
hash/equality *asymmetry* is intentional (two locations with the same id/position but different
`State` collide in hash yet are correctly unequal via `Equals`).

## Checklist Results
- `GetStateReflectsCurrentTouchSnapshot`/`ReleasedTouchIsReturnedOnceAndThenRemoved` both correctly
  test the pure-read contract (INP-AUD-001): a repeated `GetState()` call without an intervening
  `Update()`/frame-advance must return an unchanged snapshot, not silently promote
  Pressed->Moved or drop a Released touch early.
- `GetStateOrdersMultipleTouchesByAscendingIdRegardlessOfInsertionOrder` (P5-012/DEC-20) uses
  deliberately out-of-order insertion (30, 5, 17) to prove the ordering is a real id-sort, not an
  accident of insertion sequence — a well-designed discriminating test.
- `TryGetPreviousLocationFalsePathWritesInvalidPreviousLocationLikeFna`/
  `FindByIdWritesInvalidSentinelOnOutParamWhenMissing`/`...ForEmptyCollection` (DEC-12) are
  consistent, well-targeted regression tests for the same real FNA out-parameter contract
  (unconditional write, even on the false/not-found path) applied to two distinct methods — each
  pre-seeds the out-param with a distinct sentinel value first, directly proving it gets
  overwritten rather than merely happening to already hold the right value.
- `CopyToThrowsOnOutOfRangeIndexInsteadOfUndefinedBehavior` (task 902) and
  `IndexerThrowsOnOutOfRangeAccess` (task 904) are genuine regression tests for real,
  previously-fixed undefined-behavior bugs (an invalid iterator from a bad `arrayIndex` hitting
  `std::vector::insert` UB; OOB indexer reads) — now converted to a clean, tested `std::out_of_range`.
- `IsReadOnlyIsAdvisoryAndMutationStillSucceedsLikeFna` (P5-001) correctly documents and tests a
  subtle, real FNA quirk: `TouchCollection.IsReadOnly` is hardcoded `true` but is advisory only —
  FNA still mutates the underlying list — and separately discloses CNA's one intentional deviation
  (a default-constructed collection is empty+mutable rather than null-backed and NRE-throwing).
- `PressureIsExcludedFromEqualityHashAndToString` (N-006) correctly verifies a NOXNA extension
  field's deliberate exclusion from `Equals`/`GetHashCode`/`ToString`, keeping those FNA-frozen.
- `ToStringMatchesFnaFormatExactly` pins the exact byte-for-byte FNA string format
  (`"{Position:{X:7 Y:8}}"`), not just a substring check — `ToStringContainsPositionValues` above
  it is a looser complementary check, and the file correctly has both.

## Detailed Findings
None.

## Cross-File Observations
`WindowHandleGetterAndSetterRoundTrip`'s comment explicitly notes it closes a real, previously
undertested gap: `PublicApiInputSignatureFreezeTests.cpp` only pinned this property's *signature*,
not its actual stored-value round-trip — a good example of this audit's broader theme (signature
freezes and behavioral tests are complementary, not substitutes for each other) being explicitly
self-documented in this codebase already.

## Missing or Weak Tests
None identified across this file's very broad surface.

## Positive Findings
The pre-seeded-sentinel technique used for both `TryGetPreviousLocation` and `FindById`'s
false-path out-parameter tests is a rigorous, reusable pattern that directly proves a write
happened rather than merely being consistent with one having happened.

## Final Assessment
No findings. This is the last of the 24 `tests-xna-input` shard files; combined with the 24
`tests-xna-audio` files, all 48 files assigned to this audit fork are now complete.
