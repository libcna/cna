# Audit: tools/net/net_two_process_harness.cpp

## Metadata
- Source file: `tools/net/net_two_process_harness.cpp` (537 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-net` shard
- File type: standalone (non-GTest) regression-test executable, spawned twice as independent OS
  processes by `tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp`
- XNA/FNA relevance: exercises real cross-process ENet networking via `NetworkSession::Create`,
  `ENetBackend::ConnectToHost`/`GetBoundPort`/`GetSessionCountForTesting`,
  `LocalNetworkGamer::SendData`/`ReceiveData`, host migration (`HostChanged`/`SessionEnded`)
- Main related tests: `tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp` (the orchestrating GTest,
  not in this pass's scope)

## Purpose
Plays five distinct roles (`host`, `client`, `start-hosting-partial-failure`, `migration-host`,
`migration-survivor`) across genuinely independent OS processes, proving real ENet transport,
host-migration, and a specific partial-construction-failure fix all work correctly across separate
address spaces (not just within one process, which every earlier Phase 5 test already covered).

## Executive Verdict
Exceptionally well-engineered regression-test infrastructure — in particular,
`RunStartHostingPartialFailure()`'s technique for deterministically forcing a real
`ENetHostHandle::CreateHost()`-succeeds-but-`RegisterHost()`-fails partial-construction scenario
(lowering `RLIMIT_NOFILE` to exactly one more than the currently-open descriptor count) is a
genuinely clever, precisely-reasoned piece of systems-level test engineering, with the file's own
comment explicitly documenting why a naive port-conflict approach was tried first and found
unreliable.

## Checklist Results
- `RunStartHostingPartialFailure()` (lines 414-483) tests a real, specific, named defect (Task 6.3):
  `ENetBackend::StartHosting` used to `emplace()` a new session into its `Sessions()` map *before*
  calling `ENetDiscoveryService::RegisterHost()` (which can throw), leaking a permanent,
  live-but-undiscoverable ENet host on failure. The test asserts
  `GetSessionCountForTesting() == 0` both before the forced failure (sanity check) and after it
  (the real proof) — a precise, quantitative regression check for an all-or-nothing construction
  guarantee, not a loose "doesn't crash" assertion.
- `RunMigrationHost()`/`RunMigrationSurvivor()` (lines 206-375, Task 5.5) prove real 3-process host
  migration: the ordering rationale for why `SurvivorA` deterministically gets promoted over
  `SurvivorB` (the "lowest remaining wire id" rule, keyed to spawn order via a `JOINED` handshake
  line) is precisely explained and, per the comment, deliberately not left as a race the
  orchestrating test has to tolerate.
- `PumpUntil()` (lines 64-74) is a clean, reusable poll-with-deadline helper used consistently
  across every role — no duplicated polling-loop logic.
- Every role function's `session->Dispose()` call sites (RunHost, RunClient, RunMigrationHost,
  RunMigrationSurvivor, RunStartHostingPartialFailure — roughly a dozen call sites total across the
  file) never follow with `delete session`/`delete retry`/`delete neverConstructed` — see Detailed
  Findings.

## Detailed Findings

### LOW — `NetworkSession*` is `Dispose()`d but never `delete`d, at every exit path in every role function
Consistent with the identical finding in the sibling `tools/net/gamerservices_dispatcher_harness.cpp`
audited alongside this file — a real, confirmed, repeated violation of `NetworkSession`'s
documented "caller must `delete` separately" ownership contract, negligible in practice since this
is a short-lived process that exits immediately after each role completes. This file has the most
call sites of any single file found sharing this pattern so far this session (RunHost: 4 sites;
RunClient: 3 sites; RunMigrationHost: 2 sites; RunMigrationSurvivor: 8 sites;
RunStartHostingPartialFailure: 2 sites, including one `retry` local never freed even on the success
path).

## Cross-File Observations
Confirms, alongside `gamerservices_dispatcher_harness.cpp` (audited together), that the
`NetworkSession` `Dispose()`-without-`delete` pattern extends into test/tool harness code, not just
example demos — now a well-established, project-wide, repeated convention gap around this specific
class's ownership contract.

## Missing or Weak Tests
This file IS test infrastructure; not applicable.

## Positive Findings
`RunStartHostingPartialFailure()`'s `RLIMIT_NOFILE` technique is one of the most sophisticated,
carefully-reasoned pieces of deterministic-failure-injection test engineering found in this entire
audit — the comment's own account of why a port-conflict approach was tried and rejected (confirmed
empirically unreliable under real suite ordering) demonstrates genuine engineering rigor, not a
first-attempt-accepted design.

## Final Assessment
One LOW finding, repeated at roughly a dozen call sites: `NetworkSession*` leaked (not `delete`d
after `Dispose()`) — the same pattern already documented across example demos and the sibling
tool harness, now confirmed as a genuinely systemic project-wide gap around this one class's
ownership contract specifically.
