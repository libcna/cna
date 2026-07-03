# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`), built on SDL3 with a pluggable 3D graphics backend layer
(EasyGL/OpenGL ES 3.2, Vulkan, Bgfx, SDL_Renderer). It is a framework/runtime — not a game —
designed so that XNA/FNA game code can be ported to C++ with minimal API-surface changes.

- **Main goal:** Full XNA 4.0 API coverage with pixel-accurate/behavior-accurate fidelity to the
  FNA reference implementation, backed by unit and (for graphics) pixel-readback integration
  tests.
- **Current development phase:** Phase 5 — the ENet networking backend. Graphics phases (1–31)
  are complete. `GamerServices` (all classes) and `Net` (5 enums + all 18 non-enum classes: the
  full public API surface) are complete and unit-tested — see `plan_net.md`/Task history below.
  Phase 5 is now making that already-ported `Net` API actually do real networking, since FNA's
  own `Net` source (`FNA.NetStub`) never had a working implementation to port from — it is a
  non-functional stub (every gamer named "Stub Gamer", `Find()` always empty, etc.). **Phase 5 is
  therefore original design work, not line-by-line FNA-fidelity porting.** A detailed, approved
  design plan for all of Phase 5 exists at
  `/home/robertvokac/.claude/plans/scalable-swimming-feigenbaum.md` — read it before continuing
  this phase; it is the source of truth for the wire protocol, architecture decisions, and
  9-sub-task breakdown (5.1–5.9) referenced throughout this file.
- **Important architectural decisions:**
  - Graphics backend selection is compile-time via `CNA_GRAPHICS_BACKEND`.
  - `CNA_GamerServices` and `CNA_Net` are separate CMake static libraries; excluded from the main
    `CNA` GLOB so they don't contaminate the graphics-only build.
  - GamerServices/Net/Avatar are **not** binary-compatible with the original Xbox Live SDK — they
    are a reimplementation of the XNA API shape, backed by ENet (reliable UDP) for real networking
    instead of Xbox Live.
  - Phase 5's ENet backend uses **no abstract `INetworkBackend` interface** (unlike the graphics
    backends, which have 4 real swappable implementations) — ENet is the only networking
    implementation planned, so a virtual interface would be speculative abstraction. Instead: a
    static-class facade `CNA::Internal::Net::ENetBackend`, keeping ENet's C API entirely out of
    `Microsoft::Xna::Framework::Net` public headers.
  - `sharp-runtime` (sibling repo, `../sharp-runtime/`) provides all `System.*` types. Only **new**
    files are added there; existing files must not be modified without strong justification —
    another Claude Code instance works on it in parallel with no version pin from this repo (see
    section 4/5 for what happened the one time this repo needed an exception).

---

## 2. Current status

### Build
- Last verified clean build + full test run: **2069 / 2069 unit tests passing** (Task 5.8
  complete), verified stable under `--gtest_shuffle --gtest_repeat=5` (full suite) and
  `--gtest_repeat=15` (discovery-specific tests, given their real-socket timing).
- Full suite runtime jumped from ~30ms to ~1.1s after this task — expected and correct: `Find()`
  on `SystemLink` now genuinely blocks for a real (150ms) network discovery window, and the
  pre-existing `NetworkSessionTest.FindReturnsEmptyCollection` test calls it twice.
- `GamerServices` namespace: **complete**, all classes ported and tested.
- `Net` namespace: **complete** API surface — 5 enums + all 18 non-enum classes (enums →
  exceptions → data structs → event args → `NetworkSessionProperties`/`QualityOfService`/
  `AvailableNetworkSession(Collection)` → `PacketReader`/`PacketWriter` → `NetworkGamer`/
  `NetworkMachine` → `NetworkSession`/`LocalNetworkGamer`). All ported and unit-tested as of Task
  4.7.
- Phase 5 (ENet backend): **Task 5.1 complete** (ENet lifecycle + host/peer RAII wrapper, tested
  with a real loopback UDP smoke test). **Task 5.2 complete** (`NetPacketCodec` — all 6
  connected-channel wire messages encode/decode via `PacketWriter`/`PacketReader`, plus
  `SendDataOptions`→ENet-flags mapping; `NetDiscoveryProtocol` — LAN discovery query/announce
  codec including sparse `NetworkSessionProperties` encoding — both with full round-trip test
  coverage). **Task 5.3 complete** (`ENetBackend` static facade + registry; `NetworkSession` now
  starts a real ENet host for `SystemLink` sessions and pumps it every `Update()`; closed the
  three real gaps found during planning — see section 5). **Task 5.4 complete** (real
  `ConnectToHost` + `ClientHello`/`ServerWelcome`/`GamerJoinBroadcast` handshake; rosters converge
  over a real loopback UDP connection). **Task 5.5 complete** (`AppData` relay: real
  `SendData`/`ReceiveData` over the ENet transport, with host relay for gamers it doesn't own).
  **Task 5.6 complete** (real ENet disconnect handling: host removes/broadcasts departed clients,
  clients raise `SessionEnded` on losing their host). **Task 5.7 complete** (`StartGame`/`EndGame`
  now broadcast a `StateChangeBroadcastMessage` to every connected peer, so `GameStarted`/
  `GameEnded` fire and `SessionState` transitions consistently on all machines). **Task 5.8
  complete** (`Find`/`BeginFind`/`EndFind` now genuinely discover hosted `SystemLink` sessions over
  a real raw-UDP LAN discovery protocol). **Phase 5's backend work is now functionally complete —
  only Task 5.9 (final `NetworkSessionType` policy regression pass) remains.**

### What works
- The entire graphics stack (Phases 1–31).
- The entire `GamerServices`/`Net` synthetic (non-networked) API surface for every
  `NetworkSessionType` except `SystemLink` — every class, property, method, and static factory
  family that existed before Phase 5 still works exactly as before; verified via the new
  `LocalSessionTypeDoesNotStartRealNetworking`/`StartHostingIsNoOpForNonSystemLinkTypes`/
  `ConnectToHostIsNoOpForNonSystemLinkTypes` tests.
- `CNA::Internal::Net::ENetLibrary`/`ENetHostHandle` (Task 5.1): real ENet host creation
  (ephemeral or fixed port), real loopback UDP connect + packet exchange — proven by a passing
  automated test (`ENetHostHandleTests.cpp`), not just a design claim.
- `CNA::Internal::Net::NetPacketCodec`/`NetDiscoveryProtocol` (Task 5.2): all wire messages
  encode and round-trip decode correctly, including sparse `NetworkSessionProperties`.
- `CNA::Internal::Net::ENetBackend` (Task 5.3): `NetworkSession::Create(...)` with
  `NetworkSessionType::SystemLink` now binds a real ENet host on an OS-assigned ephemeral UDP
  port; `Update()` drains that host's ENet events every call; `Dispose()` tears the host down and
  unregisters it. `NetworkSession::AddRemoteGamer()`/`RemoveGamer()` correctly mutate
  `AllGamers`/`RemoteGamers`/`PreviousGamers` and raise `GamerJoined`/`GamerLeft`/`SessionEnded`.
- `CNA::Internal::Net::ENetBackend::ConnectToHost` (Task 5.4): a real `NetworkSession` can now
  connect over real loopback UDP to another real ENet host and complete the full
  `ClientHello`→`ServerWelcome`→`GamerJoinBroadcast` handshake — wire-ids are host-assigned and
  tracked in a per-session `SessionState` (peer↔wire-id maps), rosters converge on both sides, and
  `GamerJoined` fires with the correct gamertag. Proven by two real-socket loopback tests in
  `ENetBackendTests.cpp` (`HostRespondsToClientHelloWithServerWelcomeAndAddsRemoteGamer`/
  `ClientSendsClientHelloAndProcessesServerWelcome`), each using exactly one real `NetworkSession`
  paired with a raw `ENetHostHandle` standing in for "the other machine" — see section 4/5 for why
  two real `NetworkSession`s can never coexist in one test process.
- `SendData`/`ReceiveData` (Task 5.5): a real `LocalNetworkGamer::SendData` call now actually
  transmits an `AppDataMessage` over the ENet transport (via `ENetBackend::SendAppData`) to a
  remote target, with the host relaying on to the actual owning peer when it isn't itself that
  gamer's connection (`SessionState::WireIdToPeer`); a remote sender's `AppData` arriving at
  either side gets delivered into the right `LocalNetworkGamer::packetQueue_`
  (`LocalNetworkGamer::EnqueuePacket`), so `ReceiveData` returns real payload bytes and correctly
  identifies the sender. Proven by two more real-socket loopback tests in `ENetBackendTests.cpp`
  (`HostDeliversAppDataFromRemoteGamerIntoLocalPacketQueue`/`ClientSendDataTransmitsAppDataToHost`).
  Gated behind `RealNetworkingEnabled(sessionType_)`, so non-`SystemLink` sessions keep `PacketSend`
  as a complete no-op, byte-for-byte matching pre-Phase-5 behavior.
- Disconnect/leave (Task 5.6): a real ENet `DISCONNECT` event is now handled on both roles. Host
  side: removes every gamer the departed peer owned via the already-shipped
  `NetworkSession::RemoveGamer` (raises `GamerLeft`, migrates to `PreviousGamers`), then broadcasts
  a `GamerLeaveBroadcastMessage` to the other connected peers so their rosters converge too, and
  cleans up `SessionState`'s per-peer bookkeeping. Client side: losing its `HostPeer` connection
  calls `RemoveGamer` on one of its own local gamers with `NetworkSessionEndReason::HostEndedSession`
  (raises `SessionEnded`, transitions `SessionState` to `Ended`) — per `RemoveGamer`'s Task-5.3
  design, this is a session-wide signal regardless of which local gamer is passed. Proven by 3 more
  real-socket loopback tests in `ENetBackendTests.cpp`.
- `StartGame`/`EndGame` state broadcast (Task 5.7): calling `StartGame()`/`EndGame()` on the
  session that's actually playing the ENet-transport host role (never called `ConnectToHost`, i.e.
  `SessionState::HostPeer == nullptr`) now also broadcasts a `StateChangeBroadcastMessage` to every
  connected peer via new `ENetBackend::BroadcastStateChange`; each peer's `HandleStateChangeBroadcast`
  queues a local `StateChange` event so its own `Update()` raises `GameStarted`/`GameEnded` and
  transitions `SessionState` identically to the host. A session that's an ENet-transport *client*
  calling `StartGame`/`EndGame` (possible only because `NetworkSession::getIsHostProperty()` is an
  unconditional-`true` FNA-preserved stub quirk — every `NetworkGamer::getIsHostProperty()` always
  returns `true` — not something Phase 5 should "fix") still transitions its own local state but
  does **not** broadcast, since only the actual ENet host's state changes are meaningful to relay
  in this star topology. Proven by 2 more real-socket loopback tests in `ENetBackendTests.cpp`.
- LAN discovery (Task 5.8): new `CNA::Internal::Net::ENetDiscoveryService` owns one process-wide
  raw UDP socket (`ENET_SOCKET_TYPE_DATAGRAM`, well-known port 61190, `ENET_SOCKOPT_BROADCAST` +
  `ENET_SOCKOPT_REUSEADDR`). Hosting a `SystemLink` session registers with it
  (`ENetBackend::StartHosting`/`TeardownSession`); `NetworkSession::Update()` calls
  `ENetDiscoveryService::Poll()` (non-blocking) so a registered host keeps answering queries for as
  long as it's alive. `EndFind` now calls `ENetDiscoveryService::FindSessions(type)` for
  `SystemLink` — this broadcasts a `DiscoveryQueryMessage` (plus an explicit unicast copy to
  `127.0.0.1`) and blocks for a real, fixed 150ms window collecting `DiscoveryAnnounceMessage`
  replies, deduped by connect port (see below for why dedup turned out to be load-bearing, not
  optional). `AvailableNetworkSession` gained `hostAddress_`/`hostPort_` + `NOXNA
  GetConnectAddress()`/`GetConnectPort()` accessors (2 new trailing defaulted params on
  `CreateInternal`/ctor, existing call sites unaffected; `operator==` now also compares them).
  Proven by 5 new tests in `ENetDiscoveryServiceTests.cpp` plus 3 new `AvailableNetworkSessionTest`
  cases. Non-`SystemLink` `Find()` stays fully synthetic/empty and returns immediately (no socket
  I/O at all), matching the `NetworkSessionType` policy.

### What does not work yet
- A local gamer added at runtime via `AddLocalGamer()` **after** a session already started
  hosting/connecting does not get a wire-id assigned or announced to already-connected peers —
  only gamers present at `Create()` time are covered. Not yet needed by any task's stated scope;
  flagged here so a future task doesn't assume it already works.
- Host migration, `SimulatedLatency`/`SimulatedPacketLoss`, and cross-machine `IsReady` sync remain
  unimplemented/inert, as documented in the approved plan's "explicitly untestable" list.
- `NetworkSession::Find()`'s full public API path (as opposed to `ENetDiscoveryService::
  FindSessions()` directly) cannot be end-to-end tested with a real hosted session alive in this
  same process — see section 4/5's new entry on this.

---

## 3. Recent changes

This session's commits, newest first (all on branch `feature/net`, pushed to
`origin/feature/net`):

| Commit | Files | Change |
|---|---|---|
| (uncommitted at time of writing) | `NetDiscoveryProtocol.hpp/.cpp` (extended); `ENetDiscoveryService.hpp/.cpp` (new); `AvailableNetworkSession.hpp/.cpp` (extended); `ENetBackend.cpp`, `NetworkSession.cpp` (extended); `NetDiscoveryProtocolTests.cpp`, `AvailableNetworkSessionTests.cpp` (extended), `ENetDiscoveryServiceTests.cpp` (new) | Task 5.8 complete: real LAN discovery. Found and fixed a real gap in Task 5.2's design first — `DiscoveryQueryMessage`/`DiscoveryAnnounceMessage` shared no leading tag byte, but both now travel over the same raw UDP socket, so added `DiscoveryMessageTag`/`PeekTag` (mirrors `NetPacketCodec`'s existing pattern) before either could be decoded on receipt. New `ENetDiscoveryService`: one process-wide raw UDP socket (well-known port 61190, `SOCKOPT_BROADCAST`+`REUSEADDR`+`NONBLOCK`), `RegisterHost`/`UnregisterHost` (wired into `ENetBackend::StartHosting`/`TeardownSession`), `Poll()` (wired into `NetworkSession::Update()`, non-blocking, answers incoming queries), `FindSessions(type)` (wired into `NetworkSession::EndFind`, blocks a real fixed 150ms window). Found and fixed a real duplicate-result bug during testing: broadcasting a query AND explicitly unicasting a loopback copy means the same host's reply can arrive twice on one machine (via different observed source addresses) — fixed by deduping collected results by connect port, exactly the correlation key the plan's own design anticipated needing. 8 new tests, 2069/2069 total passing, stable under `--gtest_shuffle --gtest_repeat=5` and `--gtest_repeat=15` on the discovery-specific tests. |
| `2809c2b` | `ENetBackend.hpp/.cpp` (extended); `NetworkSession.cpp` (`StartGame`/`EndGame`); `ENetBackendTests.cpp` (extended) | Task 5.7 complete: new `ENetBackend::BroadcastStateChange(session, newState)` — called from `NetworkSession::StartGame()`/`EndGame()` after their existing local `StateChange` enqueue, gated behind `RealNetworkingEnabled(sessionType_)` and (inside `BroadcastStateChange` itself) `SessionState::HostPeer == nullptr` (only the actual ENet-transport host broadcasts; a session that's a transport client — reachable only via the pre-existing, unconditional-`true` `getIsHostProperty()` FNA-preserved stub quirk — transitions its own state locally but doesn't broadcast). New `HandleStateChangeBroadcast` (`ENetBackend.cpp`, the last remaining `default:` case in `HandleReceive`'s `switch`) queues a local `StateChange` event on the receiving side so `GameStarted`/`GameEnded`/`SessionState` stay consistent everywhere. 2 new loopback tests. 2060/2060 total passing, stable under `--gtest_shuffle --gtest_repeat=8` and `--gtest_repeat=20` on the ENet-specific tests. |
| `ea2e040` | `ENetBackend.cpp` (extended); `ENetBackendTests.cpp` (extended) | Task 5.6 complete: real ENet `ENET_EVENT_TYPE_DISCONNECT` handling in `PumpSession`'s event loop (`HandleDisconnect`). Host role: removes every gamer the departed peer owned via `NetworkSession::RemoveGamer` (already existed, Task 5.3), broadcasts a `GamerLeaveBroadcastMessage` to the remaining peers, cleans up `SessionState`'s `PeerWireIds`/`WireIdToPeer`/`WireIdToGamer`/`GamerToWireId` entries for that peer. Client role: losing `HostPeer` calls `RemoveGamer` on one local gamer with `NetworkSessionEndReason::HostEndedSession`, raising `SessionEnded`. New `HandleGamerLeaveBroadcast` (mirrors `HandleGamerJoinBroadcast`'s shape) lets clients process the broadcast and remove their own proxy of the departed gamer. 3 new loopback tests. 2058/2058 total passing, stable under `--gtest_shuffle --gtest_repeat=8` and `--gtest_repeat=20` on the ENet-specific tests. |
| `89dfbc0` | `LocalNetworkGamer.hpp/.cpp` (extended); `ENetBackend.hpp/.cpp` (extended); `NetworkSession.cpp` (`Update()`'s `PacketSend` branch); `ENetBackendTests.cpp`, `NetworkSessionTests.cpp` (extended) | Task 5.5 complete: `LocalNetworkGamer::EnqueuePacket` + every `SendData` overload now sets `NetworkEvent::Sender = this`. `NetworkSession::Update()`'s previously-empty `PacketSend` branch (gated behind `RealNetworkingEnabled(sessionType_)`, so non-`SystemLink` types stay byte-for-byte unchanged) now delivers local-target packets directly into the target's `packetQueue_` (remapping `.Gamer` to the sender, per the Task 5.3-documented field-collision fix) or transmits remote-target packets via new `ENetBackend::SendAppData`. `ENetBackend`'s `SessionState` gained `WireIdToPeer` (host-only) for relay routing; `HandleReceive` gained a `MessageTag::AppData` case (`HandleAppData`) that delivers to a local target or relays to the owning peer. 2 new loopback tests reusing the Task 5.4 one-real-session-plus-raw-`ENetHostHandle` pattern. 2055/2055 total passing, stable under `--gtest_shuffle --gtest_repeat=8`. |
| `b0fb10d` | `ENetBackend.hpp/.cpp` (extended); `ENetBackendTests.cpp` (extended); plan file corrected | Task 5.4 complete: `ENetBackend::ConnectToHost` + full `ClientHello`/`ServerWelcome`/`GamerJoinBroadcast` handshake over real loopback UDP. `SessionState` gained `NextWireId`/`HostPeer`/`GamerToWireId`/`WireIdToGamer`/`PeerWireIds`. **Found and fixed a real testing-strategy error in the approved plan**: `NetworkSession::BeginCreate` gates on a single process-wide `activeSession_`, so two real `NetworkSession`s can never coexist in one test process (ephemeral ports don't change this, contrary to the plan's original assumption) — an early test draft proved this by throwing and stranding `activeSession_` for the rest of the suite (42 tests failed) until fixed. Corrected testing pattern (one real `NetworkSession` + a raw `ENetHostHandle` standing in for "the other machine", RAII-`Dispose()`-guarded fixture) is now documented in the plan file and used for 2 new loopback handshake tests, reusable for Tasks 5.5–5.8. 3 new tests. 2053/2053 total passing, stable under `--gtest_shuffle --gtest_repeat=8` and `--gtest_repeat=20` on the ENet-specific tests. |
| `8fcd5e1` | `ENetBackend.hpp` (new)/`.cpp` (implemented); `NetworkSession.hpp/.cpp`, `NetworkGamer.hpp/.cpp`, `GamerCollection.hpp` (extended); `NetworkSessionTests.cpp`, `NetworkGamerMachineTests.cpp` (extended), `ENetBackendTests.cpp` (new) | Task 5.3 complete: `ENetBackend` static facade (`RealNetworkingEnabled`/`StartHosting`/`TeardownSession`/`PumpSession`/`GetBoundPort`) wired into `NetworkSession`'s constructor tail/`Dispose`/`Update` — `SystemLink` sessions now bind a real ephemeral-port ENet host; every other type is unaffected (verified via regression tests). Added `NetworkSession::AddRemoteGamer`/`RemoveGamer`, `NetworkEvent::Sender`, `GamerCollection<T>::Remove`, `NetworkGamer::SetHasLeftSession`, and a gamertag ctor param on `NetworkGamer::CreateInternal` (defaulted to "Stub Gamer", so local gamers are unaffected — real gamertags are for remote gamers ENetBackend constructs in Task 5.4). 12 new tests. 2050/2050 total passing, stable under `--gtest_shuffle --gtest_repeat=5`. |
| `34f289b` | `NetPacketCodec.cpp`, `NetDiscoveryProtocol.hpp/.cpp` (new), `NetPacketCodecTests.cpp`, `NetDiscoveryProtocolTests.cpp` (new) | Task 5.2 complete: implemented all 6 `NetPacketCodec` message encode/decode functions (`ClientHello`/`ServerWelcome`/`GamerJoinBroadcast`/`GamerLeaveBroadcast`/`StateChangeBroadcast`/`AppData`) plus `SendDataOptionsToEnetFlags`, and `NetDiscoveryProtocol`'s query/announce codec including sparse `NetworkSessionProperties` encoding (only non-null entries written, as index+value pairs). Both built on `PacketWriter`/`PacketReader` per the header committed earlier. 13 new tests, all round-trip. 2038/2038 total passing. |
| `4556200` | `include/CNA/Internal/Net/NetPacketCodec.hpp` (new) | Declares `MessageTag`, `RosterEntry`, `ClientHelloMessage`, `ServerWelcomeMessage`, `GamerJoinBroadcastMessage`, `GamerLeaveBroadcastMessage`, `StateChangeBroadcastMessage`, `AppDataMessage`, and the `NetPacketCodec` facade's API — designed to reuse the already-shipped `PacketWriter`/`PacketReader` for serialization instead of hand-rolled byte packing. (Implementation landed in the commit above.) |
| `6dd0fdd` | `include/CNA/Internal/Net/ENetLibrary.hpp/.cpp`, `ENetHostHandle.hpp/.cpp` (new), `tests/CNA/Internal/Net/ENetHostHandleTests.cpp` (new) | Task 5.1: ENet lifecycle guard (lazy `enet_initialize()`, never torn down) + move-only RAII `ENetHost*` wrapper (create/connect/service/send/broadcast/flush/disconnect). Includes a real loopback smoke test (bind two hosts, connect, exchange one UDP packet) that passed, retiring the "can this sandboxed machine even do loopback UDP" risk before building protocol/relay logic on top. 5 new tests. 2025/2025 total passing. |
| `34e5bfb` | `NetworkSession.hpp/.cpp`, `LocalNetworkGamer.hpp/.cpp` (new), `NetworkSessionTests.cpp` (new); `GamerCollection.hpp`, `SignedInGamerCollection.hpp` (extended) | Task 4.7: ported `NetworkSession` (1071 lines in FNA, the largest class in `Net`) and `LocalNetworkGamer`, **completing the entire Net API surface**. Found and documented a real, faithfully-preserved FNA bug: `EndCreate`/`EndJoin`/`EndJoinInvited` null out the static `activeAction` *after* constructing `NetworkSession`, so a constructor throw strands it non-null forever (see section 4/5). 41 new tests. |
| `588af52` | `NetworkGamer.hpp/.cpp`, `NetworkMachine.hpp/.cpp` (new); `GamerCollection.hpp` (extended) | Task 4.6: ported `NetworkGamer`/`NetworkMachine`. 7 new tests. |
| `2592841` | `PacketReader.hpp/.cpp`, `PacketWriter.hpp/.cpp` (new); `Storage/StorageDevice.cpp`, `GamerServices/Gamer.hpp/.cpp`, `GamerServices/Guide.cpp` (fixed); sharp-runtime `Stream.hpp/.cpp`, `MemoryStream.hpp/.cpp` (additive) | Task 4.5: ported `PacketReader`/`PacketWriter`. Also fixed a pre-existing, unrelated build break affecting the *entire* repo: sharp-runtime's `System::IAsyncResult` had gained two pure-virtual members that `Storage::SelectorResult`/`ContainerResult`/`Gamer::GamerAction`/`GuideAction` didn't implement. 20 new tests. |

Earlier task history (4.1–4.4, 3.1–3.15, 2.x, 1.x, 0.x — GamerServices + Net enums/early classes)
is preserved in git log; not repeated here to keep this file scannable. See `git log --oneline`
on `feature/net` for the full sequence.

---

## 4. Current blocker / main problem

**No hard blocker.** Build is clean and all 2069 tests pass. Task 5.8 (LAN discovery) is now fully
complete — **Phase 5's real-networking backend work is functionally done.** The only remaining
unit of work is Task 5.9 (final `NetworkSessionType` policy regression pass), which per the plan
is a verification/safety-net pass, not new functionality — see section 8 for the exact scope, and
the approved plan at `/home/robertvokac/.claude/plans/scalable-swimming-feigenbaum.md` (corrected
during Task 5.4 — see below) for full design detail.

**New constraint discovered during Task 5.8, same family as the Task 5.4 one below:** you cannot
end-to-end test `NetworkSession::Find()`'s full public path (`BeginFind`→`EndFind`) with a real
hosted `SystemLink` session alive in the same process — `BeginFind` throws
`InvalidOperationException` immediately because the hosting session already occupies
`activeSession_`. Unlike the Task 5.4 case, there's no raw-transport workaround either, since
`Find()`/`BeginFind`/`EndFind` are exactly the API under test. `ENetDiscoveryService::FindSessions()`
(the static method `EndFind` delegates to) has no such restriction and is fully tested directly in
`ENetDiscoveryServiceTests.cpp` — this proves the underlying discovery mechanism works end-to-end;
the one-line `EndFind`→`FindSessions` delegation itself is verified by code inspection, matching
the same "documented as untestable in this environment" precedent as `Join()`'s completion path.

**Important correction made during Task 5.4, read before writing any more loopback tests:**
`NetworkSession::BeginCreate` (all overloads) gates on a single **process-wide** `activeSession_`
static — only one real `NetworkSession` can exist in this test binary at a time; `Dispose()` on
that exact instance is the only thing that clears it. The approved plan originally assumed
ephemeral host ports meant "two hosted sessions coexist in one process" for testing — that is
**wrong**; a second `Create()` call while any session is still alive throws
`InvalidOperationException`, and if that exception isn't caught, the first (never-disposed)
session strands `activeSession_` for the rest of the process, breaking every later test that
tries to construct a session. (This happened during Task 5.4's first test draft: 42 tests failed
until fixed.) The corrected pattern — now used in `ENetBackendTests.cpp` and documented in the
plan file — is: **one real `NetworkSession` per test** (in an RAII fixture whose destructor calls
`Dispose()`, so an `ASSERT_*` failure can't strand the static), paired with a raw
`CNA::Internal::Net::ENetHostHandle` standing in for "the other machine". Reuse this pattern for
Tasks 5.5–5.8's loopback tests.

A second, unrelated thing worth flagging as context for whoever resumes (not a current blocker,
but will become one the moment `Join()`/`Create(sessionType, maxLocalGamers, maxGamers)`/
`JoinInvited(int)` need to be exercised to completion in a test): `NetworkSession::EndCreate`/
`EndJoin`/`EndJoinInvited` null out the static `activeAction`/`activeSession` **after**
constructing the `NetworkSession`, so if that constructor throws (e.g. an empty `LocalGamers`
list — the default in this test binary, since `Gamer::SignedInGamers` is never populated by
`GamerServicesDispatcher::Initialize()` in tests), `activeAction` is stranded non-null for the
rest of the process with **no public API to reset it**. This is a real, faithfully-preserved FNA
bug (FNA has the identical ordering), not something to
fix. The approved Phase 5 plan's testing strategy works around it by driving both sides of a test
connection through the safe explicit-local-gamers `Create()` overload and calling
`ENetBackend::ConnectToHost(...)` directly instead of the public `Join()`.

---

## 5. Known bugs and limitations

| Status | Issue |
|---|---|
| **Resolved** | ~~`NetPacketCodec` (Task 5.2) — header only, no `.cpp`~~ — Task 5.2 is now fully implemented and tested (13 tests, `NetPacketCodecTests.cpp`/`NetDiscoveryProtocolTests.cpp`). |
| **Confirmed bug (upstream FNA, preserved)** | `NetworkSession::EndCreate`/`EndJoin`/`EndJoinInvited` set the static `activeAction` back to `null` **after** constructing the `NetworkSession`, so a constructor throw (e.g. empty `LocalGamers`) strands `activeAction` non-null for the rest of the process — every later `NetworkSession::Begin*` call then throws a spurious `InvalidOperationException`. FNA has the identical ordering; not introduced here. This is why `Create(sessionType, maxLocalGamers, maxGamers)`, `Join(AvailableNetworkSession*)`, and `JoinInvited(int)` aren't exercised past argument validation in `NetworkSessionTests.cpp`. |
| **Design constraint found (Task 5.4), not a bug** | `NetworkSession::BeginCreate` (all overloads) gates on a single **process-wide** `activeSession_` static — only one real `NetworkSession` can be alive in this test binary at a time; a second `Create()` while one is still alive throws `InvalidOperationException`, same static-state-stranding risk as the `activeAction` bug above if the exception isn't caught before the first session's `Dispose()`. Not a bug (real single-active-session XNA/FNA semantics); just means loopback tests need exactly one real `NetworkSession` + a raw `ENetHostHandle` peer stand-in, never two real sessions. See section 4 and `ENetBackendTests.cpp`. |
| **Resolved (Task 5.3, wired for real in 5.4/5.6)** | ~~`NetworkSession::Update()`'s `GamerJoin`/`GamerLeave` branches never mutate `AllGamers`/`RemoteGamers`~~ — `AddRemoteGamer(NetworkGamer*)`/`RemoveGamer(NetworkGamer*, NetworkSessionEndReason)` now exist and correctly mutate `AllGamers`/`RemoteGamers`/`PreviousGamers` (with FIFO eviction past `MaxPreviousGamers`) and raise `GamerJoined`/`GamerLeft`, or — when the removed gamer is local — a `StateChange`-to-`Ended` event carrying the given reason (raises `SessionEnded`). `AddRemoteGamer` is called from the join handshake (Task 5.4); `RemoveGamer` is now called for real from ENet `DISCONNECT` handling and incoming `GamerLeaveBroadcast` messages (Task 5.6). |
| **Resolved (Task 5.3, used for real in 5.4)** | ~~`NetworkGamer::CreateInternal`'s single-arg constructor hardcodes `Gamer("Stub Gamer", "Stub Gamer")`~~ — `CreateInternal`/the protected constructor now take a `const std::string& gamertag = "Stub Gamer"` param. `LocalNetworkGamer` still passes nothing (stays "Stub Gamer", matching FNA's stub identically for local gamers — verified explicitly by Task 5.4's loopback tests); `ENetBackend` now passes real gamertags (received via `ClientHello`/`ServerWelcome`/`GamerJoinBroadcast`, or read from `LocalNetworkGamer::getSignedInGamerProperty()->getGamertagProperty()` when sending) when constructing **remote** `NetworkGamer` instances. |
| **Resolved (Task 5.5)** | ~~`LocalNetworkGamer::SendData`/`ReceiveData` field-meaning collision~~ — every `SendData` overload now sets `NetworkEvent::Sender = this`; `NetworkSession::Update()`'s `PacketSend` handling remaps `.Gamer = evt.Sender` when delivering into a target's `packetQueue_`, so `ReceiveData`'s already-shipped `.Gamer == packet.Gamer` sender-matching logic resolves correctly with zero changes to `ReceiveData` itself. |
| **Confirmed bug (upstream FNA, preserved), relevant to Task 5.7** | `NetworkGamer::getIsHostProperty()` always returns `true` (FNA's own stub, "matches FNA's stub" per its doc comment), so `NetworkSession::getIsHostProperty()` (which OR's every local gamer's value) is `true` for **any** session with at least one local gamer — including a session that's actually an ENet-transport *client* (connected out via `ConnectToHost`). This means `StartGame()`/`EndGame()`'s `!getIsHostProperty()` guard doesn't actually restrict calls to the real ENet host. Task 5.7 works around this at the `ENetBackend::BroadcastStateChange` level (only actually broadcasts when `SessionState::HostPeer == nullptr`) rather than fixing the FNA-preserved property. |
| **Design constraint found (Task 5.8), same family as the `activeSession_` one above** | `NetworkSession::Find()`'s full public path can't be end-to-end tested with a real hosted `SystemLink` session alive in the same process — `BeginFind` throws `InvalidOperationException` immediately since the hosting session already occupies `activeSession_`, and (unlike Task 5.4's `ConnectToHost` case) there's no raw-transport stand-in workaround, since `Find`/`BeginFind`/`EndFind` are exactly the API under test. Worked around by testing `ENetDiscoveryService::FindSessions()` directly (no `activeSession_` involvement) — this proves the discovery mechanism itself works end-to-end; `EndFind`'s one-line delegation to it is verified by inspection only, matching the same "documented as untestable" precedent as `Join()`'s completion path. |
| **Real gap found and fixed (Task 5.8)** | `NetDiscoveryProtocol`'s `DiscoveryQueryMessage`/`DiscoveryAnnounceMessage` (Task 5.2) shared no leading tag byte — harmless when only tested via direct `Encode`/`Decode*` calls, but both now travel over the *same* raw UDP socket in real usage, so a receiver had no way to tell them apart. Fixed by adding `DiscoveryMessageTag`/`PeekTag`, mirroring `NetPacketCodec`'s already-established pattern. |
| **Real gap found and fixed (Task 5.8)** | `ENetDiscoveryService::FindSessions()`'s initial implementation returned the *same* discovered host twice — broadcasting a `DiscoveryQuery` and also unicasting an explicit loopback copy means the same host's reply can arrive at the searcher via two different paths (observed under two different source addresses on this machine: `127.0.0.1` and the machine's real LAN IP), and neither path is disqualified as "not really a duplicate." Fixed by deduping collected results by connect port — exactly the correlation key the approved plan's own "Discovery protocol" section anticipated needing, though the actual trigger (address instability across delivery paths, not "two sessions on one machine") wasn't the scenario originally envisioned for it. |
| **Note (not a bug)** | `ENetDiscoveryService`'s well-known discovery port (61190) is a genuinely fixed, hardcoded value — real LAN discovery protocols require this (a querying machine has to know what port to ask on). On this shared dev machine, an unrelated concurrent process binding the same port is a narrow, accepted risk (matches this project's existing "shared machine, occasional build contention" caveat in section 7), not something Phase 5 attempts to avoid. |
| **Note (not a bug)** | `DiscoveryAnnounceMessage.OpenPrivateSlots`/`OpenPublicSlots` are computed honestly but not precisely: nothing in this codebase tracks per-gamer slot occupancy (`NetworkGamer::getIsPrivateSlotProperty()` is a never-toggled stub), so `OpenPrivateSlots` reports the session's *configured* private-slot count as if always fully open, and `OpenPublicSlots` is `MaxGamers - PrivateGamerSlots - CurrentGamerCount`. Remember `NetworkSession::EndCreate` also hardcodes `MaxGamers` to 69 regardless of the caller's argument (a separate, pre-existing FNA quirk, see above) — so these numbers reflect that hardcoded 69, not whatever a caller passed to `Create()`. |
| **Deviation (documented)** | `PacketWriter::Write(Color)` writes 4 bytes but `PacketReader::ReadColor()` reads 4 floats (16 bytes) — genuinely asymmetric upstream, preserved as-is. Not round-trippable through these two methods alone. |
| **Deviation (documented)** | `PacketReader(int capacity)`/`PacketWriter(int capacity)` discard the `capacity` argument — sharp-runtime's `MemoryStream` has no preallocating constructor; capacity is a pure optimization hint in .NET with no observable effect, so nothing is lost. |
| **Deviation (documented)** | `NetworkGamer::getIsLocalProperty()` is a virtual method overridden by `LocalNetworkGamer`, not a `dynamic_cast` runtime check like FNA's `this is LocalNetworkGamer` — a base class header can't `dynamic_cast` to a derived type it can't include. Behavior is identical either way. |
| **Deviation (documented)** | `NetworkSession::BeginJoin`/`BeginJoinInvited(int)`/`EndJoin`/`EndJoinInvited` pass/expect `null` for `NetworkSessionProperties` (FNA marks these exact lines `// FIXME` itself). Substituted with a default-constructed instance since this port's type isn't nullable. |
| **Deviation (documented)** | `LocalNetworkGamer::ReceiveData(PacketReader&, ...)` always returns 0 — FNA declares a length variable it never updates before returning it, even though the underlying packet write is real. Preserved as-is. |
| **Note (not a bug)** | `SignedInGamer::IsHeadset()`, `GamerServicesComponent`, and `GamerServicesDispatcher::Initialize()` have no automated tests — documented per `CHECKLIST.md`'s "classes that cannot be unit-tested" provision (SDL/Game dependency, or shared-process static-state pollution risk, respectively). Phase 5's `NetworkSession::Join()`-family limitation above joins this category. |
| **Incomplete** | `GamerJoinedEventArgs`/`GamerLeftEventArgs`/`HostChangedEventArgs`/`WriteLeaderboardsEventArgs` tests still use `nullptr` stand-ins for `NetworkGamer*` instead of real instances (both types now exist; just not yet revisited). |
| **Confirmed bug (graphics)** | `SpriteBatch` multiple `Begin()/End()` per frame on Vulkan: only the last batch renders. |
| **Suspected bug (graphics)** | `DrawUserIndexedPrimitives` typed overloads likely have the silent-return-on-missing-effect bug (not yet audited — Task 252). |

---

## 6. Architecture notes

### Module map

| Layer | Location | Notes |
|---|---|---|
| XNA public API (graphics) | `include/Microsoft/Xna/Framework/…` | Must match XNA 4.0 / FNA exactly |
| XNA public API (GamerServices) | `include/Microsoft/Xna/Framework/GamerServices/` | Complete. Internal ctors → `private` + `CreateInternal()` factory |
| XNA public API (Net) | `include/Microsoft/Xna/Framework/Net/` | Complete API surface (5 enums + 18 classes). Internals being wired to real networking in Phase 5 — **public shapes here are a fixed point, must not change** |
| ENet backend (Phase 5, nearly complete) | `include/CNA/Internal/Net/`, `src/CNA/Internal/Net/` | `ENetLibrary`/`ENetHostHandle` (5.1), `NetPacketCodec`/`NetDiscoveryProtocol` (5.2), `ENetBackend` registry/wiring (5.3), connect+handshake (5.4), `AppData` relay (5.5), disconnect/leave handling (5.6), state broadcast (5.7), and LAN discovery via `ENetDiscoveryService` (5.8) all done and tested; only the final `NetworkSessionType` policy regression pass remains (Task 5.9) |
| Backend contracts (graphics only) | `include/CNA/Internal/Backends/Common/` | `IGraphicsBackend`, etc. — **not** the pattern used for networking (see section 1) |
| CNA utilities | `include/CNA/`, `src/CNA/` | NOXNA helpers, logging |
| sharp-runtime | `../sharp-runtime/` (sibling repo) | `System.*` types; only add new files; no version pin from this repo (see section 1) |

### Key invariants

- **`NOXNA` macro** tags every non-XNA extension in public headers under `Microsoft::Xna::…`.
  Does **not** apply to `CNA::Internal::Net` (Phase 5's new code) — that's already outside the XNA
  namespace, so nothing there needs the marker.
- **C# `internal` constructors** → `private` in C++, exposed via `NOXNA static CreateInternal(…)`.
- **C# properties** → `getXProperty()`/`setXProperty()`. Plain PascalCase methods (e.g.
  `SendNetworkEvent`, `ClearPacketQueue`, `GetConnectAddress`/`GetConnectPort` on
  `AvailableNetworkSession`) signal a CNA-only concept that has no FNA property to match the
  getter/setter convention against. Public-field style is never used to shortcut this convention —
  except where FNA/real XNA itself already uses public fields, e.g.
  `Vector2`/`Vector3`/`Vector4`/`Matrix`/`Quaternion`.
- **`System::Exception`** is the base for all GamerServices/Net exceptions (never `std::runtime_error`).
- **`GamerCollection<T>`** stores raw non-owning `T*` pointers. Gained `CreateInternal`/`Add`
  (Task 4.6/4.7) and `Remove` (Task 5.3) as NOXNA escape hatches for the C# `internal`
  same-assembly access FNA relies on that C++ `protected` doesn't replicate across sibling
  classes.
- **CNA-internal ENet code lives entirely under `CNA::Internal::Net`**, never
  `Microsoft::Xna::Framework::Net` — `enet/enet.h` and its types must not leak into any public
  XNA-facing header. `ENetBackend`'s per-session state (`SessionState` — `ENetHostHandle Host`,
  `uint8_t NextWireId`, `ENetPeer* HostPeer`, `GamerToWireId`/`WireIdToGamer`/`PeerWireIds`/
  `WireIdToPeer` maps, as of Task 5.5) is a private `.cpp`-only struct, not declared in any header,
  keyed by `NetworkSession*` in a `std::unordered_map<NetworkSession*, std::unique_ptr<SessionState>>`
  function-local static registry (`ENetBackend.cpp`'s anonymous-namespace `Sessions()`).
  See the approved plan for the exact new-file layout.
- **Only one real `NetworkSession` can exist per process at a time** (`activeSession_` static
  gate in `BeginCreate`/`BeginFind`, discovered/documented properly during Task 5.4 — see section
  4/5). Loopback networking tests must use one real `NetworkSession` + a raw `ENetHostHandle` (or,
  for discovery, a directly-called `ENetDiscoveryService::FindSessions()`) peer stand-in, never two
  real sessions; see `ENetBackendTests.cpp`'s `SystemLinkSessionFixture` and
  `ENetDiscoveryServiceTests.cpp`'s copy of the same pattern.
- **`ENetDiscoveryService`'s well-known discovery port is `61190`**, hardcoded (real LAN discovery
  needs a fixed, known port). Its raw UDP socket is process-wide, lazily created, never torn down
  (same "no C++ AppDomain.ProcessExit equivalent" precedent as `ENetLibrary`). Query and Announce
  messages share this one socket, so they carry an explicit `DiscoveryMessageTag` (added Task 5.8,
  a gap found in Task 5.2's original `NetDiscoveryProtocol` design) to tell them apart on receipt.
- **Template headers** (e.g. `GamerCollection.hpp`) contain full implementation — no `.cpp`.
- **SPDX headers:** `// SPDX-License-Identifier: MS-PL` for files ported from FNA (both `.hpp`
  and `.cpp`). `// SPDX-License-Identifier: MIT` + `// Copyright (c) Robert Vokac and
  contributors` for **original** CNA-internal code with no FNA equivalent (established this
  session for all of Phase 5's new files — MS-PL is FNA's own license and should only mark files
  that actually port FNA source).
- **Doxygen** `/** @brief … @param … @return */` required on every public member, in every `.hpp`
  — not just XNA-facing ones.
- sharp-runtime: **only add new files**, never modify existing ones, *unless* the existing file
  is genuinely blocking every build in this repo (happened once — Task 4.5's `IAsyncResult`
  fix — and once for `Stream`/`MemoryStream` `Position` support, additive-only). Treat this as a
  last resort, not a routine option.

### Net class dependency order (complete)

```
Enums (done) → Exceptions (done) → Data structs (done) → EventArgs (done)
→ GamerServices fully done (Gamer, collections, SignedInGamer, GamerServicesDispatcher/
  GamerServicesComponent/Guide)
→ Net enums (done) → NetworkSessionProperties / QualityOfService / AvailableNetworkSession(Collection)
  / Net event-args / NetworkSessionJoinException / PacketReader / PacketWriter (done)
→ NetworkGamer (done) / NetworkMachine (done)
→ NetworkSession (done) → LocalNetworkGamer (done)
→ [Phase 5, nearly complete] ENetLibrary/ENetHostHandle (done, 5.1) → NetPacketCodec/
  NetDiscoveryProtocol (done, 5.2) → ENetBackend + NetworkSession wiring (done, 5.3) →
  handshake/roster sync (done, 5.4) → AppData relay (done, 5.5) → disconnect handling
  (done, 5.6) → state broadcast (done, 5.7) → discovery (done, 5.8) → regression pass (5.9, next)
```

---

## 7. Useful commands

```bash
# Working directory
cd /rv/data/development/github.com/openeggbert/cna_net

# Full build (all targets: CNA, CNA_GamerServices, CNA_Net, CnaTests)
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"

# Run all tests
cmake-build-debug/CnaTests

# Run just the Net/GamerServices/Phase-5 tests
cmake-build-debug/CnaTests --gtest_filter="*Network*:*Gamer*:*ENet*:*Packet*"

# Run just the new Phase 5 ENet backend tests
cmake-build-debug/CnaTests --gtest_filter="ENetHostHandleTest.*:ENetBackendTest.*:ENetDiscoveryServiceTest.*"

# FNA reference source (for GamerServices/Net API-shape questions — Phase 5 itself has no FNA
# reference, see section 1)
ls /rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/GamerServices/
ls /rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/Net/

# ENet reference (vendored)
cat third_party/enet/include/enet/enet.h

# Approved Phase 5 design plan (read this before continuing Phase 5 work)
cat /home/robertvokac/.claude/plans/scalable-swimming-feigenbaum.md

# sharp-runtime include root
ls /rv/data/development/github.com/openeggbert/sharp-runtime/include/System/
```

Builds can occasionally time out on this shared machine if another session is compiling
concurrently (observed load average >100 on a 16-core box) — retry with a reduced `-j` and a
longer timeout rather than assuming a real compile error; check `pgrep -fl cc1plus` before
concluding a build is stuck.

---

## 8. Next smallest tasks

In priority order (all from the approved plan at
`/home/robertvokac/.claude/plans/scalable-swimming-feigenbaum.md` — read it in full before
starting, **including the Task 5.4 correction notes added to the Testing strategy section**; this
section is a summary, not a replacement):

1. **Task 5.9 — Final `NetworkSessionType` policy regression pass.** The last Phase 5 task, and per
   the plan a verification/safety-net pass rather than new functionality. Goal: one explicit test
   per non-`SystemLink` `NetworkSessionType` value (`Local`, `LocalWithLeaderboards`, `PlayerMatch`,
   `Ranked`) proving that constructing/using a session of that type is **provably unaffected** by
   all of Phase 5's real-networking machinery — i.e. `ENetBackend::RealNetworkingEnabled(type)` is
   `false`, `ENetBackend::GetBoundPort(session)` is `0`, `PacketSend`/`AppData`/state-broadcast
   stay complete no-ops, and `Find(type, ...)` returns empty **immediately** (no 150ms real search
   window — only `SystemLink` should ever pay that cost). Several of these already exist
   piecemeal (`StartHostingIsNoOpForNonSystemLinkTypes`, `ConnectToHostIsNoOpForNonSystemLinkTypes`,
   `FindSessionsReturnsEmptyImmediatelyForNonSystemLinkTypes`, etc., scattered across
   `ENetBackendTests.cpp`/`ENetDiscoveryServiceTests.cpp`) — this task's job is to make sure the
   coverage is **systematic** (one test per enum value, not just spot checks) and to close any gap
   found. Also: final `NEXT.md` update declaring Phase 5's backend work complete (this file already
   mostly reflects that after Task 5.8 — 5.9 is confirmation, not a rewrite).
   - Verify with `cmake --build cmake-build-debug --target CnaTests`, then
     `cmake-build-debug/CnaTests` full run (2069+ expected) — this task should add tests, not
     change any production code, so the only real risk is discovering an actual regression.
   - Files: likely only test files (`ENetBackendTests.cpp`, `ENetDiscoveryServiceTests.cpp`, or a
     new dedicated `NetworkSessionTypePolicyTests.cpp` if that reads more clearly as one file).

---

## 9. Do not do yet

- **No changes to `Microsoft::Xna::Framework::Net` public class shapes, method signatures, or
  property names** — the entire Net API surface is a fixed point; Phase 5 only changes internal
  implementation. The plan's "Blast radius" table lists the small, additive, NOXNA-marked
  exceptions (new defaulted fields/params, new NOXNA methods) — nothing beyond that list.
- **No `INetworkBackend` abstract interface** — decided against in the plan; ENet is the only
  implementation, an interface would be speculative abstraction.
- **No fixing the `NetworkSession::EndCreate`/`EndJoin`/`EndJoinInvited` static-state-stranding
  bug** — it's a faithfully-preserved real FNA bug, not a defect to correct. Work around it in
  tests (see the plan's testing strategy) instead.
- **No test that constructs two real `NetworkSession` instances in one process** — `BeginCreate`
  gates on a single process-wide `activeSession_` (see section 4/5); the second `Create()` throws,
  and if uncaught, strands `activeSession_` for the rest of the test binary. Use one real
  `NetworkSession` + a raw `ENetHostHandle` peer stand-in instead (see `ENetBackendTests.cpp`'s
  `SystemLinkSessionFixture`), and RAII-guard `Dispose()` so an `ASSERT_*` failure can't strand it.
- **No test that calls the public `NetworkSession::Find()`/`BeginFind()` while a real hosted
  session is alive in the same process** — `BeginFind` gates on the same `activeSession_` static,
  so it would throw immediately. Test `ENetDiscoveryService::FindSessions()` directly instead (see
  `ENetDiscoveryServiceTests.cpp`) — it has no `activeSession_` involvement.
- **No enet.h includes in any `Microsoft::Xna::Framework::Net` header** — keep ENet's C API
  entirely behind `include/CNA/Internal/Net/`.
- **No changes to graphics-layer code** — the graphics phase (31) is healthy and should not be
  disturbed, except for the kind of build-break emergency fix already made once (Task 4.5,
  `Storage/StorageDevice.cpp`'s `IAsyncResult` implementation).
- **No modifications to existing sharp-runtime files** without a build-break-level reason — see
  section 6's invariant on this.
- **No public-field shortcuts** — do not replace `getXProperty()`/`setXProperty()` with public
  fields to save time (except where FNA itself already uses public fields, e.g. `Vector2`/`Matrix`).
- **No Avatar work (Phase 8)** — deferred, low priority, blocked behind Net/ENet completion.

---

## 10. Resume prompt

```
Read NEXT.md first, then read the approved Phase 5 design plan in full:
/home/robertvokac/.claude/plans/scalable-swimming-feigenbaum.md
Open only the files needed for the first task listed in section 8. Do not refactor unrelated
code. Do not expand scope beyond the task.

Current status: GamerServices + Net API surface fully ported and tested. Phase 5 (ENet backend)
Tasks 5.1 (ENetLibrary/ENetHostHandle), 5.2 (NetPacketCodec/NetDiscoveryProtocol), 5.3 (ENetBackend
registry + NetworkSession wiring), 5.4 (real ConnectToHost + ClientHello/ServerWelcome/
GamerJoinBroadcast handshake over loopback UDP), 5.5 (AppData relay: real SendData/ReceiveData,
with host relay for gamers it doesn't own), 5.6 (real ENet disconnect handling: host
removes/broadcasts departed clients, clients raise SessionEnded on losing their host), 5.7
(StartGame/EndGame now broadcast a StateChangeBroadcastMessage to every connected peer), and 5.8
(real LAN discovery via ENetDiscoveryService: Find/BeginFind/EndFind now genuinely discover hosted
SystemLink sessions over a raw UDP protocol) are all done and tested. Phase 5's real-networking
backend is functionally complete — only Task 5.9 (a regression/verification pass, not new
functionality) remains. 2069/2069 tests passing, stable under --gtest_shuffle --gtest_repeat=5
(full suite) and --gtest_repeat=15 (discovery tests).

IMPORTANT corrections, read before writing more loopback tests:
1. (Task 5.4) Only one real NetworkSession can exist per process (activeSession_ static gate in
   BeginCreate) — never construct two real NetworkSession instances in a test. Use one real
   NetworkSession + a raw ENetHostHandle peer stand-in instead (see ENetBackendTests.cpp's
   SystemLinkSessionFixture), RAII-guarding Dispose() so an ASSERT_* failure can't strand the
   static for every later test.
2. (Task 5.8) The same activeSession_ gate also blocks BeginFind — you cannot call the public
   NetworkSession::Find()/BeginFind() while a real hosted session is alive in the same process.
   Test ENetDiscoveryService::FindSessions() directly instead (no activeSession_ involvement); see
   ENetDiscoveryServiceTests.cpp.
See section 4/5 and the plan file's corrected Testing strategy section for the full story.

Before trusting any of this: do a real `cmake --build cmake-build-debug --target CnaTests` yourself
first. sharp-runtime is a sibling repo edited by a separate session with no version pin from here
— an interface change there silently broke every build target once already this project (see
section 4's history / git log for the Task 4.5 IAsyncResult incident).

Next: Task 5.9 — final NetworkSessionType policy regression pass (see section 8, item 1). This is
the LAST Phase 5 task — after it's done, Phase 5's backend work is complete.
Read the full approved plan first: /home/robertvokac/.claude/plans/scalable-swimming-feigenbaum.md
Build: cmake --build cmake-build-debug --target CnaTests
Run:   cmake-build-debug/CnaTests
Update NEXT.md after finishing each task.
```
