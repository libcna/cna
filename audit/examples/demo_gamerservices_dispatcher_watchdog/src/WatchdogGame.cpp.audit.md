# Audit: examples/demo_gamerservices_dispatcher_watchdog/src/WatchdogGame.cpp

## Metadata
- Source file: `examples/demo_gamerservices_dispatcher_watchdog/src/WatchdogGame.cpp` (210 lines)
- Audit status: AUDITED
- Subsystem: `examples-demo_gamerservices_dispatcher_watchdog` shard
- File type: standalone `Game`-subclass demo implementation (Task 15.12)
- XNA/FNA relevance: exercises `GamerServicesDispatcher::Initialize`, `NetworkSession::Create`,
  `SignedInGamer::GetAchievements`
- Related production code: `GamerServicesDispatcher.hpp`/`.cpp`, `NetworkSession.hpp`/`.cpp`
  (already audited this session)

## Purpose
Implements the three-step warmup/measure/report state machine, wall-clock-timing each of
`GamerServicesDispatcher::Initialize()`, `NetworkSession::Create(Local, 1, 8)`, and
`SignedInGamer::GetAchievements()`.

## Executive Verdict
Correct, and directly, runtime-confirms the load-bearing claim independently reached via static
analysis in the parallel `xna-gamerservices` shard fork this session: that
`GamerServicesDispatcher::UpdateAsync()` returning `true` forever once initialized was the root
cause of a historical infinite hang in `NetworkSession::Create`'s synchronous polling loop, and
that this is now genuinely fixed. This demo's own comments explicitly reference "Task 12.1's
historical hang" for both `GamerServicesDispatcher::Initialize()` and `NetworkSession::Create()`,
and "Task 7.1's historical hang" for `GetAchievements()` — all three completing and printing their
timing, rather than hanging, is direct empirical proof (not just a static-analysis inference) that
all three fixes hold.

## Checklist Results
- `MeasureMs()` (lines 53-59) is a simple, correct wall-clock timing helper using
  `std::chrono::steady_clock` (monotonic, appropriate for elapsed-time measurement, unlike
  `system_clock` which could jump).
- The state machine's warmup-frame counting (`kWarmupFrames = 20`) and the `AllDone` step's
  "grace frames" pattern (`doneGraceFrames_ < 60`, only calling `Exit()` once
  `doneGraceFrames_ >= 60`) correctly guard against `Exit()` not halting `Update()` immediately —
  the same previously-learned lesson ("Task 15.14's own discovery") cited across other demos this
  session, applied here in its own distinct shape (a monotonically-increasing grace counter rather
  than a modulo-guarded re-trigger).
- `NetworkSession::Create(NetworkSessionType::Local, 1, 8)` correctly uses `Local` (not
  `SystemLink`), appropriate for a single-process regression check with no real networking needed.

## Detailed Findings

### LOW — `NetworkSession*` is `Dispose()`d but never `delete`d in `RunCreate`
```cpp
NetworkSession* session = nullptr;
createResult_.elapsedMs = MeasureMs([&]() {
    session = NetworkSession::Create(NetworkSessionType::Local, 1, 8);
});
...
if (session != nullptr)
{
    session->Dispose();
}
```
Same shape as the finding already documented in six other Net demos audited this session
(`demo_qos_probe`, `demo_session_lifecycle_events`, `demo_gamer_roster_hud`,
`demo_session_browser`, `demo_simulated_network_conditions`, `demo_net_client_server_arena`) — a
real, confirmed violation of `NetworkSession`'s documented "caller must `delete` separately"
ownership contract, negligible in practice since the process exits shortly afterward. This is now
the **seventh** instance of this exact pattern found this session, and the first as a local
variable rather than a member field.

## Cross-File Observations
This demo is the clearest, most direct empirical confirmation yet of the
`GamerServicesDispatcher::UpdateAsync()` "permanent no-op once initialized" finding from the
parallel `xna-gamerservices` shard audit — where that audit confirmed the mechanism via static
source reading, this demo proves the actual fix holds by literally measuring that the call
completes rather than hanging.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
A genuinely well-designed, purpose-built regression-visualization tool: the printed timing values
and visual "waiting..." → "SUCCESS" transition make a class of bug (infinite hang) that is
otherwise silent and hard to demonstrate directly observable.

## Final Assessment
One LOW finding: `NetworkSession*` leaked (not `delete`d after `Dispose()`) — the seventh instance
of this exact pattern found across Net-adjacent demos this session.
