# Audit: examples/demo_qos_probe/src/Main.cpp

## Metadata
- Source file: `examples/demo_qos_probe/src/Main.cpp` (151 lines)
- Audit status: AUDITED
- Subsystem: `examples-demo_qos_probe` shard
- File type: standalone two-process (`--host`/`--join`) console demo executable (Task 15.3)
- XNA/FNA relevance: exercises `Microsoft::Xna::Framework::Net::NetworkSession`,
  `NetworkGamer::RoundtripTime`, and `QualityOfService`
- Related production code: `NetworkSession.hpp`/`.cpp`, `QualityOfService.hpp`/`.cpp`,
  `AvailableNetworkSession.hpp`/`.cpp` (all already audited this session as part of the `xna-net`
  shard)

## Purpose
A real two-process ENet demo (host + client) printing a live-refreshing quality-of-service view:
the host prints a real, continuously-updating `NetworkGamer::RoundtripTime` per connected remote
gamer; the client prints a one-shot `QualityOfService` sample from `NetworkSession::Find()`'s
discovery reply.

## Executive Verdict
Correct, and unusually honest about a real, asymmetric scope limitation rather than presenting a
misleadingly-symmetric demo. The file's own top comment explicitly states the client side has "only
a one-shot QualityOfService sample," not a live value, and cites the specific architectural reason
(a star-topology client has no direct `ENetPeer` to read a live RTT from) rather than silently
showing a fabricated or frozen number. The client-side per-iteration output explicitly labels this
as "not tracked from the client side (Task 4.1 documented gap)" instead of printing a misleading
unchanging value with no explanation.

## Checklist Results
- Uses the standard `GamerServicesDispatcher::Initialize(services)` + `Gamer::getSignedInGamersProperty()`
  bootstrap pattern consistent with other demos in this family — confirms (independently of the
  parallel `xna-gamerservices` fork's own audit) that this initialization path is real and expected
  usage, not a workaround.
- The client's discovery-retry loop (lines 96-100, up to 100 attempts at 50ms) is bounded, not an
  infinite spin — exits cleanly with a diagnostic message if no host is found.
- `NetworkSessionProperties{}` (default-constructed, empty) is correctly passed to `Find()` as the
  search-properties filter, matching this file's own scope (no custom session properties needed for
  this demo).

## Detailed Findings
None.

## Cross-File Observations
This demo is effectively a working integration test for two claims already independently confirmed
while auditing `NetworkSession.hpp` in the parallel `xna-net`/`xna-gamerservices` work this
session: (1) `NetworkGamer::RoundtripTime` has a real `SetRoundtripTime()` internal-wiring setter
(NOXNA, not part of real XNA's public surface) fed from `ENetBackend`'s own native RTT tracking —
this demo's host-side printout is a real consumer of that value, not a stub; (2) `QualityOfService::CreateInternal(TimeSpan)`'s
measured overload (Task 4.2) is genuinely reachable via `NetworkSession::Find()`'s discovery
path — this demo's client-side one-shot printout is a real consumer of that overload.

## Missing or Weak Tests
This is a manual/interactive two-process demo (no automated pass/fail accounting), unlike
`demo_packet_roundtrip`'s self-verifying design — reasonable for a live-network scenario, but means
it likely isn't directly CI-automatable without external orchestration (not verified in this pass
whether such orchestration exists, e.g. `net_two_process_harness` referenced in this file's own
comment).

## Positive Findings
The deliberate, well-explained host/client asymmetry is a strong example of a demo accurately
reflecting a real, documented production scope boundary rather than hiding or glossing over it.

## Final Assessment
No findings.
