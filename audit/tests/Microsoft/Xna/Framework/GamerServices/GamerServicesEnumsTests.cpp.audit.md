# Audit: tests/Microsoft/Xna/Framework/GamerServices/GamerServicesEnumsTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/GamerServices/GamerServicesEnumsTests.cpp` (263 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-gamerservices` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `ControllerSensitivity`, `GameDifficulty`, `GamerPresenceMode`,
  `GamerPrivilegeSetting`, `GamerZone`, `LeaderboardKey`, `LeaderboardOutcome`, `MessageBoxIcon`,
  `NotificationPosition`, `RacingCameraAngle`, `AvatarBodyType`, `AvatarRendererState`,
  `AvatarMouth`, `AvatarEye`, `AvatarEyebrow`, `AvatarAnimationPreset`, `AvatarBone`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises equality and, for most, explicit ordinal-value checks for the shard's simple enums.

## Executive Verdict
Thorough, with one finding directly relevant to this fork's cross-check item 2:
`GamerPresenceModeTest.OrdinalValuesMatchXna`'s own comment explicitly names the exact risk this
audit already confirmed as a real bug (`GamerPresence::setPresenceModeProperty` indexes a 60-entry
string table directly by ordinal) — yet the test itself only verifies the *enum's* ordinal values
are correct, not that the *string table* correctly resolves for each of those ordinals. The test is
aware of, and precisely describes, the exact risk it does not actually catch.

## Checklist Results
- `AvatarBoneTest.RootAndFirstFewValues`/`GapsArePreservedExactly`/`LastValue` correctly test a
  genuinely irregular enum (non-contiguous ordinals with intentional gaps, e.g. no value at ordinal
  4) against "the real XNA reference assembly," with an explicit inline comment flagging the gap
  rather than silently omitting it — a careful, non-lazy verification of an unusual enum shape.
- Every "wire-relevant" or table-indexed enum (`ControllerSensitivity`, `GameDifficulty`,
  `GamerPresenceMode`, `GamerPrivilegeSetting`, `GamerZone`, `LeaderboardKey`, `LeaderboardOutcome`)
  gets an explicit `OrdinalValuesMatchXna` test in addition to the tautological `ValuesExist` form —
  broader and more consistent ordinal-stability coverage than the sibling `xna-net` shard's
  `NetEnumsTests.cpp`, which only added ordinal tests for the two enums it identified as literally
  wire-serialized.

## Detailed Findings

### MEDIUM (test-coverage gap, corresponding to a confirmed production MEDIUM finding) — `GamerPresenceModeTest.OrdinalValuesMatchXna` verifies enum ordinals but not the misindexed string table those ordinals feed into
The test's own comment states: "`GamerPresence::setPresenceModeProperty` indexes a 60-entry string
table directly by this enum's ordinal — a future accidental reordering would silently break
presence strings with nothing else to catch it." This is precisely the risk this session's own
`GamerPresence.cpp.audit.md` production report confirms is **already** realized (the
`presenceModeStrings_` table is alphabetically sorted but indexed by raw enum ordinal, so 59/60
modes resolve to the wrong string) — yet this test only asserts the *enum's own* ordinal values are
correct (e.g. `None=0`, `SinglePlayer=1`, `OnlineCoOp=5`, ..., `CornflowerBlue=59`), which they are.
It never reads the resolved string itself (no public getter exists for it, per the production
report), so it cannot and does not catch the actual, already-present misindexing bug — the test
guards against a *different*, hypothetical future regression (enum reordering) while remaining
blind to the real, current one (the string table's own internal ordering).

**Suggested fix** (report-only; no source changes made per this audit's scope): once
`GamerPresence.cpp`'s string table is fixed (or a `NOXNA` test-only accessor for the resolved
string is added), extend this test (or add a sibling one) to assert the resolved string for a
representative sample of ordinals, not just the enum's own values.

## Cross-File Observations
This finding sharpens cross-check item 2's answer: the risk is not merely *unrecognized* by the
test suite — it's explicitly named in this very test's comment, but the test only covers half of
the actual data-flow (the enum, not the table it indexes), so recognizing the risk didn't translate
into catching the bug.

## Missing or Weak Tests
As above: a test asserting the resolved presence *string* (not just the enum ordinal) for at least
one non-trivial `GamerPresenceMode` value would have caught the confirmed production bug directly.

## Positive Findings
The `AvatarBone` irregular-ordinal tests are a strong, careful verification of an unusual enum
shape, explicitly calling out the intentional gaps rather than glossing over them.

## Final Assessment
One MEDIUM finding: this file's `GamerPresenceModeTest.OrdinalValuesMatchXna` names the exact risk
class the confirmed `GamerPresence` string-table bug falls into, but only tests half of the
relevant data flow (the enum, not the table), so it does not actually catch the bug.
