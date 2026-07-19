# Audit: tests/Microsoft/Xna/Framework/Net/NetEventArgsTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Net/NetEventArgsTests.cpp` (108 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-net` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for all Net event-args types (`GameEndedEventArgs`,
  `GameStartedEventArgs`, `GamerJoinedEventArgs`, `GamerLeftEventArgs`, `HostChangedEventArgs`,
  `NetworkSessionEndedEventArgs`, `WriteLeaderboardsEventArgs`)
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises construction, property storage, and `System::EventArgs` inheritance for every Net
event-args type.

## Executive Verdict
Correct, and notably careful about a subtle test-design pitfall: using two distinct, named sentinel
`NetworkGamer` values (rather than `nullptr` for every gamer parameter) specifically so a
constructor argument-order bug (e.g. `HostChangedEventArgs` swapping `oldHost`/`newHost`) would
actually be caught.

## Checklist Results
- The file's own top comment explains precisely why this matters: with `nullptr` used for every
  gamer parameter (the prior pattern, per the comment), a swapped-argument bug would be invisible
  since both "old" and "new" would still read back as `nullptr` either way.
  `HostChangedEventArgsTest.StoresOldAndNewHost` uses two distinct sentinels and explicitly asserts
  `EXPECT_NE(args.getOldHostProperty(), args.getNewHostProperty())` in addition to checking each
  individually — a real, non-tautological proof the constructor didn't swap them.
- Every type's `InheritsEventArgs` test correctly uses `dynamic_cast<System::EventArgs*>` rather
  than merely checking the type compiles where an `EventArgs&` is expected — a genuine runtime
  proof of the inheritance relationship.

## Detailed Findings
None.

## Cross-File Observations
None beyond what's already noted in the `xna-net` production shard's own audit of these exact
event-args types (all confirmed correct there).

## Missing or Weak Tests
Not identified in this pass.

## Positive Findings
The two-distinct-sentinels technique for catching constructor argument-order bugs is exactly the
kind of test design this audit has repeatedly found valuable elsewhere and is a strong pattern to
see applied consistently here.

## Final Assessment
No findings.
