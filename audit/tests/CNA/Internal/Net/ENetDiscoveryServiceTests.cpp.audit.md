# Audit: tests/CNA/Internal/Net/ENetDiscoveryServiceTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Net/ENetDiscoveryServiceTests.cpp` (314 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Net::ENetDiscoveryService` (backs
  `Microsoft::Xna::Framework::Net::NetworkSession::Find` for `SystemLink`; CNA-internal, no direct
  FNA equivalent), Tasks 1.1-1.5, 4.2, 5.8
- Main related tests: complements `NetDiscoveryProtocolTests.cpp`'s codec-unit-level truncation
  tests (explicitly cross-referenced) and `NetworkSessionTypePolicyTests.cpp`'s timing-based
  non-SystemLink sweep

## Purpose
Tests LAN discovery end-to-end: registered-host discovery with real measured QoS, unregister
behavior, and — most significantly — a set of raw-UDP-socket-based adversarial tests simulating an
external/untrusted sender injecting malformed or mismatched-filter packets directly at the
discovery port.

## Executive Verdict
Excellent, security- and reliability-conscious test file with real cross-process-adjacent rigor.
`MalformedAnnounceDuringSearchIsIgnoredAndDoesNotLeaveADanglingResultsPointer` documents and tests a
genuinely serious, confirmed-fixed defect: an exception thrown mid-poll during `FindSessions()`
used to leave a static `currentResults_` pointer dangling at an about-to-be-destroyed stack
variable, since the cleanup reset was skipped by exception unwinding — a real use-after-scope
hazard from adversarial network input.

## Checklist Results
- `FindSessionsDiscoversRegisteredHost`'s own comment carefully documents WHY it can exercise
  `ENetDiscoveryService::FindSessions()` directly in the same process as a live host even though the
  full public `NetworkSession::Find()` API could not (the latter would throw
  `InvalidOperationException` since the host already occupies the one-session-per-process
  `activeSession_` gate) — a precise, honest statement of what this test can and cannot prove about
  the full public API surface, with the gap explicitly deferred to `TwoProcessLoopbackTest.cpp`.
  It also correctly documents the dedup-by-connect-port behavior and the non-deterministic
  broadcast-vs-loopback-unicast race, appropriately checking only "non-empty" rather than an exact
  address for the race-dependent field — avoiding a flaky over-specific assertion.
- The same test's QoS assertions (Task 4.2) meaningfully verify a REAL round-trip measurement
  actually occurred (non-zero RTT, average equals minimum for a single real sample) rather than the
  previously-hardcoded all-zero stub this comment documents replacing.
- `BuildRawAnnounceWithPropertyIndex`/`SendRawUdpDatagram` deliberately bypass the project's own
  `Encode()` path entirely and use a raw POSIX UDP socket, correctly modeling a genuinely
  independent external attacker who has no dependency on CNA's own encoder — the right threat model
  for an unauthenticated broadcast protocol.
- `MalformedAnnounceDuringSearchIsIgnoredAndDoesNotLeaveADanglingResultsPointer`'s own comment is
  unusually transparent about the test's own history: it explains the dangling-pointer bug was
  fixed via a `CurrentResultsGuard` RAII reset, RETAINED as defense-in-depth even after a SEPARATE,
  later fix (Task 1.4) stopped the exception from escaping in the first place — meaning the test no
  longer observes an exception at all, and the comment honestly narrates that evolution rather than
  leaving a stale, now-misleading description. It also correctly re-runs `FindSessions()` a SECOND
  time after the malformed packet to prove no corrupted static state survived the first call — the
  exact scenario the original fix targeted.
- `PollIgnoresMalformedAnnounceWhileIdlingAndDiscoveryKeepsWorking` correctly tests the DIFFERENT
  passive-responder code path (`Poll()` via `NetworkSession::Update()`, with no `FindSessions()` in
  flight) separately from the active-search path above — a genuinely distinct code path that a
  single malformed-packet test would not cover.
- `ReplyToQueryOnlyAnswersWhenSessionTypeFilterMatchesTheHost` (Task 1.5) is a carefully-designed
  test: it correctly notes `FindSessions()` itself can't exercise this server-side filter check
  (since it early-returns before sending anything for non-matching types), so it talks to the
  discovery port directly — and crucially includes a POSITIVE control (a matching-filter query DOES
  get a reply, on the same socket) immediately after the negative case, proving the "no reply"
  result reflects the actual filter check rather than a broken test setup that could never observe
  a reply at all.
- The Emscripten-specific conditional compilation and `EXPECT_TRUE(found.empty())` branches
  correctly document a real, permanent platform limitation (no raw UDP broadcast/unicast equivalent
  on the Web platform) rather than silently skipping.

## Detailed Findings
None.

## Cross-File Observations
This file's positive-control technique in `ReplyToQueryOnlyAnswersWhenSessionTypeFilterMatchesTheHost`
is a good example of proving a negative result is meaningful, consistent with similar
positive/negative pairing seen in `SdlInputBridgeKeyboardTests.cpp`'s Nordic-key test earlier in
this shard. The truncated-buffer/malformed-input tests here are explicitly and correctly scoped as
the system-level complement to `NetDiscoveryProtocolTests.cpp`'s codec-unit-level truncation tests.

## Missing or Weak Tests
None identified — the adversarial and cross-path coverage here is unusually thorough.

## Positive Findings
The unusually transparent, self-narrating comment on
`MalformedAnnounceDuringSearchIsIgnoredAndDoesNotLeaveADanglingResultsPointer` explaining how the
test's own meaning changed across two related fixes is an excellent example of maintaining test
documentation accuracy over time rather than letting comments go stale; the positive/negative
filter-check pairing is a strong test-design pattern.

## Final Assessment
No findings.
