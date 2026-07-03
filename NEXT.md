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
- **Current development phase:** **Phase 5 (the ENet networking backend) is COMPLETE.** Now in
  **Phase 6 (Platform-Specific Work)**: Task 6.1 (Linux two-process real ENet test) is **done**;
  Task 6.2 (Windows cross-build + Wine verification) is **in progress, paused mid-flight** — see
  section 4 for exactly where and why. A fresh, approved plan for Phase 6 (scoped to Tasks 6.1/6.2
  only — 6.3/6.4 need SDKs this sandbox doesn't have) lives at the same plan file path as Phase 5's
  (see below), which was overwritten for Phase 6 since it's a different task.
  Graphics phases (1–31) are complete. `GamerServices` (all classes) and `Net` (5 enums + all 18
  non-enum classes: the full public API surface) are complete and unit-tested — see
  `plan_net.md`/Task history below. Phase 5 made that already-ported `Net` API actually do real
  networking, since FNA's own `Net` source (`FNA.NetStub`) never had a working implementation to
  port from — it was a non-functional stub (every gamer named "Stub Gamer", `Find()` always empty,
  etc.) before this phase. **Phase 5 was therefore original design work, not line-by-line
  FNA-fidelity porting.** The approved design plan for all of Phase 5's 9 sub-tasks (5.1–5.9, all
  done) lived at `/home/robertvokac/.claude/plans/scalable-swimming-feigenbaum.md` — that same file
  now holds the **Phase 6** plan instead (plan files get overwritten per task per this project's
  workflow; if you need Phase 5's original plan content for historical reference, it's preserved in
  this session's conversation history, not in a standalone file). **Note on `plan_net.md`:** that
  file's own, separately-numbered "Phase 5" checklist (a sketch written before this effort started)
  describes a different, more elaborate design — an abstract `INetworkBackend` interface, host
  migration, a `PlayerMatch` relay server via a `CNA_NET_RELAY_HOST` env var, latency/QoS
  simulation — that was **deliberately not followed**; see section 1's "Important architectural
  decisions" and section 9 for why (no abstract interface: ENet is the only implementation).
  `plan_net.md`'s checkboxes were never kept up to date as a live tracker across this whole
  multi-session effort (every phase's boxes are still unchecked, including graphics phases 1–31
  and GamerServices/Net, which are provably done) — `NEXT.md` is the actively maintained status
  document; don't infer progress from `plan_net.md`'s checkboxes. Its Task 6.1–6.5 sketch is what
  the current Phase 6 plan is based on (scoped down to what's actually achievable here).
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
- Last verified clean build + full test run: **2076 / 2076 unit tests passing** (Task 5.9
  complete — **all of Phase 5 is now done**), verified stable under `--gtest_shuffle
  --gtest_repeat=8` (full suite) and `--gtest_repeat=15` (`NetworkSessionTypePolicyTest`/discovery
  tests specifically, given their real-socket timing).
- Full suite runtime is ~1.1s (up from ~30ms pre-Phase-5) — expected and correct: `Find()` on
  `SystemLink` genuinely blocks for a real (150ms) network discovery window (Task 5.8), and
  several tests (the pre-existing `FindReturnsEmptyCollection` plus the new
  `NetworkSessionTypePolicyTest`/`ENetDiscoveryServiceTest` suites) call it multiple times.
- **Task 5.9 required zero production code changes** — it was a pure verification/regression
  pass, and every new systematic test passed on the first run, confirming Tasks 5.1–5.8's
  `NetworkSessionType` gating (`RealNetworkingEnabled`) was already correctly applied everywhere.
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
  a real raw-UDP LAN discovery protocol). **Task 5.9 complete** (final `NetworkSessionType` policy
  regression pass — a systematic sweep, in new `NetworkSessionTypePolicyTests.cpp`, over all four
  non-`SystemLink` types proving each is provably unaffected by every real-networking mechanism
  Tasks 5.1–5.8 built; found zero regressions, changed zero production code).
  **Phase 5 (the ENet networking backend) is complete.** `Microsoft::Xna::Framework::Net`'s
  entire public API surface now does real LAN networking for `SystemLink` sessions, while every
  other `NetworkSessionType` remains exactly the pre-Phase-5 synthetic stub, byte-for-byte.
- Phase 6 (Platform-Specific Work): **Task 6.1 complete** — a genuine two-OS-process ENet loopback
  test (`tools/net/net_two_process_harness.cpp` spawned twice by
  `tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp`), proving Phase 5's real transport works
  across independent address spaces, not just the one-process-two-roles trick every other Phase 5
  test uses. **Task 6.2 (Windows cross-build + Wine) is in progress, paused mid-flight** — see
  section 4 for the exact stopping point and what's needed to resume it.

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
- `NetworkSessionType` policy regression pass (Task 5.9): new `NetworkSessionTypePolicyTests.cpp`
  systematically sweeps all four non-`SystemLink` types (`Local`, `LocalWithLeaderboards`,
  `PlayerMatch`, `Ranked`) through everything Phase 5 built — `RealNetworkingEnabled` is `false`;
  `Create()` never binds a port; `Update()` never throws or binds a port; `ConnectToHost` is a
  no-op; `SendData` stays fully synthetic (no delivery); `StartGame`/`EndGame` still work locally
  (raise `GameStarted`/transition `SessionState`) but never bind a port to broadcast from;
  `Find()` returns empty in well under 50ms (proving the fast synthetic path is taken, not that it
  just found nothing after paying `SystemLink`'s real 150ms search cost). All 7 new tests passed
  on the first run with zero production code changes.
- Two-process real ENet loopback (Task 6.1): `cna_net_two_process_harness` (new CMake executable,
  `tools/net/net_two_process_harness.cpp`) plays `--role=host` or `--role=client`; the new
  `TwoProcessLoopbackTest.cpp` orchestrator spawns both via `posix_spawn` (host's real bound port
  handed to the client through a pipe on the host's stdout, not via `ENetDiscoveryService` — see
  that file's own comments for why sharing the discovery port across two independent processes was
  deliberately avoided), waits on both with a SIGKILL watchdog, and asserts both exit 0 after a
  real join + one `AppData` round trip. Passed on the first run and stable across 10 repeats —
  genuinely proves Phase 5's ENet transport works across independent OS processes, not just within
  one process's shared static state.

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
- **Windows cross-build (Task 6.2) is unverified as of this writing** — see section 4 for the
  exact stopping point. The Linux-native build and the new two-process test are unaffected.

---

## 3. Recent changes

This session's commits, newest first (all on branch `feature/net`, pushed to
`origin/feature/net`):

| Commit | Files | Change |
|---|---|---|
| (uncommitted at time of writing) | `CMakeLists.txt` (extended); `tools/net/net_two_process_harness.cpp` (new); `tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp` (new) | **Phase 6, Task 6.1 complete**: a genuine two-OS-process ENet loopback test. New `cna_net_two_process_harness` executable plays `--role=host`/`--role=client`; the orchestrator test spawns both via `posix_spawn`, hands the host's real bound port to the client through a pipe (deliberately not through `ENetDiscoveryService`'s shared port — see the file's own comments on why cross-process discovery-port sharing is fragile here), watches both with a `waitpid`+`SIGKILL` timeout, and asserts a real join + `AppData` round trip succeeds. Passed first try, stable across 10 repeats. Does not touch sharp-runtime. See section 4 for Task 6.2's (Windows) in-progress, paused status, including 4 real Windows-specific bugs found and fixed in sharp-runtime along the way (uncommitted there, pending explicit user go-ahead to commit in that sibling repo). 1 new test, 2077/2077 total passing on native Linux. |
| `6f1f11e` | `NetworkSessionTypePolicyTests.cpp` (new) | Task 5.9 complete — **Phase 5 done.** Final `NetworkSessionType` policy regression pass: a systematic sweep (one set of assertions per non-`SystemLink` type, not spot checks) proving `Local`/`LocalWithLeaderboards`/`PlayerMatch`/`Ranked` are all provably unaffected by every real-networking mechanism Tasks 5.1–5.8 built (`RealNetworkingEnabled` false, no port ever bound, `ConnectToHost`/`SendData` stay no-ops, `StartGame`/`EndGame` still work locally without broadcasting, `Find()` returns empty in <50ms rather than paying `SystemLink`'s real 150ms search window). Zero production code changes — every test passed on the first run, confirming the `RealNetworkingEnabled` gating already added throughout 5.1–5.8 was correctly applied everywhere. 7 new tests, 2076/2076 total passing, stable under `--gtest_shuffle --gtest_repeat=8` and `--gtest_repeat=15`. |
| `f1f5587` | `NetDiscoveryProtocol.hpp/.cpp` (extended); `ENetDiscoveryService.hpp/.cpp` (new); `AvailableNetworkSession.hpp/.cpp` (extended); `ENetBackend.cpp`, `NetworkSession.cpp` (extended); `NetDiscoveryProtocolTests.cpp`, `AvailableNetworkSessionTests.cpp` (extended), `ENetDiscoveryServiceTests.cpp` (new) | Task 5.8 complete: real LAN discovery. Found and fixed a real gap in Task 5.2's design first — `DiscoveryQueryMessage`/`DiscoveryAnnounceMessage` shared no leading tag byte, but both now travel over the same raw UDP socket, so added `DiscoveryMessageTag`/`PeekTag` (mirrors `NetPacketCodec`'s existing pattern) before either could be decoded on receipt. New `ENetDiscoveryService`: one process-wide raw UDP socket (well-known port 61190, `SOCKOPT_BROADCAST`+`REUSEADDR`+`NONBLOCK`), `RegisterHost`/`UnregisterHost` (wired into `ENetBackend::StartHosting`/`TeardownSession`), `Poll()` (wired into `NetworkSession::Update()`, non-blocking, answers incoming queries), `FindSessions(type)` (wired into `NetworkSession::EndFind`, blocks a real fixed 150ms window). Found and fixed a real duplicate-result bug during testing: broadcasting a query AND explicitly unicasting a loopback copy means the same host's reply can arrive twice on one machine (via different observed source addresses) — fixed by deduping collected results by connect port, exactly the correlation key the plan's own design anticipated needing. 8 new tests, 2069/2069 total passing, stable under `--gtest_shuffle --gtest_repeat=5` and `--gtest_repeat=15` on the discovery-specific tests. |
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

**Phase 5: no blocker, fully done.** Build is clean and all 2076 tests pass (2077 including the
new Task 6.1 test — see below). Task 5.9 (final `NetworkSessionType` policy regression pass) is
complete, closing out Phase 5 entirely.

**Phase 6 status — read this before touching Task 6.2 again:**

- **Task 6.1 (Linux two-process test): done, committed-ready, no issues.** See "What works" in
  section 2 and `tools/net/net_two_process_harness.cpp` /
  `tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp`. This work does **not** touch sharp-runtime
  at all — safe to commit/push independent of the Task 6.2 situation below.

- **Task 6.2 (Windows cross-build + Wine): in progress, paused mid-build.** What happened, in
  order:
  1. Configured `cmake -B cmake-build-windows -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake
     -DCNA_GRAPHICS_BACKEND=SDL_RENDERER -DCNA_ENABLE_NET=ON` (matches the toolchain file's own
     documented usage — **not** `EASYGL`, which is Linux/Emscripten-only; an earlier plan draft
     wrongly suggested `EASYGL` for Windows, corrected here).
  2. Hit a stale `.sdl-prebuilt/` cache (a gitignored, persistent-across-builds SDL3 install root —
     see `cmake/ThirdPartySDL.cmake`'s own doc comment) left over from when this repo apparently
     lived at a different path (`.../openeggbert/cna/` instead of `.../cna_net/`). Fixed per that
     file's own documented recovery step: `rm -rf .sdl-prebuilt` (gitignored build cache, safe to
     delete) and reconfigured — SDL3/SDL3_mixer built from vendored source for mingw successfully.
  3. Configure then failed on `find_package(ZLIB REQUIRED)` inside **sharp-runtime's**
     `CMakeLists.txt` — no mingw zlib dev package was installed. **The user installed
     `libz-mingw-w64-dev` via apt themselves**; configure succeeded after that.
  4. Build then hit a **real, genuine build break in `sharp-runtime`** (not in any `cna_net`/Net
     code) — four Windows/mingw-specific bugs, all pre-existing (not introduced by this session,
     just never exercised under a real Windows/GCC target before):
     - `src/System/Environment.cpp`: `SetEnvironmentVariable`/`SetCurrentDirectory` collide with
       `<windows.h>` A/W-suffix macros (the file already `#undef`s `GetCurrentDirectory` for the
       identical reason, just missed these two) — definitions in the `.cpp` were silently renamed
       to `...A` by the preprocessor, mismatching the header's declarations.
     - Same file: `SHGetFolderPathA`/`SHGFP_TYPE_CURRENT` used without `#include <shlobj.h>`.
     - Same file: `#pragma warning(suppress: 4996)` is MSVC-only syntax; GCC/mingw's
       `-Werror=unknown-pragmas` turns the resulting "unknown pragma" warning into a hard error.
     - `src/System/Net/Sockets/{NetworkStream,TcpClient,UdpClient}.cpp`: same MSVC-only-pragma
       issue, this time `#pragma comment(lib, "ws2_32.lib")` (harmless under `-Wno-error`, fatal
       under this project's `-Werror`). `ws2_32` is already correctly linked for `WIN32` at the
       CMake level (`sharp-runtime/CMakeLists.txt:48-49`), so the pragma was always redundant
       under GCC, not load-bearing.
     **Fixed all four narrowly** — every change is inside `#if defined(_WIN32)` and/or
     `#if defined(_MSC_VER)` guards, so the Linux/native code paths are provably untouched: verified
     by rebuilding sharp-runtime's own test suite on Linux **twice** (once after each round of
     fixes) — **8467/8467 passing both times**. **The user explicitly approved this fix-and-verify
     approach both times it came up.**
  5. **The Windows build was never actually re-attempted after these fixes landed.** Two
     subsequent `AskUserQuestion` prompts asking to proceed with the next round of
     sharp-runtime-touching/Windows-build work each went unanswered for 60 seconds (timed out, not
     declined) — per this project's "don't keep retrying a blocked action" instruction, that line
     of work was paused rather than resumed unilaterally. **The Windows cross-build's last known
     state is exactly where step 4 left it**: sharp-runtime's mingw build fixed and reverified on
     Linux (8467/8467, twice), but the actual `cmake --build cmake-build-windows --target CnaTests
     --target cna_net_two_process_harness` invocation with those fixes in the tree has not been
     run. Whether it links cleanly, and whether `CnaTests.exe`/`cna_net_two_process_harness.exe`
     pass under Wine, is entirely unverified.
  6. Separately, while this Windows-build line of work sat paused, a routine and fully-permitted
     **native Linux verification build** (`cmake --build cmake-build-debug --target CnaTests`) done
     for Task 6.1's own sake failed with bizarre `multiple definition of ...` linker errors
     mentioning **Windows `.dll` files**. Root cause: `.sdl-prebuilt/` (see step 2) is keyed **only
     by source path** (`CMAKE_CURRENT_SOURCE_DIR`), not by target platform/architecture — the
     *original* Windows SDL3 configure back in step 2 (before the sharp-runtime saga even started)
     had regenerated that directory with Windows binaries, silently overwriting the *same*
     directory the *native* `cmake-build-debug` build's cached `SDL3::SDL3` imported target still
     pointed at, so the native build started linking Windows DLLs into Linux executables. **Fixed
     by `rm -rf .sdl-prebuilt` again and rebuilding natively** (regenerates Linux `.so` files);
     native build confirmed fully healthy again afterward (2077/2077, stable across 5 shuffled
     repeats). **This is a real, reusable gotcha for this repo, not just an artifact of this
     session — see the dedicated "Known bugs and limitations" entry.** Note this discovery is
     unrelated to, and did not require, the blocked Windows-build retry in step 5 — it surfaced
     purely from verifying Task 6.1's own native build health.

  **The 4 sharp-runtime fixes are uncommitted, local-only changes in that sibling repo's working
  tree** (`git status` there shows exactly `src/System/Environment.cpp`,
  `src/System/Net/Sockets/{NetworkStream,TcpClient,UdpClient}.cpp` modified, nothing else) — they
  have **not** been committed or pushed anywhere. Since sharp-runtime is maintained by a separate
  parallel session with no version pin from this repo, **do not commit/push them without asking
  the user explicitly first**, even though the fix-and-verify steps themselves were approved.

  **To resume Task 6.2:** confirm with the user first (same reason as before — the sharp-runtime
  working tree still has uncommitted changes), then **before rebuilding for Windows, be aware
  you're about to blow away the native build's SDL3 again** (see the gotcha above) — either accept
  that and plan to `rm -rf .sdl-prebuilt` + rebuild `cmake-build-debug` natively afterward (as this
  session did), or fix the underlying `CNA_SDL_PREBUILT_ROOT` path-collision in
  `cmake/ThirdPartySDL.cmake` first (make it platform-specific, e.g. append
  `${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}` the same way `.sdl-prebuilt-emscripten` already
  gets its own distinct root) so native and cross builds stop fighting over one cache — this is
  arguably the *correct* permanent fix and worth doing before more Task 6.2 iteration, not just a
  one-off workaround. Then:
  ```bash
  cd /rv/data/development/github.com/openeggbert/cna_net
  cmake --build cmake-build-windows --target CnaTests --target cna_net_two_process_harness -j"$(nproc)"
  ```
  If it succeeds, run `wine cmake-build-windows/CnaTests.exe --gtest_filter="*Network*:*Gamer*:*ENet*:*Packet*"`
  per the approved plan's Task 6.2 verification step, and document the outcome here. If it
  surfaces *more* sharp-runtime build breaks of the same class (MSVC-only pragmas, WinAPI macro
  collisions), the same narrow fix-and-verify pattern applies — but confirm with the user before
  each round of edits to that repo, matching what happened this time.

For what a genuinely new unit of work looks like after Task 6.2 (there's no single obvious "next
task" the way there was throughout Phase 5), see section 8 — informed by `plan_net.md`'s Phase
6/7/8 sketches, needs a decision from whoever resumes / the user.

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
| **Resolved (Task 5.9, Phase 5 complete)** | Task 5.9's systematic sweep (`NetworkSessionTypePolicyTests.cpp`) found **zero regressions** across `Local`/`LocalWithLeaderboards`/`PlayerMatch`/`Ranked` — every `RealNetworkingEnabled` gate added throughout Tasks 5.1–5.8 was already correctly applied everywhere. No production code changed for this task. |
| **Note (not a bug)** | `plan_net.md`'s own, separately-written "Phase 5" checklist (5a/5b/5c, its own Task 5.1–5.18 numbering) describes a different, more elaborate design than what was actually built — an abstract `INetworkBackend` interface, host migration, a `PlayerMatch` relay server via a `CNA_NET_RELAY_HOST` env var, latency/QoS simulation. That design was **deliberately not followed** (see section 1/9: no abstract interface, ENet is the only implementation). `plan_net.md`'s checkboxes were never kept live across this whole multi-session effort — every phase's boxes are unchecked, including graphics (1–31) and GamerServices/Net, which are provably done. Don't infer project status from `plan_net.md`; `NEXT.md` is the maintained source of truth. |
| **Deviation (documented)** | `PacketWriter::Write(Color)` writes 4 bytes but `PacketReader::ReadColor()` reads 4 floats (16 bytes) — genuinely asymmetric upstream, preserved as-is. Not round-trippable through these two methods alone. |
| **Deviation (documented)** | `PacketReader(int capacity)`/`PacketWriter(int capacity)` discard the `capacity` argument — sharp-runtime's `MemoryStream` has no preallocating constructor; capacity is a pure optimization hint in .NET with no observable effect, so nothing is lost. |
| **Deviation (documented)** | `NetworkGamer::getIsLocalProperty()` is a virtual method overridden by `LocalNetworkGamer`, not a `dynamic_cast` runtime check like FNA's `this is LocalNetworkGamer` — a base class header can't `dynamic_cast` to a derived type it can't include. Behavior is identical either way. |
| **Deviation (documented)** | `NetworkSession::BeginJoin`/`BeginJoinInvited(int)`/`EndJoin`/`EndJoinInvited` pass/expect `null` for `NetworkSessionProperties` (FNA marks these exact lines `// FIXME` itself). Substituted with a default-constructed instance since this port's type isn't nullable. |
| **Deviation (documented)** | `LocalNetworkGamer::ReceiveData(PacketReader&, ...)` always returns 0 — FNA declares a length variable it never updates before returning it, even though the underlying packet write is real. Preserved as-is. |
| **Note (not a bug)** | `SignedInGamer::IsHeadset()`, `GamerServicesComponent`, and `GamerServicesDispatcher::Initialize()` have no automated tests — documented per `CHECKLIST.md`'s "classes that cannot be unit-tested" provision (SDL/Game dependency, or shared-process static-state pollution risk, respectively). Phase 5's `NetworkSession::Join()`-family limitation above joins this category. |
| **Incomplete** | `GamerJoinedEventArgs`/`GamerLeftEventArgs`/`HostChangedEventArgs`/`WriteLeaderboardsEventArgs` tests still use `nullptr` stand-ins for `NetworkGamer*` instead of real instances (both types now exist; just not yet revisited). |
| **Confirmed bug (graphics)** | `SpriteBatch` multiple `Begin()/End()` per frame on Vulkan: only the last batch renders. |
| **Suspected bug (graphics)** | `DrawUserIndexedPrimitives` typed overloads likely have the silent-return-on-missing-effect bug (not yet audited — Task 252). |
| **Real infra bug found (Task 6.2), not yet fixed** | `.sdl-prebuilt/` (see `cmake/ThirdPartySDL.cmake`, `CNA_SDL_PREBUILT_ROOT`) is keyed **only** by `CMAKE_CURRENT_SOURCE_DIR` (source checkout path), not by target platform/architecture — only Emscripten gets its own distinct `.sdl-prebuilt-emscripten`. Configuring a Windows mingw cross-build regenerates the *same* directory a native Linux build's cached `SDL3::SDL3` imported target still points at, so the native build silently starts linking Windows `.dll`s into Linux executables (`multiple definition of ...` at link time). Recovery is `rm -rf .sdl-prebuilt` + rebuild natively (as done during Task 6.2's native-build check this session — confirmed 2077/2077 restored). The durable fix is to make `CNA_SDL_PREBUILT_ROOT` platform-specific (append `${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}`), not yet done. |

---

## 6. Architecture notes

### Module map

| Layer | Location | Notes |
|---|---|---|
| XNA public API (graphics) | `include/Microsoft/Xna/Framework/…` | Must match XNA 4.0 / FNA exactly |
| XNA public API (GamerServices) | `include/Microsoft/Xna/Framework/GamerServices/` | Complete. Internal ctors → `private` + `CreateInternal()` factory |
| XNA public API (Net) | `include/Microsoft/Xna/Framework/Net/` | Complete API surface (5 enums + 18 classes), now fully wired to real networking for `SystemLink` (Phase 5, complete) — **public shapes here are a fixed point, must not change** |
| ENet backend (Phase 5, **complete**) | `include/CNA/Internal/Net/`, `src/CNA/Internal/Net/` | `ENetLibrary`/`ENetHostHandle` (5.1), `NetPacketCodec`/`NetDiscoveryProtocol` (5.2), `ENetBackend` registry/wiring (5.3), connect+handshake (5.4), `AppData` relay (5.5), disconnect/leave handling (5.6), state broadcast (5.7), LAN discovery via `ENetDiscoveryService` (5.8), and the final `NetworkSessionType` policy regression pass (5.9) all done and tested |
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
→ [Phase 5, COMPLETE] ENetLibrary/ENetHostHandle (done, 5.1) → NetPacketCodec/
  NetDiscoveryProtocol (done, 5.2) → ENetBackend + NetworkSession wiring (done, 5.3) →
  handshake/roster sync (done, 5.4) → AppData relay (done, 5.5) → disconnect handling
  (done, 5.6) → state broadcast (done, 5.7) → discovery (done, 5.8) → regression pass (done, 5.9)
→ [Phase 6/7/8, not started — see plan_net.md] platform-specific work / integration tests /
  Avatar (deferred, low priority) — no approved detailed plan exists yet for any of these,
  unlike Phase 5; needs a decision from whoever resumes on what to tackle next
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

**Phase 5 is complete — there is no single obvious "next smallest task" the way there was for
5.1–5.9.** `plan_net.md`'s original roadmap (see section 1's note on that file) sketches three
follow-on phases, none of which has an approved, detailed plan the way Phase 5 did
(`scalable-swimming-feigenbaum.md`). Whoever resumes should **check with the user** on which
direction to take before writing a detailed plan or starting implementation:

1. **Phase 6 — Platform-specific work** (`plan_net.md` Tasks 6.1–6.5). Everything built in Phase 5
   was only ever exercised on Linux/loopback in this sandboxed environment. Real multiplatform
   support (Windows/WinSock2, Web/Emscripten WebSocket adaptation with `SystemLink` disabled,
   Android NDK `INTERNET` permission, CMake platform guards) is unstarted and unverified.
2. **Phase 7 — Integration tests** (`plan_net.md`, brief sketch only). Broader than Phase 5's
   loopback unit tests — likely a genuine two-process (not one-process-two-roles) test on Linux
   per `plan_net.md` Task 6.1, which nothing in this session's test suite actually does (every
   Phase 5 loopback test uses one real `NetworkSession` + a raw ENet/socket stand-in in the *same*
   process — see section 4/5 — never two real, independent processes).
3. **Phase 8 — Avatar** (`plan_net.md` Tasks 8.1–8.13). Explicitly deferred/low-priority per this
   file's own "Do not do yet" section below; all types live under
   `Microsoft::Xna::Framework::GamerServices` (Avatar-specific enums/structs/classes), independent
   of Phase 5's networking work.

If the user has something else in mind entirely (a different feature, a bug fix elsewhere in the
codebase, revisiting one of the "Incomplete"/"What does not work yet" items in sections 2/5), that
naturally takes priority over any of the above — none of these three are blocking or urgent.

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
- **No Avatar work (Phase 8)** — deferred, low priority. Net/ENet (Phase 5) is now complete, so
  Avatar is no longer *blocked*, but it's still not the assumed next task — confirm with the user
  before starting it (see section 8).
- **No unilateral choice of Phase 6/7/8** — Phase 5 finishing doesn't imply "start the next phase
  automatically"; none of Phase 6/7/8 has an approved detailed plan, unlike Phase 5. Ask the user
  first (see section 8).

---

## 10. Resume prompt

```
Read NEXT.md first, section 4 in full before doing anything else. Phase 5 (the ENet networking
backend, Tasks 5.1-5.9) is COMPLETE — do not re-open it. Phase 6 is IN PROGRESS: Task 6.1 (Linux
two-process test) is done; Task 6.2 (Windows cross-build + Wine) is PAUSED MID-BUILD with real,
uncommitted changes sitting in the sharp-runtime sibling repo's working tree.

Current status: Phase 5 fully complete (2076/2076 tests). Phase 6 Task 6.1 also complete (+1 test,
2077/2077 on native Linux) — a genuine two-OS-process ENet loopback test
(tools/net/net_two_process_harness.cpp + tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp).

IMPORTANT — Task 6.2 (Windows) is mid-flight, read section 4 for the full blow-by-blow before
touching it:
- The Windows cross-build (cmake-build-windows/, mingw-w64 toolchain) hit a REAL, pre-existing
  build break in sharp-runtime (not in any cna_net/Net code) — 4 Windows/mingw-specific bugs in
  System/Environment.cpp and System/Net/Sockets/{NetworkStream,TcpClient,UdpClient}.cpp (WinAPI
  macro collisions, a missing include, MSVC-only pragmas GCC's -Werror rejects).
- Those 4 fixes have ALREADY BEEN MADE and verified (sharp-runtime's own Linux test suite:
  8467/8467, twice) — but they are UNCOMMITTED in the sharp-runtime working tree. Check
  `cd ../sharp-runtime && git status` before doing anything else; if those 4 files are still
  modified there, that's this exact pending state, not something to redo.
- The actual `cna_net` Windows build (CnaTests + cna_net_two_process_harness) has NOT been
  re-attempted since those fixes landed — the permission system requires fresh explicit user
  confirmation before further action on the sharp-runtime working tree, and two consecutive
  requests timed out (user away from keyboard, not declining). Ask the user to confirm, then run
  the commands in section 4's "To resume Task 6.2" block.
- Do NOT commit/push the sharp-runtime changes without asking the user explicitly first, even
  though the fix-and-verify steps themselves were already approved.

IMPORTANT constraints to know before touching Phase 5/6 test code again (both are real, permanent
properties of the shipped design, not bugs to fix):
1. Only one real NetworkSession can exist per process (activeSession_ static gate in BeginCreate/
   BeginFind) — never construct two real NetworkSession instances in a test, and never call the
   public NetworkSession::Find()/BeginFind() while a real hosted session is alive in the same
   process (it would throw immediately). See ENetBackendTests.cpp's SystemLinkSessionFixture and
   ENetDiscoveryServiceTests.cpp for the one-real-session-plus-raw-transport-stand-in pattern.
   (Task 6.1's two-process test sidesteps this differently: two real *processes*, each with its
   own real NetworkSession — that's fine, since activeSession_ is process-wide, not global.)
2. NetworkGamer::getIsHostProperty() is an unconditional-true FNA-preserved stub quirk — it does
   NOT distinguish "real ENet host" from "real ENet client." Anything gating on "am I the real
   host" (like state-change broadcast) checks ENetBackend/SessionState internals instead.
3. NetworkSession::EndJoin (and EndJoinInvited) hardcode the resulting session's type to
   NetworkSessionType::PlayerMatch regardless of what was actually joined (upstream FNA "FIXME") —
   so a session built via the public Join() never does real networking. Task 6.1's harness uses
   Create()+ConnectToHost() on both sides instead, exactly like every Phase 5 test.
See section 4/5 for the full story on all three, plus a note on why plan_net.md's own Phase 5
checklist looks entirely unchecked (it's a stale sketch, not the design that was actually built —
NEXT.md is the maintained source of truth).

Before trusting any of this: do a real `cmake --build cmake-build-debug --target CnaTests` yourself
first (native Linux — this does NOT depend on the paused Windows work at all).

Next: resolve Task 6.2 per section 4's exact resume steps (confirm with the user first, given the
sharp-runtime state), or ask the user what to work on if they'd rather set Task 6.2 aside. Do not
assume Phase 7/8 or Avatar — see section 8.
Build: cmake --build cmake-build-debug --target CnaTests
Run:   cmake-build-debug/CnaTests
```
