# Audit: src/CNA/Internal/Net/ENetBackend.cpp

## Metadata
- Source file: `src/CNA/Internal/Net/ENetBackend.cpp`
- Audit status: AUDITED (full read, 1265 lines)
- Subsystem: `cna-internal-core` shard (Net)
- File type: C++ implementation
- XNA/FNA relevance: N/A -- NOXNA
- Main related tests: not independently located in this pass (referenced by name in comments:
  `ENetBackendTests.cpp`, `SystemLinkSessionFixture`)
- Prior dedicated audit history: this file carries the densest prior-remediation commentary of any file in
  this entire repo-wide audit -- `audit_net.md` "2026-07-18" and "third round" fixes are cited at more than a
  dozen distinct sites (pre-handshake AppData queueing, wire-id churn/reuse, remote-gamer ownership, resource
  teardown ordering, host-migration state reset, drop-counter completeness). This pass is an independent
  re-review on top of that history and found one new, significant finding not covered by any existing
  comment.

## Purpose
Implements the full SystemLink transport: per-session `SessionState` (wire-id maps, owned remote gamers,
pending-delivery/pending-pre-handshake queues), the ClientHello/ServerWelcome/GamerJoinBroadcast/
GamerLeaveBroadcast/StateChangeBroadcast/AppData message handlers, host migration, and the public
`ENetBackend` facade methods.

## Executive Verdict
Needs attention -- one HIGH-severity finding: **the message dispatcher (`HandleReceive`) does not verify
that a broadcast-only message actually came from this session's authoritative host peer**, so any connected
client can forge `ServerWelcome`/`GamerJoinBroadcast`/`GamerLeaveBroadcast`/`StateChangeBroadcast` directly
to the host. Everything else re-reviewed in this pass -- resource lifetime, the extensive pre-handshake/
migration/wire-id bookkeeping, and the EAGAIN-equivalent event-pump loop -- holds up; this subsystem's own
`audit_net.md` history has already fixed a large number of real bugs, and none of that prior work overlaps
with this finding, which is a different class of issue (authorization, not lifecycle/bookkeeping).

## Checklist Results

### HIGH: no sender-role/authorization check on host-broadcast-only message types
`HandleReceive()` (lines 927-970) dispatches by `MessageTag` alone:

```
case MessageTag::ServerWelcome:
    HandleServerWelcome(session, state, NetPacketCodec::DecodeServerWelcome(data));
    break;
case MessageTag::GamerJoinBroadcast:
    HandleGamerJoinBroadcast(session, state, NetPacketCodec::DecodeGamerJoinBroadcast(data));
    break;
case MessageTag::GamerLeaveBroadcast:
    HandleGamerLeaveBroadcast(session, state, NetPacketCodec::DecodeGamerLeaveBroadcast(data));
    break;
case MessageTag::StateChangeBroadcast:
    HandleStateChangeBroadcast(session, state, NetPacketCodec::DecodeStateChangeBroadcast(data));
    break;
```

Note that **`peer` is not passed** to any of these four handlers -- only `HandleClientHello`, `HandleConnect`,
`HandleDisconnect`, and `HandleAppData` receive the sending/relevant peer at all. By this protocol's own
design (documented throughout this file and `NetPacketCodec.hpp`), `ServerWelcome`/`GamerJoinBroadcast`/
`GamerLeaveBroadcast`/`StateChangeBroadcast` are meant to be sent **only by the session's host**, to its
connected clients -- but `HandleReceive()` is invoked identically for every packet arriving on **every**
connected `ENetPeer`, whether that peer is `state.HostPeer` (this session's own upstream, if it's a client)
or one of potentially several client peers connected *to* this session (if it's the host). Nothing prevents
a modified/malicious client -- already a legitimately-connected peer, needing no MITM or spoofing, just a
custom ENet client speaking this fully-inferable wire format -- from sending any of these four message types
directly to the host:

- **Forged `GamerLeaveBroadcastMessage`**: `HandleGamerLeaveBroadcast()` (lines 628-648) calls
  `session->RemoveGamer(gamer, ...)` for *every* `WireId` in the attacker-supplied list, with no check that
  the wire ids named belong to the sending peer at all -- a malicious client can kick **any other connected
  gamer** (including gamers belonging to entirely different clients) off the host's session.
- **Forged `GamerJoinBroadcastMessage`/`ServerWelcomeMessage`**: `HandleGamerJoinBroadcast()` (lines
  492-516) and `HandleServerWelcome()` (lines 434-490) both `new NetworkGamer(...)` for every roster entry in
  the attacker-supplied message and call `session->AddRemoteGamer(gamer)` -- a malicious client can inject
  arbitrary fake gamers (arbitrary gamertags) directly into the **host's own** roster. `HandleServerWelcome`
  additionally reassigns the *host's own local gamers'* wire ids from the attacker-supplied
  `AssignedWireIds` (lines 437-445), corrupting the host's own routing state.
- **Forged `StateChangeBroadcastMessage`**: `HandleStateChangeBroadcast()` (lines 518-524) raises a
  `NetworkEventType::StateChange` event with an arbitrary attacker-chosen `NetworkSessionState` -- any game
  code that reacts to this event on the host (e.g. transitioning UI, gating gameplay logic) can be forced
  into an arbitrary state by a connected client.

Under honest/unmodified clients this is unreachable (a real CNA client only ever sends `ClientHello` and
`AppData`), which is presumably why none of this subsystem's many prior `audit_net.md` remediation rounds
caught it -- those rounds were driven by confirmed-reachable bugs in the honest-client control flow
(pre-handshake races, wire-id churn, teardown ordering), not adversarial-client testing. Once a player runs
a modified client (a realistic scenario for a LAN-party-style feature that plenty of games have historically
seen abused this exact way), this is directly, trivially exploitable with no additional access beyond a
normal client connection.

**Fix shape**: pass the sending `peer` (or a `bool isFromAuthoritativeHost` derived from
`peer == state.HostPeer`, mirroring what `HandleConnect()` already does at line 913) into
`HandleServerWelcome`/`HandleGamerJoinBroadcast`/`HandleGamerLeaveBroadcast`/`HandleStateChangeBroadcast`,
and silently drop (matching this file's own established "malformed input is dropped, not thrown" posture)
any of these four message types received on a peer connection that is not this session's own `HostPeer`
(for a client role) or is not this session acting as host receiving from a peer it doesn't recognize as
itself in a position to send server-authoritative messages (for a host receiving from one of its own
clients, which should never send these types at all).

### Related, lower-severity observation: `HandleClientHello` has no per-peer resend guard
`HandleClientHello()` (lines 365-432) creates new `NetworkGamer` objects for every gamertag in the incoming
message every time it's called, with no check for whether this peer already completed its handshake. A
peer that repeatedly resends `ClientHello` (each capped at 255 gamertags by the wire format's own count
byte, per `NetPacketCodec.cpp`'s `EncodeCount`/`DecodeClientHello`) can unboundedly grow
`state.OwnedRemoteGamers`/the session's own gamer collection over time -- a resource-exhaustion concern
distinct from, but related to, the authorization gap above (both stem from the host trusting message
*content* from a connected peer without validating the peer's *role* or call cadence). Scored lower than the
main finding because it requires sustained repeated sends to have real impact, versus the single-message
forgery above.

### Everything else re-verified in this pass: correct
- **Resource teardown** (`TeardownSession`, lines 1030-1053): explicitly disconnects every known peer and
  flushes before destroying the host, matching Task 2.14's own documented fix.
- **Wire-id churn** (`AssignWireId`/`FreeWireIds`, lines 187-206): correctly reuses reclaimed ids before
  incrementing `NextWireId`, avoiding the documented 256-cumulative-join wraparound bug.
- **Pending pre-handshake queue** (`FlushPendingPreHandshakeAppData`/`PurgePendingPreHandshakeSendsFor`,
  lines 322-363): correctly bounded (`kMaxPendingPreHandshakeAppData = 64`, oldest evicted), correctly purged
  wherever a named gamer can no longer resolve (disconnect, leave-broadcast, migration reset), correctly
  counted via `droppedAppDataCount_` in every one of those paths -- independently re-traced all four call
  sites (`HandleDisconnect`, `HandleGamerLeaveBroadcast`, `AttemptHostMigration`, `SendAppData`'s own
  overflow branch) and confirmed each increments the counter exactly once per entry actually dropped.
- **Host migration** (`AttemptHostMigration`, lines 660-830): the deterministic lowest-remaining-wire-id
  tie-break is correctly computed excluding the dead host; the full wire-id-bookkeeping reset before either
  outcome (self-promoted or reconnect-elsewhere) is correctly ordered relative to `OwnedRemoteGamers.clear()`
  (dropping any dangling `PendingPreHandshakeSends` reference first, matching the comment's own reasoning).
  The direct `state.Host.Connect(...)` call bypassing the public `ConnectToHost()` wrapper (to avoid
  `Sessions()[session]`'s Emscripten-path replacement invalidating the `state` reference this very function
  is still using) is a subtle but correctly-reasoned deliberate choice, explicitly documented as such.
- **`ReceivedPacketGuard`** (lines 972-987): correctly guarantees `enet_packet_destroy` runs even if
  `HandleReceive`'s own try/catch somehow still let an exception escape -- genuine defense-in-depth, not
  redundant given the two failure modes (an exception inside the try, vs. one from code outside it) are
  different failure surfaces.
- **`SimulatedLatency`/`SimulatedPacketLoss`** (`ShouldDropForSimulatedLoss`, `ReleaseDuePendingDeliveries`,
  lines 80-92, 600-626): correctly scoped to local-delivery AppData only (not session-management traffic,
  not a relay pass-through), correctly deterministic at the documented 0.0/1.0 extremes without touching the
  RNG.

## Detailed Findings

1. **[HIGH] No sender-role/authorization check on host-broadcast-only message types** -- any connected
   client can forge `ServerWelcome`/`GamerJoinBroadcast`/`GamerLeaveBroadcast`/`StateChangeBroadcast`
   directly to the host, enabling arbitrary-gamer-kick, fake-gamer injection, and forced state-change events.
   `HandleReceive()`, lines 927-970, and the four handlers it dispatches to without a peer/role check.

2. **[LOW-MEDIUM] No per-peer resend guard on `ClientHello`** -- a peer can unboundedly grow the host's
   owned-remote-gamer count via repeated resends. `HandleClientHello()`, lines 365-432.

## Cross-File Observations
This finding is orthogonal to every one of this subsystem's own extensively-documented `audit_net.md` fixes
-- those addressed lifecycle/bookkeeping correctness under honest-client conditions; this is an
authorization gap that only manifests under an adversarial/modified client, a threat model the prior rounds'
own citations (reachability via `ENetBackendTests.cpp`'s public-API test) don't appear to have covered.

## Missing or Weak Tests
No test located exercising a peer sending a message type outside its expected role (e.g. a connected client
peer sending `GamerLeaveBroadcastMessage`/`ServerWelcomeMessage` to the host) -- exactly the test that would
demonstrate finding #1 directly.

## Positive Findings
The sheer density and specificity of this file's own prior-remediation history (a dozen-plus distinct,
dated, task-ID-cited fixes) reflects a subsystem that has already absorbed an unusual amount of real audit
attention; the host-migration tie-break logic and pending-queue bookkeeping are both intricate and, on
independent re-verification, correct.

## Final Assessment
One HIGH-severity finding: no sender-authorization check on host-only broadcast message types, letting any
connected client forge session-management messages to the host. One LOW-MEDIUM finding: no resend guard on
`ClientHello` allows unbounded fake-gamer injection via repeated sends. Both are new findings from this
pass, distinct from this subsystem's already-extensive `audit_net.md` remediation history.
