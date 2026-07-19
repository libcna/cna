# Audit: tests/Microsoft/Xna/Framework/GamerServices/GamerServicesDataTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/GamerServices/GamerServicesDataTests.cpp` (315 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-gamerservices` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `PropertyDictionary`, `LeaderboardIdentity`, `GamerPresence`,
  `GamerPrivileges`, `GameDefaults`, `Achievement`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises `PropertyDictionary`'s full `SetValue`/`GetValueX`/indexer/`Add`/`Remove`/`Clear`/`Keys`/
`Values`/`CopyTo` surface, plus `LeaderboardIdentity`, `GamerPresence`, `GamerPrivileges`,
`GameDefaults`, and `Achievement`'s basic construction/property tests.

## Executive Verdict
Thorough for happy-path and documented-quirk coverage, and directly relevant to two cross-check
items: it **bakes in the confirmed production bug's exact (wrong) exception type as the expected
behavior for `PropertyDictionary`'s missing-key indexers** (cross-check item 5), and **never
exercises `GamerPresence`'s resolved presence string at all** (cross-check item 2, consistent with
no public getter existing for it).

## Checklist Results
- `MutableIndexerThrowsOnMissingKeyInsteadOfAutoVivifying`/`ConstIndexerThrowsOnMissingKey` both
  assert `std::out_of_range` — the raw `std::` exception type this session's own
  `PropertyDictionary.hpp.audit.md` production report flags as a MEDIUM finding (should be
  `System::Collections::Generic::KeyNotFoundException` per the project's established exception-type
  convention). These tests correctly prove the *current* behavior (no crash, a catchable exception),
  but they lock in the wrong exception type as "correct" — a future fix to the production code would
  need these two tests updated in lockstep, or they'd start failing for the right reason.
- `CopyToAlwaysThrows` correctly asserts `System::NotImplementedException` — a real `System::` type,
  showing the exception-type gap is specific to the missing-key indexer paths, not universal across
  this class.
- `GamerPresenceTest` (lines 243-259) only covers `DefaultMode`/`SetPresenceMode`/`SetPresenceValue`
  — no test reads or asserts on any resolved/formatted presence *string* at all, consistent with
  this session's own `GamerPresence.cpp.audit.md` finding that no public getter exposes the
  (misindexed) `presenceModeStrings_` table's output.
- `GameDefaultsTest.DefaultValues`'s own comment correctly explains the FNA-faithful ordinal-0
  defaults (`GameDifficulty::Easy`, `ControllerSensitivity::Low`), matching the parallel production
  audit's own confirmation of this.

## Detailed Findings

### MEDIUM (test-coverage gap, corresponding to a confirmed production MEDIUM finding) — `PropertyDictionary`'s missing-key indexer tests assert the wrong (raw `std::`) exception type as expected behavior
`MutableIndexerThrowsOnMissingKeyInsteadOfAutoVivifying` and `ConstIndexerThrowsOnMissingKey` both
`EXPECT_THROW(..., std::out_of_range)`. Per this session's own `include/Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp.audit.md`
report, real .NET's `Dictionary<TKey,TValue>` indexer throws `KeyNotFoundException`, and this
project's own established convention is to use `System::Collections::Generic::KeyNotFoundException`
here — meaning these two tests have baked in the confirmed-wrong exception type as their expected
value. This is the definitive answer for this fork's cross-check item 5: the production bug is
**baked into the test suite as expected behavior**, not merely undetected by it — fixing the
production code without also updating these two tests would break them.

## Cross-File Observations
This is the clearest and most direct evidence for cross-check item 5 (`PropertyDictionary`'s
exception-type convention violation) and corroborates item 2 (`GamerPresence`'s string table has no
test coverage, consistent with no public accessor existing for it).

## Missing or Weak Tests
No test exercises `GamerPresence`'s resolved presence string (consistent with there being no public
way to obtain it, per the production audit) — not fixable at the test level without a production
change first.

## Positive Findings
Comprehensive coverage of `PropertyDictionary`'s value-typed accessors (int/float/double/long/
string/outcome/DateTime/TimeSpan/Stream), each cross-checked against FNA's real behavior where
relevant (e.g. the `Stream` test's own comment explaining the lack of a matching `SetValue`
overload in FNA either).

## Final Assessment
One MEDIUM finding: two tests bake in the confirmed-wrong `std::out_of_range` exception type as
expected `PropertyDictionary` behavior, meaning fixing the underlying production bug requires these
tests to be updated in the same change, not just the production code.
