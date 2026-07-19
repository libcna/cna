# Audit: examples/demo_simulated_network_conditions/src/SimGame.cpp

## Metadata
- Source file: `examples/demo_simulated_network_conditions/src/SimGame.cpp` (321 lines)
- Audit status: AUDITED
- Subsystem: `examples-demo_simulated_network_conditions` shard
- File type: standalone `Game`-subclass demo implementation (Task 15.4)
- XNA/FNA relevance: exercises `NetworkSession::setSimulatedLatencyProperty`/
  `setSimulatedPacketLossProperty`, `LocalNetworkGamer::SendData(PacketWriter&, ...)`/
  `ReceiveData(PacketReader&, ...)`
- Related production code: `NetworkSession.hpp`/`.cpp`, `LocalNetworkGamer.hpp`/`.cpp`,
  `PacketReader.hpp`/`.cpp`, `PacketWriter.hpp`/`.cpp` (all already audited this session)

## Purpose
Implements a host-authoritative Pong match over real ENet networking, using
`PacketWriter`/`PacketReader` to exchange paddle/ball positions each frame, with live-adjustable
simulated latency/packet-loss dials.

## Executive Verdict
Correct, with one LOW finding shared across multiple demos audited this session (leaked, un-
`delete`d `NetworkSession*`). The `PacketWriter::Write(Vector2)`/`PacketReader::ReadVector2()`
pairing used for position sync is correctly symmetric (unlike the deliberately-asymmetric `Color`
pairing documented elsewhere in the `xna-net` shard audit) — this demo does not encounter that
quirk since it never serializes a `Color`.

## Checklist Results
- The `while (localNetworkGamer_->getIsDataAvailableProperty()) { localNetworkGamer_->ReceiveData(reader, sender); ...}`
  loop (lines 249-261) correctly never depends on `ReceiveData(PacketReader&, NetworkGamer*&)`'s
  return value — a good defensive choice given this session's own `xna-net` shard audit confirmed
  that overload "always returns 0" (a documented, preserved FNA quirk); this demo only reads
  `sender` and the reader's contents, sidestepping the quirk entirely rather than being silently
  broken by it.
- The HUD's own comment (lines 188-192) honestly discloses that "Real measured RTT" is
  unaffected by the simulated-latency dial, since ENet's own per-peer RTT measurement happens at a
  lower transport layer than the simulated-delivery queue — an accurate, disclosed scope boundary
  consistent with `NetworkSession.hpp`'s own documented `SimulatedLatency` implementation (Task
  6.1-6.5, audited this session).
- Smoke-test auto-dial-nudge guard correctly uses `smokeFramesLeft_ > 0`.

## Detailed Findings

### LOW — `NetworkSession*` is `Dispose()`d but never `delete`d in `~SimGame()`
```cpp
SimGame::~SimGame()
{
    if (session_ != nullptr)
    {
        session_->Dispose();
        session_ = nullptr;
    }
}
```
Same shape as the finding already documented in `demo_gamer_roster_hud`/`demo_session_browser`
(and present in `demo_qos_probe`/`demo_session_lifecycle_events`) — a real, confirmed violation of
`NetworkSession`'s documented "caller must `delete` separately" ownership contract, negligible in
practice since the process exits immediately afterward. This is now the **fifth** demo in this
session sharing the identical pattern.

## Cross-File Observations
Adds a fifth confirmed instance to the cross-cutting note (added to
`AUDIT_CROSS_CUTTING_FINDINGS.md`) tracking this `Dispose()`-without-`delete` pattern across Net
demos: `demo_qos_probe`, `demo_session_lifecycle_events`, `demo_gamer_roster_hud`,
`demo_session_browser`, and now `demo_simulated_network_conditions`.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The RTT-unaffected-by-simulated-latency disclosure and the defensive non-reliance on
`ReceiveData(PacketReader&, ...)`'s known-broken return value are both genuinely careful, informed
uses of the underlying API's real, documented behavior.

## Final Assessment
One LOW finding: `NetworkSession*` leaked (not `delete`d after `Dispose()`) — the fifth instance of
this exact pattern found across Net demos this session, now clearly established as a systemic
demo-authoring habit rather than an isolated oversight.
