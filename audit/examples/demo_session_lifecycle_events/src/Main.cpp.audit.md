# Audit: examples/demo_session_lifecycle_events/src/Main.cpp

## Metadata
- Source file: `examples/demo_session_lifecycle_events/src/Main.cpp` (119 lines)
- Audit status: AUDITED
- Subsystem: `examples-demo_session_lifecycle_events` shard
- File type: standalone console demo executable, `NetworkSessionType::Local` (Task 15.7)
- XNA/FNA relevance: exercises `NetworkSession::StartGame()`/`EndGame()`/`RemoveGamer()` and the
  resulting `GameStarted`/`GameEnded`/`SessionEnded`/`WriteArbitratedLeaderboard`/
  `WriteUnarbitratedLeaderboard`/`WriteTrueSkill` events
- Related production code: `NetworkSession.hpp`/`.cpp` (already audited this session as part of the
  `xna-net` shard)

## Purpose
Demonstrates a full `Lobby -> Playing -> Lobby -> Ended` `NetworkSession` lifecycle tour using only
the local event queue (no real networking), then manually raises all three leaderboard delegates to
prove their wiring is sound despite production code never calling them.

## Executive Verdict
Correct, and directly corroborates two findings independently reached while auditing
`NetworkSession.hpp`/`.cpp` in this same session's `xna-net` shard work. First: this demo's own
comment explicitly corrects an assumption in its own originating task description — `EndGame()`
transitions `Playing` back to `Lobby`, not to `Ended` — matching this session's own direct reading
of `NetworkSession::EndGame()` (`NetworkSession.cpp` lines 498-513), which queues a `StateChange` to
`NetworkSessionState::Lobby`, not `Ended`. Second: the demo explicitly demonstrates and calls out
that `WriteArbitratedLeaderboard`/`WriteUnarbitratedLeaderboard`/`WriteTrueSkill` are fully wired
(subscribing and `Raise()`-ing them works) but that "nothing in production code ever calls `Raise()`
on them (confirmed by grep — zero call sites outside this demo)" — matching this session's own
`NetworkSession.hpp` audit finding that these three events are "declared for API parity; never
raised (leaderboards/TrueSkill unimplemented upstream)."

## Checklist Results
- Correctly reaches `Ended` state only via an actual `RemoveGamer()` call (line 109), matching the
  real production path a genuine disconnect/session-end would funnel through, rather than
  fabricating a shortcut to reach that state.
- Manual leaderboard-delegate raises (lines 105-107) use `WriteLeaderboardsEventArgs::CreateInternal`
  (the only public construction path — confirmed in this session's own audit of
  `WriteLeaderboardsEventArgs`) — correct, matches that type's documented internal-use-only
  construction contract.
- `session->Dispose()` is called at the end (line 117) — correct cleanup, consistent with
  `NetworkSession`'s documented caller-owns-and-must-Dispose ownership contract (also audited this
  session).

## Detailed Findings
None.

## Cross-File Observations
Independently confirms, via direct runtime observation rather than static source reading alone, two
claims made in `audit/include/Microsoft/Xna/Framework/Net/NetworkSession.hpp.audit.md` and
`audit/src/Microsoft/Xna/Framework/Net/NetworkSession.cpp.audit.md`: the `EndGame()` transition
target and the leaderboard-events'-never-raised-in-production status. This kind of independent,
runtime corroboration from a different audit angle (a working demo vs. static source reading)
strengthens confidence in both findings.

## Missing or Weak Tests
This is itself a demo with its own printed-summary self-check (`leaderboardFireCount`, expected 3)
— a lightweight form of self-verification, though not a hard pass/fail exit code like
`demo_packet_roundtrip`'s.

## Positive Findings
The demo's own comment honestly corrects an inaccuracy in its originating task description after
verifying against the real source — a good practice that prevents documentation drift from
compounding.

## Final Assessment
No findings.
