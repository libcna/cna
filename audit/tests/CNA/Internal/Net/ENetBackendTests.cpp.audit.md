# Audit: tests/CNA/Internal/Net/ENetBackendTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Net/ENetBackendTests.cpp` (2083 lines — read in full)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Net::ENetBackend` (backs
  `Microsoft::Xna::Framework::Net::NetworkSession`'s entire `SystemLink` real-networking
  implementation — handshake, roster sync, AppData relay, disconnect/leave, host migration,
  simulated latency/loss; CNA-internal, no direct FNA equivalent), Tasks 1.1-1.4, 2.7, 2.11, 2.14,
  3.1, 4.1-4.3, 4.6, 5.1-5.7, 5.13, 6.1-6.5, 12.1-12.3, plus 3 rounds of `audit_net.md` remediation
- Main related tests: uses `ENetHostHandle`/`NetPacketCodec` (both tested separately in this shard);
  complements `NetworkSessionTypePolicyTests.cpp`'s non-SystemLink sweep and
  `TwoProcessLoopbackTest.cpp`'s cross-process migration test

## Purpose
The single largest and most comprehensive test file in this shard: exercises the real ENet-backed
`SystemLink` implementation end-to-end — handshake (ClientHello/ServerWelcome), roster sync,
AppData relay (including host-to-third-party relay), the pending-pre-handshake-send queue's full
lifecycle (delivery, ordering, bounded eviction, purge-on-leave, purge-on-disconnect, safe disposal),
disconnect/leave handling, real host migration (self-promotion, tie-break targeting a specific
survivor), StartGame/EndGame broadcast, simulated latency/packet-loss, wire-id reclamation, prompt
disconnect-on-dispose, remote-gamer ownership/leak-freedom, and real RTT measurement.

## Executive Verdict
Exceptional — arguably the single most rigorous test file encountered in this entire audit. Nearly
every test is explicitly labeled with a task ID or an `audit_net.md remediation` round number and
documents a REAL, specific, previously-broken behavior it now guards against, several confirmed via
directly-cited production code line references or explicit contrasts with FNA's own source
behavior. The three rounds of `audit_net.md` remediation tests in particular (`AppDataQueued...`,
`MultiplePendingAppDataSends...`, `PendingAppDataQueueEvicts...`, `GamerLeaveBroadcastPurges...`,
`ClientDisconnectPurges...`, `ClientSessionEndedOnHostDisconnectDrops...`,
`HostMigrationResetDrops...`, `DisposeWithPendingAppDataInQueueIsSafe`) collectively give complete,
systematic coverage of every way the pending-pre-handshake-send queue can be resolved, bounded,
purged, or safely destroyed — a genuinely comprehensive state-machine-completeness treatment of a
non-trivial piece of internal bookkeeping.

## Checklist Results
- `AppDataQueuedBeforeSecondLocalGamerIsWiredIsDeliveredOnceResolved`'s own comment is a strong
  example of honest test-quality self-correction: it explicitly explains why it REPLACES an older
  test that "manufactured its 'not yet known' target" via a gamer that could never actually join in
  production, meaning the old test could only ever prove a drop, never a real later delivery — the
  new test instead finds a NATURALLY reachable pre-handshake gap (`AddLocalGamer` mid-session) and
  proves the full queued→delivered contract end-to-end over the real wire.
- `MultiplePendingAppDataSendsForSameSenderTargetPairDeliverInOriginalOrder` correctly proves ORDER
  preservation specifically (not just "all eventually arrive") — a materially stronger and
  previously entirely untested property per its own comment.
- `PendingAppDataQueueEvictsOldestOnceBoundIsReached` correctly proves the bounded-eviction property
  with an exact count assertion (`kMaxPendingPreHandshakeAppData` + 5 sends → exactly 5 evictions),
  not just "the queue doesn't grow forever."
- `GamerLeaveBroadcastPurgesPendingAppDataNamingTheDepartedGamer`'s comment documents this closing a
  genuinely real gap MISSED by an earlier remediation round's own queue work — a rare and valuable
  example of a test suite catching its own prior remediation's incompleteness, not just the
  original defect.
- `ClientDisconnectPurgesPendingAppDataNamingThatGamer` and
  `ClientSessionEndedOnHostDisconnectDropsAndCountsEveryPendingSend` correctly distinguish two
  genuinely different purge scopes (purging entries naming ONE specific departed gamer vs.
  discarding the ENTIRE queue on total connection loss) as separate, individually-tested code paths.
- `HostMigrationResetDropsAndCountsEveryPendingSend` correctly tests the SAME whole-queue-
  invalidation property via a THIRD, architecturally distinct code path (the host-migration reset
  block), rather than assuming coverage transfers from the other two purge tests.
- `DisposeWithPendingAppDataInQueueIsSafe`'s comment correctly notes this specific safety property
  "would show up as a real ASan use-after-free/leak under [the] ASan build if this were actually
  broken, not just this plain build" — an honest acknowledgment that this specific test's own
  detection power depends on which build variant runs it, rather than overclaiming what a plain
  (non-ASan) run alone could catch.
- `HostRelaysAppDataBetweenTwoNonLocalPeers` (Task 5.13) correctly identifies and specifically
  targets the single most complex routing branch in the production file (`HandleAppData`'s
  peer-to-peer relay, distinct from local-delivery or drop), explicitly noting no prior test in the
  file had exercised a genuine third connected party — and it also verifies the NEGATIVE case (the
  sender must NOT receive an echo of its own relayed packet), a real and easy-to-miss adjacent
  correctness property.
- `ZeroSimulatedLatencyAndPacketLossDeliverAppDataImmediately`/`SimulatedPacketLossOfOneDropsAll...`/
  `...OfZeroDropsNoAppData`/`SimulatedLatencyDelaysAppDataDeliveryUntilTheClockAdvances` (Task
  6.1-6.5) correctly use `SetClockForTesting`/`ResetClockForTesting` to make the latency case exactly
  deterministic (freeze, verify held-back, advance by EXACTLY the configured latency, verify
  released) — real determinism for a genuinely time-dependent feature, not a flaky sleep-based
  approximation, and 0.0/1.0 loss values are correctly noted as not needing RNG seeding at all.
- `ClientPromotesItselfWhenItIsTheOnlyKnownSurvivor` correctly proves the promotion is a GENUINE
  new-host transition (not just local flag-flipping) by confirming the session becomes newly
  discoverable via a real `ENetDiscoveryService::FindSessions()` call from a fresh, unregistered
  state — a strong, externally-observable proof rather than an internal-state-only assertion.
- `ClientTargetsTheLowestSurvivingWireIdInsteadOfPromotingItself`'s own comment correctly scopes
  itself as proving the TIE-BREAK MATH specifically (excluding the dead host, picking the true
  minimum remaining wire id among 3 candidates: the dead host id 0, another survivor id 2, and the
  client's own id 5) rather than a full cross-process reconnect — explicitly deferring the latter to
  `TwoProcessLoopbackTest.cpp`'s migration test, avoiding overlapping/redundant test scope between
  the two files.
- `HostRejectsClientHelloWhenPlayingAndJoinInProgressDisallowed` (Task 2.7) documents and tests a
  real, previously-silent security/correctness gap: a host already `Playing` with
  `AllowJoinInProgress == false` (the documented default) used to silently accept a mid-game join
  anyway.
- `DisconnectedPeerWireIdIsReclaimedAndReusedByTheNextJoiner` (Task 2.11) is a well-engineered
  regression test for a real wraparound-corruption bug class: rather than spinning 256+ real
  cumulative join/leave cycles to literally force wraparound (correctly noted as slow and only
  proving the failure at the very end), it directly proves the actual fix MECHANISM — 3 cycles all
  reclaim and reuse the SAME wire id rather than an ever-incrementing counter — the property that
  prevents wraparound regardless of cumulative cycle count.
- `DisposeDisconnectsConnectedPeersPromptlyInsteadOfWaitingForTimeout` (Task 2.14) correctly proves
  a peer sees a PROMPT `DISCONNECT` event within a normal polling window, not merely eventually via
  ENet's own internal timeout — a meaningfully stronger and more specific claim than "the connection
  eventually closes."
- `HostFreesOwnedRemoteGamerOnDispose` (Task 3.1) uses a dedicated testing-only accessor
  (`GetOwnedRemoteGamerCountForTesting`) to directly prove a remote gamer's ownership/lifetime
  (0→1 on join, back to 0 on dispose) — precise, unambiguous leak-freedom verification rather than
  an indirect inference.
- `HostMeasuresRealRoundtripTimeForRemoteGamer` (Task 4.1) correctly proves a previously
  permanently-dead property (`RoundtripTime` never assigned) becomes genuinely non-zero over a real
  two-peer connection — matching the "prove it's really wired to something real" pattern already
  seen and praised in `ENetDiscoveryServiceTests.cpp`'s QoS test.
- Every `Task 12.2`/`Task 12.3`/`Task 4.6` reference (`NetworkGamer::Id` being the real wire-
  negotiated id not FNA's hardcoded 0; `IsHost` correctly resolving for both local and remote
  gamers; `GamerJoined` event firing exactly once for the genuinely new join, with the pre-existing
  local gamer's own replay explicitly reset out of the counter first) shows careful, specific
  attention to exact event-count and exact-value correctness rather than approximate assertions.
- `HostSurvivesTruncatedClientHelloAndContinuesFunctioningAfterward` (Task 1.4) correctly proves BOTH
  halves of resilience: the malformed packet doesn't crash/throw out of `Update()`, AND the host
  remains fully functional for a subsequent well-formed connection — not merely "didn't crash."
- The shared `ConnectFakeClientAndCompleteHandshake` helper is reused across many of the pending-
  queue and migration tests, reducing duplication while keeping each test's own body focused on its
  specific property under test.
- The `RestoreGlobalGuard`/`DisposeGuard` RAII patterns for `Gamer::setSignedInGamersProperty` and
  session disposal are used consistently and correctly across every test that manipulates the
  process-wide signed-in-gamers registry, with each guard's own comment correctly explaining the
  double-free hazard being avoided (restoring to a FRESH empty collection, not a captured "previous"
  pointer that `setSignedInGamersProperty` itself already deleted) — directly matching this
  session's own previously-documented `feedback_global_registry_restore_double_free` finding class,
  here applied CORRECTLY as a positive counter-example.

## Detailed Findings
None — this file is exemplary throughout its full 2083 lines.

## Cross-File Observations
This file's `ClientTargetsTheLowestSurvivingWireIdInsteadOfPromotingItself` explicitly and correctly
defers full cross-process migration-reconnect coverage to `TwoProcessLoopbackTest.cpp`'s
`HostMigrationPromotesOneSurvivorAndTheOtherReconnectsAcrossRealProcesses` test — a clean, explicit
division of scope between the two files (tie-break math here; genuine cross-process reconnection
there) with no gap or unnecessary duplication. The `RestoreGlobalGuard` pattern here is identical to
and consistent with the same pattern documented in `NetworkSessionTests.cpp`'s
`DisposeFreesEveryGamerTheSessionEverOwned` (outside this shard), correctly cited as precedent.

## Missing or Weak Tests
None identified — this file's coverage of the pending-send queue lifecycle, migration tie-break
logic, and wire-id/ownership bookkeeping is unusually exhaustive.

## Positive Findings
The three-round `audit_net.md`-remediation-labeled test set for the pending-pre-handshake-send queue
is the most complete state-machine-lifecycle test coverage found anywhere in this audit (delivery,
ordering, bounded eviction, two distinct purge scopes via three distinct code paths, and safe
disposal). The deterministic clock-based latency test and the reclaimed-wire-id test are both
excellent examples of proving a real fix's underlying MECHANISM rather than merely its
symptom-level absence.

## Final Assessment
No findings. This file should be considered a reference example, alongside `VideoDecoderTests.cpp`
(also in this shard), for exceptionally rigorous, self-aware, and honestly-scoped regression-test
authorship in this project.
