# Audit: tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp

## Metadata
- Source file: `tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp` (275 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test), spawns a real helper process
  (`tools/net/net_two_process_harness.cpp`, not itself part of this shard)
- XNA/FNA relevance: Tests `Microsoft::Xna::Framework::Net::NetworkSession`'s `SystemLink` real
  cross-process transport, host migration, and `StartHosting` rollback (Tasks 6.1, 6.3, 5.5)
- Main related tests: explicitly documented as the cross-process complement to
  `ENetBackendTests.cpp`/`ENetDiscoveryServiceTests.cpp`'s single-process loopback tests; structure
  mirrors `GamerServicesDispatcherHangRegressionTest.cpp`'s spawn+watchdog helpers

## Purpose
Yes — this file directly and substantively justifies the "needs a fresh process" pattern already
cited by `AudioMixerTests.cpp`/`GamerServicesDispatcherHangRegressionTest.cpp`. Its own header
comment explains precisely WHY: every other loopback test in this suite proves the transport works
WITHIN a single process (only one real `NetworkSession` can exist per process, per the
`activeSession_` gate), using a raw `ENetHostHandle`/socket as a stand-in for "the other machine."
This file is the first and only one to prove the same transport genuinely works across separate
address spaces — real host↔client join and data exchange, `StartHosting` partial-failure rollback,
and real 3-process host migration with real LAN rediscovery.

## Executive Verdict
Excellent, and the single most rigorous "genuinely needs multiple processes" justification found in
this shard. Unlike `GamerServicesDispatcherHangRegressionTest.cpp` (which needs process isolation
because of a process-lifetime static hazard), this file needs it because the property under test
— TWO real, independent `NetworkSession`-holding endpoints connected over a genuine socket — is
architecturally impossible to construct within one process at all, given the documented
one-real-session-per-process constraint.

## Checklist Results
- `HostAndClientJoinAndExchangeAppDataAcrossRealProcesses` (Task 6.1) correctly hands the host's
  real bound port to the client OUT-OF-BAND via a stdout pipe, with the file's own comment
  explicitly justifying why `ENetDiscoveryService` itself is deliberately NOT used for this
  handshake (two independent processes both binding that service's shared well-known port would be
  fragile) — and correctly notes that mechanism is separately validated at the single-process level
  elsewhere (Task 5.8, i.e. `ENetDiscoveryServiceTests.cpp`). This is a well-reasoned choice to keep
  this test's own moving parts minimal and focused on the actual property under test.
- `StartHostingRollsBackCleanlyOnDiscoveryRegistrationFailure` (Task 6.3) documents and tests a
  real, confirmed-fixed defect: `ENetBackend::StartHosting` used to commit a new session into its
  map BEFORE the (possibly-throwing) discovery registration call, leaving a stale, undiscoverable,
  un-rolled-back entry on failure — and the comment correctly explains why this specific failure
  can only be forced deterministically in an isolated process (a discovery socket that has never
  yet been bound in that process).
- `HostMigrationPromotesOneSurvivorAndTheOtherReconnectsAcrossRealProcesses` (Task 5.5) is a
  genuinely impressive integration test: 3 real, independent processes, a real mid-session host
  death, a REAL cross-process `ENetDiscoveryService::FindSessions()` rediscovery (explicitly noted
  as "the one thing the host/client tests above deliberately avoid relying on cross-process" — i.e.
  this test knowingly takes on cross-process discovery risk specifically because migration
  genuinely requires it), and verification that exactly the DETERMINISTIC survivor (the one with
  the lower wire ID, per the documented "lowest remaining wire id" promotion rule) is promoted. The
  test's own comment correctly explains WHY the ordering is deterministic rather than a race
  (SurvivorA is spawned and its `JOINED` line awaited before SurvivorB is spawned, and host-side
  wire-ID assignment order is itself documented elsewhere as arrival-order-based) — this is
  genuinely careful reasoning about eliminating a race rather than tolerating or ignoring one.
- The same test's comment on its own 30-second internal timeout (vs. the simpler tests' 8 seconds)
  is refreshingly honest about a REAL observed flakiness data point: "confirmed flaky at 10s under
  heavy concurrent load (observed timing out once inside a full ~4700-test suite run, despite
  711ms/run consistently in isolation across 8 separate runs)" — this is exactly the right way to
  justify a generous timeout: grounded in an actual observed failure under real load, not a
  guessed-at margin, while explicitly noting the margin doesn't mask a genuine hang (a real bug
  still fails well before the deadline in practice).
- `WaitWithWatchdog`/`ReadLineWithDeadline`/`SpawnHarness` form solid, reusable process-supervision
  infrastructure: non-blocking `waitpid`+`SIGKILL`-on-timeout, `poll()`-based line-reading with a
  deadline (correctly handling `EINTR` retry), and pipe-based stdout/stderr capture for diagnostics
  on failure (every `EXPECT_*` failure message includes the captured subprocess output) — this last
  point is a real debuggability strength: a CI failure in this test would show WHY the subprocess
  failed, not just that it did.

## Detailed Findings
None.

## Cross-File Observations
This file's spawn+watchdog pattern structurally mirrors
`GamerServicesDispatcherHangRegressionTest.cpp`'s own helpers (explicitly cross-referenced in that
file's own comments) — consistent, reused process-isolation test infrastructure across this shard
for two DIFFERENT underlying reasons (a process-lifetime-static hazard there; a fundamental
one-session-per-process architectural constraint here) — both legitimate, well-justified uses of
the same pattern.

## Missing or Weak Tests
None identified — this file's three tests each target a distinct, well-justified cross-process
property (basic join/exchange, rollback-on-partial-failure, host migration with rediscovery).

## Positive Findings
The host-migration test's deterministic-ordering reasoning (eliminating a race via explicit
join-order sequencing rather than tolerating one) and the honestly-disclosed, load-observed timeout
justification are both exemplary pieces of test engineering for describing and taming real-world
concurrency/timing non-determinism rather than papering over it.

## Final Assessment
No findings. This file provides strong, well-justified evidence that the "needs a fresh process"
test pattern is applied appropriately and only where genuinely necessary in this shard.
