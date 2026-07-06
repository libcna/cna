# Plan: XNA 4.0 Net / GamerServices / Avatar — Deep-Dive Audit & Hardening

**This is a fresh plan, written 2026-07-06**, superseding the archived `plan_net_20260706.md`
(deliberately not read while writing this plan or performing the audit behind it — the goal was an
independent, unbiased re-examination of the current code against the XNA 4.0 spec, not a
continuation biased by prior framing of what's "done").

## Methodology

Four independent research passes were run against the current codebase:
1. A full line-by-line audit of `Microsoft::Xna::Framework::Net` (+ its `CNA::Internal::Net` ENet
   backend) against the FNA reference source (`/rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/Net/`).
2. A full line-by-line audit of `Microsoft::Xna::Framework::GamerServices` (excluding Avatar)
   against the FNA reference source (`.../FNA.NetStub/src/GamerServices/`).
3. A full audit of the Avatar surface — both the faithful XNA `Avatar` API (ported from the real,
   genuine Microsoft `Microsoft.Xna.Framework.Avatar.dll` reference assembly, since FNA itself has
   no real Avatar implementation) and the CNA-original "real rendering" `NOXNA`/`*EXT` extension
   layer built on top of it (`SkinnedModelEXT`, `AvatarRenderer::*EXT`, the content pipeline, the
   procedural Blender asset generator).
4. A survey of existing demo/example applications plus proposals for ~20 new ones to showcase
   currently-undemonstrated Net/GamerServices/Avatar functionality.

Each pass was told to independently re-verify its own highest-severity claims by direct code
reading (not just trust a sub-pass's report), and to explicitly rule out plausible-sounding but
false leads rather than report them as bugs. Findings are tagged `[BUG]` (a concrete, verified
behavioral defect), `[API-GAP]` (a missing member vs. the real API surface), `[TEST-GAP]` (missing
or weak test coverage), or `[AUDIT]` (needs a deeper investigation pass to confirm one way or the
other).

**Every task below that touches behavior must add or extend a test that would fail without the
fix** — this is a hard requirement per this repo's own `CLAUDE.md`, not optional polish. Build with
`cmake --build cmake-build-debug --target CnaTests`, verify with `cmake-build-debug/CnaTests`
before considering any task done, and commit per-task per this repo's own git conventions (see
`CLAUDE.md`'s "Git Commits" section — one task, one commit, reference the task ID).

---

## Phase 1 — Net: Critical Bugs (Security & Memory Safety)

These are the highest-severity findings: several are remotely triggerable (a crafted UDP packet
from any device on the LAN, or from a connected peer) and cause real memory corruption, unbounded
resource consumption, or a crashed process — not just XNA-fidelity gaps.

- [x] **Task 1.1** — Fix out-of-bounds vector write from a crafted negative property index in
  `NetDiscoveryProtocol::ReadProperties`. Confirmed: `ReadProperties` (`src/CNA/Internal/Net/NetDiscoveryProtocol.cpp`,
  around lines 38-57) reads `index = reader.ReadInt32()` directly off the wire with no lower-bound
  check. For a negative `index`, the pre-extend `while (count <= index)` loop never executes (since
  `0 <= negative` is false), so execution falls straight through to
  `NetworkSessionProperties::operator[](index)`, whose own `index >= size()` guard is also false for
  negative values — it falls through to `properties_[static_cast<std::size_t>(index)]`, casting a
  negative `int` to a huge `std::size_t`: an out-of-bounds `std::vector::operator[]` access
  (undefined behavior). This is reachable by any LAN device (or a spoofed source) sending a crafted
  `DiscoveryAnnounceMessage` to port 61190 while any local `Find()`/`FindSessions()` is in flight —
  no authentication exists on this path.
  **Fixed:** added a `if (index < 0) throw std::runtime_error(...)` guard right after reading
  `index` off the wire, before it ever touches `properties_` (matching `BinaryReader`'s own
  established `std::runtime_error`-on-malformed-input convention elsewhere in this codebase).
  Added `NetDiscoveryProtocolTest.DecodeAnnounceRejectsNegativePropertyIndex`, hand-crafting a raw
  wire payload with a negative property index (bypassing the normal `Encode()` path, which could
  never produce one) and asserting `DecodeAnnounce` throws cleanly.
  **Verified the bug is real, not theoretical:** reverted the fix and ran the new test directly —
  confirmed a real, immediate **segmentation fault (exit code 139)**, not just a benign no-op UB;
  restored the fix and reran — passes, full suite 3233/3235 (2 expected skips), no regressions.

- [ ] **Task 1.2** — Fix unbounded-allocation DoS via a huge positive property index in the same
  `ReadProperties` path (`NetDiscoveryProtocol.cpp`). A crafted `index` near `INT32_MAX` makes the
  pre-extend `while (count <= index)` loop call `Add()`/`push_back` up to ~2 billion times — a
  multi-second hang or OOM. Fix: clamp the accepted index range to something sane (e.g.
  `NetworkSessionProperties`' realistic maximum size, or a fixed small cap like 256) and reject
  anything outside it. Add a test feeding an index like `INT32_MAX` and asserting the parse is
  rejected quickly (with a wall-clock timeout in the test itself) rather than hanging.

- [ ] **Task 1.3** — Fix the dangling-pointer bug in `ENetDiscoveryService::FindSessions` that
  corrupts memory on the *next* poll after any exception mid-search. Confirmed
  (`src/CNA/Internal/Net/ENetDiscoveryService.cpp`, around lines 246/260): `currentResults_ = &results`
  (a stack-local `std::vector` inside `FindSessions`) is set before the poll loop and only reset to
  `nullptr` *after* the loop completes normally. If `PollOnce → HandleReceived → NetDiscoveryProtocol::DecodeAnnounce`
  throws mid-loop (e.g. from the very bugs in Tasks 1.1/1.2, or any other malformed packet), the
  exception unwinds past the `nullptr` reset, leaving `currentResults_` pointing at destroyed stack
  storage. The *next* `Poll()` call (invoked from every `NetworkSession::Update()`) then writes
  through the dangling pointer via `currentResults_->push_back(...)`. Fix with RAII (e.g. a scope
  guard that resets `currentResults_` to `nullptr` in a destructor, so it's reset even on exception
  unwind) rather than a plain post-loop assignment. Add a test that induces an exception mid-poll
  (a malformed packet) and then confirms a subsequent, well-formed `Poll()`/`FindSessions()` call
  behaves correctly and doesn't corrupt/crash (run under ASan if available).

- [ ] **Task 1.4** — Add exception handling around all `Decode*` calls in
  `ENetBackend::HandleReceive` and `ENetDiscoveryService::HandleReceived` so a single malformed
  packet from any connected peer (`ENetBackend.cpp`, `HandleReceive`, ~lines 356-387) or any LAN
  device (`ENetDiscoveryService.cpp`, `HandleReceived`, ~lines 117-163) cannot crash the entire
  host process. Confirmed: neither file has a single `try`/`catch` anywhere
  (`grep -n "try\|catch" src/CNA/Internal/Net/*.cpp` returns zero hits), and `BinaryReader::ReadBytes`/`ReadString`
  throw `std::runtime_error` on underflow, which currently propagates all the way up through
  `NetworkSession::Update()` into the caller's own game loop — this is a real, unauthenticated,
  remote denial-of-service against any game built on this framework (LAN discovery is
  unauthenticated broadcast UDP; connected-channel traffic requires no special payload validation
  either). Also fix the packet leak noted in the same code path: when an exception fires inside
  `PumpSession`, `enet_packet_destroy(evt.packet)` (~line 448) is skipped. Fix: wrap packet decoding
  in try/catch, log/drop the malformed packet, ensure `enet_packet_destroy` always runs (e.g. via
  RAII), and do not let a decode exception propagate out of `Update()`. Add tests feeding
  deliberately truncated/malformed packets through both paths and asserting the process keeps
  running and continues to function correctly afterward.

- [ ] **Task 1.5** — Fix `ReplyToQuery` never actually decoding the incoming `Query` message.
  Confirmed (`ENetDiscoveryService.cpp`, `HandleReceived`, ~lines 117-167): on a `Query`-tagged
  datagram, `ReplyToQuery` is called directly without ever calling `NetDiscoveryProtocol::DecodeQuery`
  — `SessionTypeFilter` is written by clients but completely ignored server-side, so a registered
  host replies to *any* `Query` datagram regardless of the claimed filter. Fix: decode the query and
  only reply if the host's own session type matches the requested filter (or document why filtering
  is intentionally not enforced, if that's a deliberate simplification — but as-is this looks like an
  unintentional gap, not a documented deviation). Add a test asserting a host of type X does not
  reply to a query explicitly filtering for type Y.

- [ ] **Task 1.6** — Validate the discovery protocol version field is actually checked. Confirmed
  (`NetDiscoveryProtocol.hpp`, `kDiscoveryProtocolVersion`): the version is written on the wire by
  `DecodeQuery`/`DecodeAnnounce` but never compared against the expected value — entirely
  decorative today. Add a check that rejects/logs a mismatched version instead of attempting to
  parse a possibly-incompatible payload as if it were current-format. Add a test with a
  mismatched version byte asserting the packet is rejected cleanly.

---

## Phase 2 — Net: Correctness Bugs

- [ ] **Task 2.1** — Fix `NetworkSession`'s event dispatch always passing `nullptr` as `sender`
  instead of the session itself. Confirmed: `GamerJoined.Raise(nullptr,...)` /
  `GamerLeft.Raise(nullptr,...)` / `HostChanged.Raise(nullptr,...)` / `GameStarted.Raise(nullptr,...)` /
  `GameEnded.Raise(nullptr,...)` / `SessionEnded.Raise(nullptr,...)` (`NetworkSession.cpp`, ~lines
  294-317), and the `GamerJoined.SetReplayHook` closure also invokes `handler(nullptr, ...)`. Root
  cause: `NetworkSession` doesn't inherit `System::Object`, so there's no `this`-as-`Object*` to
  pass. Any game code reading the `sender` parameter of a `NetworkSession` event handler gets
  `nullptr` always, unlike real XNA where `sender` is the raising `NetworkSession` instance. Fix:
  make `NetworkSession` inherit `System::Object` (with a `NOXNA GetTypeName()` override per
  `CHECKLIST.md`'s convention for `System::Object`-derived concrete classes) and pass `this`
  everywhere events are raised. Add tests asserting `sender` is the actual session instance for at
  least `GamerJoined` and one other event.

- [ ] **Task 2.2** — Fix `NetworkSession::RemoveGamer` never removing a departing gamer from
  `localGamers_`. Confirmed (`NetworkSession.cpp`, ~lines 407-446): `isLocal` is computed by
  scanning `localGamers_`, and the gamer is removed from `remoteGamers_`/`allGamers_` and added to
  `previousGamers_` — but `localGamers_.Remove(gamer)` is never called. Reachable in production via
  `ENetBackend.cpp`'s `RemoveGamer(locals[0], HostEndedSession)` call (~line 299). This breaks the
  `AllGamers == LocalGamers ∪ RemoteGamers` invariant: a removed local gamer keeps appearing in
  `getLocalGamersProperty()` forever. Fix: remove from `localGamers_` too when `isLocal` is true.
  Add a test that adds a local gamer, removes it via `RemoveGamer`, and asserts it's gone from
  `LocalGamers` too (not just `AllGamers`).

- [ ] **Task 2.3** — Fix `NetworkSession::AddLocalGamer` never raising `GamerJoined`. Confirmed:
  `AddLocalGamer` only does `localGamers_.Add(adding); allGamers_.Add(adding);` with no event
  enqueue, unlike `AddRemoteGamer` (~lines 396-405), which explicitly enqueues a
  `NetworkEventType::GamerJoin` event. This is the same class of bug as the just-fixed Task 12.3
  (`GamerJoined` replay-on-subscribe), but for a still-broken code path: a handler already
  subscribed before `AddLocalGamer` runs never learns about the newly-added local gamer at all (no
  replay, no queue). Fix: enqueue a `GamerJoin` event the same way `AddRemoteGamer` does. Add a test
  subscribing to `GamerJoined` before calling `AddLocalGamer` and asserting it fires.

- [ ] **Task 2.4** — Fix the local-gamer `Id`-collision bug after remove-then-add churn. Confirmed:
  `NetworkSession`'s constructor assigns sequential local-placeholder ids (`nextLocalId` starting at
  0), but `AddLocalGamer` derives its new gamer's id from the *live* `allGamers_.getCountProperty()`
  at the time of the call. Since `RemoveGamer` shrinks that count with no separate monotonic
  counter, a remove-then-add sequence can hand out a colliding `Id` — e.g. 3 gamers join with ids
  0,1,2; gamer 1 leaves (count now 2); calling `AddLocalGamer` again assigns the new gamer id
  `2` too, colliding with the still-present gamer that already owns id 2 — corrupting
  `FindGamerById`. Fix: track a real monotonic per-session id counter (separate from any live
  collection's size) for locally-assigned placeholder ids, used consistently by both the
  constructor and `AddLocalGamer`. Add a test: add 3 gamers, remove the middle one, add a new one,
  assert `FindGamerById` resolves every remaining/new gamer to a distinct, correct instance (no
  collision) — this test should fail without the fix (currently masked; see Task 6.1's related
  test-gap task).

- [ ] **Task 2.5** — Add capacity enforcement to `NetworkSession::AddRemoteGamer` against
  `MaxGamers`/`PrivateGamerSlots`. Confirmed (`NetworkSession.cpp`, ~lines 396-405):
  `AddRemoteGamer` unconditionally adds any remote gamer regardless of `maxGamers_`/`privateGamerSlots_`,
  silently violating the documented "maximum players allowed" contract. Decide and implement the
  correct XNA-faithful behavior (check FNA's own equivalent path, if any, for the expected
  behavior/exception type when a session is full) and add a test asserting a session at capacity
  rejects (or otherwise correctly handles) an additional remote gamer.

- [ ] **Task 2.6** — Investigate and either implement or explicitly document-as-unsupported real
  host migration. Confirmed dead/unwired end-to-end: `AllowHostMigration`'s setter
  (`NetworkSession.cpp`, ~lines 185-186) is plain storage never read anywhere in `ENetBackend.cpp`;
  `NetworkEventType::HostChange` is never enqueued anywhere in the repo; `ENetBackend::HandleDisconnect`
  (~lines 288-303) unconditionally ends the session the instant the host peer disconnects, with no
  election logic at all; the wire tag `0x05` reserved for `HostChangeBroadcast` in
  `NetPacketCodec.hpp` (~line 31) is explicitly commented "not implemented"; `NetworkGamer::IsHost`
  is never recomputed on any migration event. This means `AllowHostMigration = true` currently does
  nothing — a session always ends when its host disconnects, regardless of the flag. Either (a)
  implement real host election (elect a remaining gamer, fire `HostChanged`, update wire routing
  state) gated on the flag, or (b) if implementing this is out of scope for now, make the code
  honest: e.g. have the setter throw `NotSupportedException` if set to `true`, or add a clear
  doc-comment + a regression test asserting the current (unsupported) behavior, so a caller can't
  be misled into thinking migration works. Whichever direction is chosen, add a test proving the
  actual (documented) behavior.

- [ ] **Task 2.7** — Enforce `AllowJoinInProgress` in `ENetBackend::HandleClientHello`. Confirmed
  (`ENetBackend.cpp`, ~lines 132-177): incoming `ClientHello` is unconditionally accepted regardless
  of `sessionState_`/`AllowJoinInProgress` — a host with `AllowJoinInProgress = false` still
  silently accepts new players mid-`Playing` state. Fix: reject (or queue, per whatever the correct
  XNA-faithful behavior is) a join attempt when the session is `Playing` and `AllowJoinInProgress`
  is false. Add a test.

- [ ] **Task 2.8** — Fix `LocalNetworkGamer::ReceiveData(vector&, int offset, sender)` writing past
  the end of the caller's buffer. Confirmed (`LocalNetworkGamer.cpp`, ~lines 44-47):
  `int len = std::min(packet.size(), data.size());` ignores `offset` entirely, then
  `std::copy(packet.begin(), packet.begin()+len, data.begin()+offset)` — concrete repro:
  `data.size()==10`, `offset==5`, incoming packet `size()==8` → `len=min(8,10)=8` → writes
  `data[5..13)`, 3 elements past the end of a 10-element buffer (undefined behavior). FNA's
  `Array.Copy` throws a catchable `ArgumentException` for the equivalent misuse. Fix: compute the
  correct bound as `std::min(packet.size(), data.size() - offset)` (with a check that `offset <=
  data.size()` first, throwing an appropriate exception otherwise). Add a test with a real
  non-empty queue and a non-zero offset that would overflow without the fix (run under ASan if
  available to prove no OOB write).

- [ ] **Task 2.9** — Add bounds validation to both `LocalNetworkGamer::SendData(offset, count,
  ...)` overloads. Confirmed (`LocalNetworkGamer.cpp`, ~lines 96-98, 116-118):
  `std::vector<bytecs> mem(data.begin()+offset, data.begin()+offset+count)` with no check that
  `offset + count <= data.size()` — undefined behavior where FNA would throw. Add the missing
  bounds check (throwing the FNA-matching exception type) and a test exercising an
  out-of-range `offset+count` combination.

- [ ] **Task 2.10** — Fix `NetworkSessionProperties`'s non-const `operator[]` silently
  auto-appending on out-of-range *reads*, not just writes. Confirmed: the non-const `operator[]`
  unconditionally does `if (index >= size()) { push_back(nullopt); return back(); }` — since C++
  can't distinguish get-intent from set-intent through a plain `operator[]`, *any* out-of-range
  access through a mutable reference (including a bare read with no assignment) silently grows the
  list instead of throwing like FNA's getter always does. Only the const accessor (using `.at()`)
  throws correctly. This is a real semantic divergence beyond the already-documented "write-only
  auto-grow" quirk. Decide the correct fix (e.g. a proxy-object pattern to distinguish read vs.
  write intent, matching `PropertyDictionary`'s similar issue in Phase 7) and add a test for a
  bare out-of-range read through a non-const reference.

- [ ] **Task 2.11** — Fix the wire-id wraparound/collision bug in `ENetBackend`'s
  `SessionState::NextWireId`. Confirmed: `NextWireId` is a `uint8_t` (`ENetBackend.cpp`, ~line 50),
  incremented via `state.NextWireId++` in `AssignWireId` and never reclaimed/decremented on a
  gamer leaving. A long-running lobby with churn (not 256 *simultaneous* gamers, just 256
  cumulative joins over the session's life) silently reassigns an in-use wire id, corrupting
  `HandleAppData`'s wire-id-based routing for whichever gamer previously owned that id. Fix: either
  reclaim ids on leave (a free-list) or widen the id type with a documented wraparound-avoidance
  scheme; add a regression test simulating 256+ join/leave cycles asserting no misrouting occurs.

- [ ] **Task 2.12** — Fix list-length wire fields silently truncating past 255 entries. Confirmed
  pattern in `NetPacketCodec::Encode` (~lines 60, 91, 97, 137, 167): `.size()` is cast down to a
  single `bytecs` for `LocalGamertags`/`AssignedWireIds`/`ExistingRoster`/`NewGamers`/`WireIds`,
  while the accompanying loop still serializes the *full*, untruncated collection — if any of these
  ever exceeds 255 entries, the written count wraps (e.g. 256→0) while every element is still
  written, desynchronizing the decoder. Low likelihood given `NetworkSession::MaxSupportedGamers ==
  31`, but nothing ties the wire format to that invariant. Add an assertion/static bound check tying
  the wire format's capacity to `MaxSupportedGamers`, and a test (or a `static_assert`-style
  compile-time check plus a runtime guard) documenting/enforcing the real limit.

- [ ] **Task 2.13** — Fix `SendAppData` silently dropping packets sent before the ENet handshake
  completes. Confirmed (`ENetBackend.cpp`, ~lines 489-536): a `GamerToWireId` lookup miss does a
  bare `return;` — a `SendData` call issued immediately after `Join()`/`ConnectToHost()` (before at
  least one `Update()` call pumps the `ClientHello`/`ServerWelcome` round-trip) is silently
  discarded with no error, retry, or queuing. Decide whether to queue-and-flush-once-ready, or at
  least surface this discard somehow (a NOXNA diagnostic hook, a debug log) rather than a totally
  silent drop. Add a test proving the current (fixed) behavior explicitly, whichever direction is
  chosen.

- [ ] **Task 2.14** — Add graceful peer disconnect on session teardown. Confirmed:
  `ENetBackend::TeardownSession` destroys the ENet host with no prior `enet_peer_disconnect` call
  for still-connected peers; `ENetHostHandle::Disconnect()` is confirmed never called from
  production code (only test fixtures use it) — remote peers wait out ENet's internal timeout
  instead of receiving an immediate clean disconnect notification. Fix: call
  `enet_peer_disconnect`/flush before destroying the host in `TeardownSession`. Add a test asserting
  a remote peer receives a disconnect event promptly (not via timeout) when the local session is
  disposed.

- [ ] **Task 2.15** — Investigate and fix `NetworkSession::Join()`'s real handshake being
  unreachable from the public API. Confirmed: `BeginJoin`/`EndJoin` hardcode
  `NetworkSessionType::PlayerMatch` rather than deriving it from the `AvailableNetworkSession` being
  joined (an acknowledged in-source FIXME), and `ENetBackend::RealNetworkingEnabled` only returns
  `true` for `SystemLink` — so every session produced by the real public `Join()` entry point has
  real networking *disabled*. Only tests that call `ConnectToHost` directly (bypassing `Join()`)
  exercise the real handshake at all. Fix: derive the correct `NetworkSessionType` from the
  `AvailableNetworkSession` (or otherwise resolve the FIXME correctly) so `Join()` produces a
  session with real networking enabled when appropriate. Add a test that calls the real public
  `Join()` (not `ConnectToHost` directly) and asserts real networking actually activates.

---

## Phase 3 — Net: Memory Ownership Model (cross-cutting)

The `Net` namespace currently has no ownership model at all for its heap-allocated objects — every
class is designed as if a GC were present. This phase is 3 related, cross-cutting leak fixes; a
single design decision should drive all three (e.g. adopt `std::unique_ptr`/`std::shared_ptr`
consistently, or introduce a small internal pool/arena with explicit lifetime tied to the owning
`NetworkSession`, documented clearly either way).

- [ ] **Task 3.1** — Fix the permanent leak of every `NetworkGamer`/`LocalNetworkGamer`. Confirmed:
  `NetworkSession.cpp` (~lines 101, 110, 330) and `ENetBackend.cpp` (~lines 144, 198, 219) all `new`
  gamer objects that are only ever stored in `GamerCollection<T>`'s non-owning raw
  `std::vector<T*>`. Neither `NetworkSession::Dispose()` (~lines 232-243) nor `RemoveGamer`
  (~lines 407-446) ever `delete`s anything (`grep -rn "delete.*Gamer" src/` returns zero hits).
  Every join/leave cycle over a session's life permanently leaks one object. Decide and implement a
  real ownership model (see phase intro) and add a test that (where feasible, e.g. via a
  test-only allocation counter, or at minimum via valgrind/ASan leak detection in CI) proves gamers
  are freed on session disposal and on `RemoveGamer`.

- [ ] **Task 3.2** — Fix the permanent leak of every `NetworkSessionAction` across the `Begin*`/`End*`
  async family. Confirmed: `new NetworkSessionAction(...)` at `NetworkSession.cpp` (~lines 524, 554,
  580, 671, 697, 756, 834, 853); every `End*` (e.g. `EndCreate` ~lines 587-606, `EndFind` ~lines
  704-722) only does `activeAction_ = nullptr;` with no prior `delete`. Fix as part of the same
  ownership-model decision as Task 3.1, and add a test/leak-check proving no leak across a full
  `Begin*`→`End*` cycle.

- [ ] **Task 3.3** — Fix `NetworkSession` objects themselves never being freed. Confirmed: no code
  path in `src/` or `tests/` ever `delete`s a `NetworkSession*` — `Dispose()` only flips
  `isDisposed_` to `true`. Decide the correct ownership contract (does the caller own the returned
  pointer and must `delete` it after `Dispose()`? should `Dispose()` itself free it, matching a
  more RAII-friendly convention?) — document it clearly in the class's own doc comments — and fix
  accordingly. Add a test/leak-check proving the chosen contract actually results in no leak when
  followed correctly.

---

## Phase 4 — Net: API Gaps

- [ ] **Task 4.1** — Wire up `NetworkGamer::RoundtripTime` to real ENet per-peer RTT data. Confirmed
  permanently dead: backed by `roundtripTime_`, default-constructed and never assigned anywhere
  (`grep -rn "RoundtripTime"` finds zero writes) — ENet natively tracks real per-peer round-trip
  time that's simply never surfaced. Wire it up from the underlying `ENetPeer`'s own RTT tracking.
  Add a test (over a real two-peer ENet connection) asserting `RoundtripTime` becomes non-zero
  after some real traffic.

- [ ] **Task 4.2** — Make `QualityOfService` reflect real measurements for real `SystemLink`
  sessions instead of always being a hardcoded stub. Confirmed:
  `QualityOfService::CreateInternal()` takes zero parameters and always yields `IsAvailable=true`
  plus all-zero rates; the only production call site (`ENetDiscoveryService.cpp`, ~line 159) invokes
  it with no arguments when building a real `AvailableNetworkSession` from a genuine LAN discovery
  reply. Wire real bandwidth/RTT measurements through (ties into Task 4.1 for RTT). Add a test
  asserting a `QualityOfService` built from a real discovered session reflects real measured
  values, not the hardcoded stub.

- [ ] **Task 4.3** — Implement real effect for `NetworkSession.SimulatedLatency`/`SimulatedPacketLoss`.
  Confirmed: `grep` finds no reference to either property name anywhere in `CNA::Internal::Net`
  outside `NetworkSession`'s own plain storage — no delay queue or synthetic packet-drop logic
  exists anywhere in `ENetBackend`/`ENetHostHandle`. Either implement real simulated
  latency/packet-loss (e.g. via ENet's own `enet_peer_throttle_configure` and/or an internal delay
  queue) or explicitly document these properties as currently non-functional placeholders. Add a
  test proving the actual (real or explicitly-documented-as-inert) effect.

- [ ] **Task 4.4** — Add `ReadBytes(int count)` (array-returning) and `Write(char)`/`ReadChar()` to
  `sharp-runtime`'s `System::IO::BinaryReader`/`BinaryWriter`. Confirmed gap vs. FNA's `PacketReader`
  (which inherits `System.IO.BinaryReader`) that ordinary XNA game code commonly relies on for raw
  byte-block reads. This is a `sharp-runtime` change — per this repo's own `CLAUDE.md` extension
  rule, add it there first, then verify `PacketReader`/`PacketWriter` correctly inherit/expose it.
  Coordinate with whoever drives `sharp-runtime` (this repo's own convention: never modify existing
  `sharp-runtime` files without asking the user first, for every commit). Add tests in both
  `sharp-runtime` and this repo's `PacketReaderWriterTests.cpp`.

- [ ] **Task 4.5** — Add a `CopyTo` equivalent to `NetworkSessionProperties` (FNA's
  `ICollection<int?>.CopyTo(array, index)`). Confirmed root cause one level down: `sharp-runtime`'s
  generic `ICollection<T>` interface never declares `CopyTo` at all (unlike the non-generic
  `ICollection`, which does). Decide whether to add `CopyTo` to `sharp-runtime`'s generic
  `ICollection<T>` (coordinate per the same rule as Task 4.4) or implement it directly on
  `NetworkSessionProperties` without going through the generic interface. Add a test.

- [ ] **Task 4.6** — Extend `NetworkGamer::IsHost` to be correct for remote gamers representing the
  actual host machine, as seen from a non-host client. Confirmed self-documented gap
  (`NetworkGamer.hpp`, ~lines 81-90): a remote gamer's `IsHost` is currently always `false`, because
  the wire roster (`RosterEntry`) carries no host flag. Fix requires extending `NetPacketCodec`'s
  roster message format to carry a host flag, and wiring it through `HandleServerWelcome`/`HandleGamerJoinBroadcast`
  in `ENetBackend.cpp` (ties directly into Task 2.6's host-migration work — do them together or in
  sequence). Add a test (over a real two-peer connection) asserting a client correctly sees
  `IsHost == true` on the remote gamer representing the actual host.

---

## Phase 5 — Net: Test Coverage

- [ ] **Task 5.1** — Add a test for `NetworkSession::AddLocalGamer`'s success (non-throwing) path.
  Confirmed every currently-constructible test session already has `maxLocalGamers_` pinned to zero
  spare capacity (`NetworkSessionTests.cpp`, ~lines 235-241), so only the throw-at-limit path is
  exercised — masking Tasks 2.3 and 2.4 entirely. Construct a session with genuine spare local-gamer
  capacity (may require a new, safe construction path — investigate whether one exists or needs
  adding) and test that `AddLocalGamer` succeeds, raises `GamerJoined`, and assigns a correct,
  non-colliding `Id`.

- [ ] **Task 5.2** — Add a test for `LocalNetworkGamer::ReceiveData`'s offset-taking overload with a
  real non-empty queue and a non-zero offset. Confirmed only the empty-queue early-return and the
  `offset==0` delegating overload are currently exercised (`NetworkSessionTests.cpp`, ~lines
  533-538) — this is exactly the gap that let Task 2.8's bug ship undetected.

- [ ] **Task 5.3** — Add a boundary/overflow test for `LocalNetworkGamer::SendData`'s
  offset+count overload. Confirmed the existing `SendDataWithOffsetAndCount` test
  (`NetworkSessionTests.cpp`, ~lines 555-559) only exercises a safely in-range case — this is
  exactly the gap that let Task 2.9's bug ship undetected.

- [ ] **Task 5.4** — Add ordinal-value assertions to `NetEnumsTests.cpp` for `SendDataOptions` and
  `NetworkSessionType` specifically (not just tautological self-equality checks). Confirmed both
  enums are serialized as raw bytes on the wire (`static_cast<SendDataOptions>(reader.ReadByte())` /
  `static_cast<NetworkSessionType>(reader.ReadByte())` in `NetPacketCodec.cpp`/`NetDiscoveryProtocol.cpp`)
  — a silent enum reordering would desync wire compatibility with nothing in the test suite to catch
  it.

- [ ] **Task 5.5** — Add a test for `NetworkSessionJoinException`'s protected serialization
  constructor (`SerializationInfo&`, `StreamingContext&`), currently uncovered.

- [ ] **Task 5.6** — Fix `NetEventArgsTests.cpp` exercising `GamerJoinedEventArgs`/`GamerLeftEventArgs`/
  `HostChangedEventArgs`/`WriteLeaderboardsEventArgs` exclusively with `nullptr` gamer pointers,
  which can't catch a constructor-argument-order bug (e.g. `HostChangedEventArgs` accidentally
  swapping `oldHost`/`newHost`). Rewrite using two distinct non-null sentinel pointers and assert
  each property returns the correct, distinguishable one.

- [ ] **Task 5.7** — Add a test proving `AvailableNetworkSessionCollection::Dispose()`'s actual,
  documented deviation from FNA (FNA clears its collection on `Dispose()`; this port intentionally
  doesn't). Confirmed the existing test (`AvailableNetworkSessionTests.cpp`, ~lines 96-102) only
  checks `IsDisposed` becomes `true`, never that `Count`/contents are unchanged afterward — the
  actual documented behavior has zero regression coverage.

- [ ] **Task 5.8** — Add a test exercising `AvailableNetworkSession::operator==` through
  `AvailableNetworkSessionCollection`'s `IndexOf`/`Contains` (the entire reason the operator was
  added, per its own doc comment), not just as an ad hoc standalone equality check.

- [ ] **Task 5.9** — Add a test proving `QualityOfService`/`SessionProperties` are excluded from
  `AvailableNetworkSession::operator==`, matching the header's own doc comment (which explicitly
  states this) — currently the only equality test varies `CurrentGamerCount`, never these two
  fields.

- [ ] **Task 5.10** — Add thorough `PacketReader`/`PacketWriter` round-trip tests beyond math types:
  `Byte`/`SByte`/`Int16`/`UInt16`/`UInt32`/`Int64`/`UInt64`/`String`, boundary values (`Int64`
  min/max), multi-byte/Unicode string content, and EOF/underrun behavior verified through
  `PacketReader` itself (not just relying on `sharp-runtime`'s own separate test suite for the
  underlying `BinaryReader`/`BinaryWriter`).

- [ ] **Task 5.11** — Add negative-capacity tests for `PacketReader(int)`/`PacketWriter(int)`. Real
  .NET's `MemoryStream(int capacity)` throws `ArgumentOutOfRangeException` for a negative value
  regardless of whether preallocation is actually implemented — confirm/fix cna's constructors to
  match, and add the test.

- [ ] **Task 5.12** — Create a dedicated `LocalNetworkGamerTests.cpp` file. Confirmed its ~14 test
  cases currently live embedded in `NetworkSessionTests.cpp` (~lines 504-605), contrary to
  `CHECKLIST.md`'s per-class test-file convention. Move them (no behavior change, pure test-file
  reorganization).

- [ ] **Task 5.13** — Add a multi-peer (3+ node) integration test. Confirmed every existing
  `ENetBackendTests.cpp` scenario is a single host + at most one client — the fan-out/relay logic
  built specifically for >1 peer (`HandleClientHello`'s broadcast-to-other-peers loop, ~lines
  166-176; `HandleDisconnect`'s remaining-peers broadcast, ~lines 330-337; `BroadcastStateChange`'s
  per-peer loop, ~lines 560-563; `HandleAppData`'s host-relay-between-two-other-peers branch, ~lines
  258-267, the single most complex routing logic in the file) is never exercised with a genuine
  third connected party. Add a real 3-peer test (or more) covering at minimum the relay branch.

- [ ] **Task 5.14** — Add adversarial/malformed-packet tests for `NetPacketCodecTests.cpp` and
  `NetDiscoveryProtocolTests.cpp`. Confirmed neither file currently exercises a negative/oversized
  property index (Tasks 1.1/1.2) or a truncated buffer (Task 1.4) — none of the highest-severity
  bugs found in this audit has any regression coverage today. Add tests for each, asserting clean
  rejection (post-fix) rather than crash/corruption.

- [ ] **Task 5.15** — Add error-path tests for `ENetHostHandle`: `Connect()` with an unresolvable
  hostname (should throw a clear, catchable exception), `Send()`/`Broadcast()` targeting zero
  connected peers, and the (currently untested) path where `enet_packet_create` returns null.

- [ ] **Task 5.16** — Add a regression test for the wire-id wraparound/collision scenario fixed in
  Task 2.11 — e.g. 256+ join/leave cycles on one `SessionState` asserting no misrouting.

- [ ] **Task 5.17** — Add a test proving `SimulatedLatency`/`SimulatedPacketLoss` have the effect
  implemented (or explicitly documented as absent) in Task 4.3.

- [ ] **Task 5.18** — Add a dedicated test file for `ENetLibrary` (`EnsureInitialized()`'s
  double-init idempotency currently only exercised incidentally by other tests, never directly
  asserted).

- [ ] **Task 5.19** — Add ordinal-value/spelling-lock tests for `WriteArbitratedLeaderboard`/
  `WriteUnarbitratedLeaderboard`/`WriteTrueSkill` events — currently zero test coverage, not even a
  subscribe-smoke-test, even though they're correctly never raised (matching FNA).

---

## Phase 6 — Net: Further Investigation

- [ ] **Task 6.1** — Investigate and fix `activeAction_` getting permanently stuck if the
  `NetworkSession` constructor throws mid-`EndCreate`/`EndJoin`/`EndJoinInvited`. Confirmed:
  `activeAction_` is only cleared *after* successful construction — a throw (e.g. from an empty
  global-`SignedInGamers` list access) leaves every subsequent `Begin*` call throwing
  `InvalidOperationException` forever, for the rest of the process's life.
  `NetworkSessionTests.cpp` (~lines 317-331) explicitly documents *avoiding* this landmine in its
  own tests rather than fixing the underlying issue. Decide the correct fix (clear `activeAction_`
  in a `catch`/RAII guard around the constructor call) and add a test proving a failed
  `Create()`/`Join()` doesn't permanently brick subsequent calls.

- [ ] **Task 6.2** — Audit and fix (or explicitly accept and document) the pointer-identity gamer
  matching in `LocalNetworkGamer.cpp` (`gamer == packet.Gamer`, ~lines 51-52) — a faithfully-preserved
  FNA "FIXME: bad equality check." Confirmed this poses lower risk than it might initially appear
  (since `GamerCollection<T>` stores raw non-owning `T*` and container reallocation never moves the
  pointee), but the risk model changes once Task 3.1's leak fix lands (currently nothing is ever
  freed, so no address can be coincidentally reused — fixing the leak reintroduces a theoretical
  use-after-free/aliasing risk here). Re-evaluate this specifically once Task 3.1 lands, and fix or
  formally document as an accepted, tracked risk.

- [ ] **Task 6.3** — Investigate and fix the partial-failure state possible in
  `ENetBackend::StartHosting`. Confirmed: if `ENetDiscoveryService::RegisterHost` throws (e.g. via
  `EnsureSocket`'s bind/create failure) *after* `sessions.emplace(...)` has already succeeded, the
  session is left registered with a live, bound ENet host but never registered for LAN discovery —
  a real host that's permanently undiscoverable via `Find()`, with no rollback. Add proper
  transactional rollback (or reorder the operations so failure can't leave an inconsistent state)
  and a test.

- [ ] **Task 6.4** — Investigate whether `ENetBackend::Sessions()`'s function-local static map and
  `ENetDiscoveryService`'s file-static `registeredHost_`/`socket_`/`currentResults_` need real
  synchronization, and document the thread-safety contract explicitly either way. Confirmed
  currently safe only because `PumpSession`/`Poll()` are exclusively driven from
  `NetworkSession::Update()` in every observed call path (no `std::thread`/`mutex`/`atomic` anywhere
  in the module) — a future multi-threaded `Update()` caller would race silently. Add an explicit
  doc comment on the thread-safety contract (single-threaded-only, must be called from the same
  thread every time) if that remains the design, or add real synchronization if multi-threaded use
  is ever intended.

- [ ] **Task 6.5** — Investigate whether `SO_REUSEADDR` (used for the shared discovery UDP port
  61190, which two independent OS processes both bind in `TwoProcessLoopbackTest.cpp`) actually
  guarantees reliable delivery of unicast datagrams to multiple same-port listeners on all target
  platforms (Linux/Windows/Web/Android), or whether `SO_REUSEPORT` (or a different design) would be
  more correct. Document the finding either way (this "apparently works today" per existing test
  passes, but isn't explicitly audited/documented as reliable).

- [ ] **Task 6.6** — Investigate and fix (in `sharp-runtime`, coordinating per that repo's own
  modification rule) the endianness asymmetry between `BinaryWriter::Write(Single/double)` (raw
  native-order `memcpy`) and `BinaryReader::ReadSingle/ReadDouble` (explicit little-endian
  reconstruction). Confirmed unreachable through this repo's own ENet wire messages today (none use
  float/double fields), but a real latent bug for ordinary game code writing `Vector2`/`Vector3`/etc.
  through `PacketWriter` on a big-endian host. Cross-cutting into `sharp-runtime`.

- [ ] **Task 6.7** — Investigate the possible null-dereference in `ReplyToQuery` if it's ever reached
  before a host gamer exists (`WireGamertagFor(registeredHost_->getHostProperty())` has no
  null-check). Confirmed unreachable in practice today given current call ordering
  (`StartHosting` always runs after a host gamer is constructed), but add either a defensive
  null-check or an explicit assertion/test documenting the invariant that makes this safe, so a
  future refactor can't silently reintroduce the risk.

- [ ] **Task 6.8** — Investigate reducing the repeated `Flush()` calls after every single `Send()`
  inside per-peer broadcast fan-out loops (e.g. `HandleClientHello`'s `GamerJoinBroadcastMessage`
  fan-out, ~lines 168-176). Not a correctness bug, but an avoidable syscall-per-peer cost if
  fan-out ever scales past a handful of peers — batch the sends and flush once per broadcast instead.

- [ ] **Task 6.9** — Re-verify `LeaderboardReader`-adjacent doc-comment accuracy: confirm the
  `HasLeftSession` doc-comment in `NetworkGamer.hpp` (~line 30) correctly describes FNA's actual
  access modifier (`{ get; private set; }`, not `internal`) — a minor doc-accuracy fix, not a
  behavior change, but worth correcting so future readers don't misunderstand what CNA's `NOXNA
  SetHasLeftSession()` extension is actually restoring vs. adding.

---

## Phase 7 — GamerServices: Bugs

- [ ] **Task 7.1** — Fix `SignedInGamer::GetAchievements()` hanging forever once GamerServices is
  actually initialized — the same class of bug as the already-fixed `NetworkSession` hang (Task
  12.1 in the prior plan). Confirmed (`SignedInGamer.cpp`, ~lines 88-111): `BeginGetAchievements()`
  never marks its `GamerAction` completed (unlike `BeginAwardAchievement`, ~line 79, which
  explicitly does `statStoreAction_->setIsCompletedProperty(true)`). The synchronous wrapper's
  `while (!result->getIsCompletedProperty()) { if (!GamerServicesDispatcher::UpdateAsync())
  statReceiveAction_->setIsCompletedProperty(true); }` only terminates when `UpdateAsync()` returns
  `false` — once a real `GamerServicesComponent` exists (`IsInitialized == true`, the normal case
  for any real game), `UpdateAsync()` returns `true` forever and the loop spins at 100% CPU
  indefinitely. The only existing test (`SignedInGamerTest.GetAchievementsReturnsEmptyCollection`)
  passes only because the test suite deliberately never calls `GamerServicesDispatcher::Initialize()`
  (documented in `GamerServicesServiceTests.cpp`, ~lines 16-20), making this bug completely
  invisible to the current test suite. Fix the same way `NetworkSession.cpp` was fixed: mark the
  action pre-completed in `BeginGetAchievements` (there's no real deferred work to wait on). Add a
  regression test analogous to `tests/CNA/Internal/Net/GamerServicesDispatcherHangRegressionTest.cpp`
  that runs in a genuinely separate process with `Initialize()` actually called, proving
  `GetAchievements()` returns promptly instead of hanging.

- [ ] **Task 7.2** — Audit every other `SignedInGamer` `Begin*` method (`BeginGetFriends`,
  `BeginGetProfile`, and any others in the same file) for the identical "never marks itself
  completed" pattern found in Task 7.1 — Task 7.1's fix was scoped to `GetAchievements` specifically,
  but the same root cause (an async action never calling `setIsCompletedProperty(true)`) may recur
  in sibling methods in the same file. Fix any found, and add the same out-of-process regression
  test pattern for each.

- [ ] **Task 7.3** — Fix `GameDefaults`'s constructor initializing `GameDifficulty`/`ControllerSensitivity`
  to the wrong stub default values. Confirmed: FNA's `internal GameDefaults()` constructor is
  genuinely empty (an upstream "FIXME: This is one huge joke" — the field is just left at C#'s
  `default(T)`, which is the enum's ordinal-0 value). `GameDifficulty`'s ordinal-0 value is `Easy`
  (order: `Easy=0, Normal=1, Hard=2`); `ControllerSensitivity`'s ordinal-0 value is `Low` (order:
  `Low=0, Medium=1, High=2`). `GameDefaults.hpp` (~lines 108-109) instead hardcodes
  `gameDifficulty_{GameDifficulty::Normal}` and `controllerSensitivity_{ControllerSensitivity::Medium}`
  — both wrong (by contrast, `racingCameraAngle_{RacingCameraAngle::Back}` on ~line 117 is already
  correct since `Back` is ordinal 0 there). The existing test (`GamerServicesDataTests.cpp`, ~lines
  144-145) asserts the *wrong* values too, self-consistently hiding the bug. Fix both field
  initializers to their correct ordinal-0 values and correct the two `EXPECT_EQ` lines in the
  existing test to match the fix (not to keep asserting the old, wrong values).

- [ ] **Task 7.4** — Fix `PropertyDictionary`'s mutable `operator[]` auto-vivifying missing keys
  instead of throwing, matching the const overload's already-correct behavior. Confirmed: FNA's
  indexer getter is `return dictionary[key];`, which throws `KeyNotFoundException` for a missing
  key via `Dictionary<TKey,TValue>`. The const overload correctly mirrors this via `.at(key)`
  (throws `std::out_of_range`), but the non-const overload uses `dictionary_[key]`
  (`std::map::operator[]`), which silently default-constructs and inserts an empty `std::any` for a
  missing key instead of throwing, and inflates `Count` as a side effect. Any read through a
  non-const `PropertyDictionary&` (the common case) silently diverges from FNA. Fix: separate
  get-context (no auto-insert, throws on missing key) from set-context (insert-on-write) — e.g. via
  a proxy-object return type, or by providing a distinct read accessor and having assignment go
  through a different path. Add a test: reading a missing key through a non-const reference should
  throw, not silently insert and inflate `Count`.

- [ ] **Task 7.5** — Fix `GamerServicesDispatcher::Initialize()` leaking the previous 4
  `SignedInGamer` objects when called a second time. Confirmed: `Initialize()` heap-allocates 4 new
  `SignedInGamer*` every call, wraps them in a new `SignedInGamerCollection`, then calls
  `Gamer::setSignedInGamersProperty(...)`. That setter deletes the old `SignedInGamerCollection`
  before replacing the pointer, but `GamerCollection<T>` has no destructor that deletes its
  contained `T*` elements — only the vector's own storage is freed. The 4 `SignedInGamer` objects
  from the first call become unreachable and leaked on any second `Initialize()` call. (FNA itself
  has the identical no-op-if-already-initialized gap — this is a C++-ownership-model issue turning
  FNA's harmless GC-covered pattern into a real leak, not a CNA logic divergence from FNA's own
  behavior.) Fix: have `Initialize()` explicitly free the previous collection's contents before
  overwriting (ties into the same ownership-model question as Net's Phase 3 — consider a consistent
  approach across both namespaces). Add a leak-check test (or at minimum a test with an
  instance-counting test double) proving no leak across two `Initialize()` calls.

- [ ] **Task 7.6** — Move `SignedInGamer::SignedIn`/`SignedOut` static events off the incorrect
  `NOXNA` tag — they are genuine, fully public XNA 4.0 API (confirmed against FNA's
  `SignedInGamer.cs`, `public static event EventHandler<SignedInEventArgs> SignedIn;`), not CNA
  extensions. Remove the `NOXNA` marker from these two declarations specifically (the
  `OnSignIn`/`OnSignOut` raiser methods are a separate, correctly-flagged-as-different issue — see
  Task 7.7).

- [ ] **Task 7.7** — Change `SignedInGamer::OnSignIn`/`OnSignOut` from `public static ... NOXNA` to
  `private static` + `friend class GamerServicesDispatcher`, matching this project's own documented
  convention for C# `internal` members (per `CHECKLIST.md`). Confirmed FNA declares these `internal
  static void OnSignIn/OnSignOut(...)`. The only caller anywhere in the codebase today is
  `GamerServicesDispatcher.cpp` (`OnSignIn`); `OnSignOut` currently has zero callers at all. Add the
  `friend` declaration and verify the build still passes after tightening visibility.

- [ ] **Task 7.8** — Fix `GamerCollection<T>::GamerCollectionEnumerator::getCurrent()` having no
  bounds check — undefined behavior, not a catchable exception like FNA's equivalent. Confirmed:
  `return (*collection_)[static_cast<std::size_t>(position_)];` uses raw `std::vector::operator[]`.
  Calling `getCurrent()` before the first `MoveNext()` (`position_ == -1`, casts to a huge
  `size_t`) or after enumeration has run past the end is real undefined behavior — likely a crash or
  memory corruption. FNA's equivalent throws a catchable `ArgumentOutOfRangeException` in the same
  situation. Fix: bounds-check and throw the matching `sharp-runtime` exception type. Add a test for
  both misuse cases (this is exactly the gap Task 8.3's test-coverage task would otherwise leave
  undiscovered).

- [ ] **Task 7.9** — Fix the wrong exception types thrown by 3 collection indexers, for consistency
  with FNA and with this same file's own other, correctly-typed exceptions. Confirmed:
  `AchievementCollection::operator[](const std::string&)` throws `std::out_of_range`, but FNA's
  equivalent explicitly does `throw new IndexOutOfRangeException();`. `sharp-runtime` already ships
  both `System::IndexOutOfRangeException` and `System::ArgumentOutOfRangeException`, and this same
  file already uses proper `sharp-runtime` exception types elsewhere (`NotSupportedException`,
  `InvalidOperationException`), so this is a real inconsistency, not a missing-dependency gap. The
  same issue affects `AchievementCollection::operator[](int)` (via `.at()`) and the base
  `GamerCollection<T>::operator[](int)` (also via `.at()`) — FNA's `List<T>`/`ReadOnlyCollection<T>`
  int indexers throw `ArgumentOutOfRangeException`. Switch all three call sites to the matching
  `sharp-runtime` exception types. Update/add tests asserting the correct exception type is thrown
  in each case (not just "throws something").

- [ ] **Task 7.10** — Add the missing `NOXNA` marker to `LeaderboardEntry::getRankingEXTProperty()`.
  Confirmed against the XNA 4.0 HTML doc spec that real XNA's `LeaderboardEntry` exposes only
  `Columns`, `Gamer`, `Rating` — no ranking property; `RankingEXT` is FNA's own stub extension.
  `LeaderboardEntry.hpp` (~lines 51-56) declares it without `NOXNA`, violating `CLAUDE.md`'s "MUST
  wrap it with NOXNA" rule (contrast with `operator==`/`operator!=` a few lines below, which are
  correctly marked). Add the marker to the declaration and its Doxygen block.

- [ ] **Task 7.11** — Add the missing `NOXNA` marker to `Guide::ShowAchievementsEXT`. Confirmed this
  is FNA's own addition (not real XNA 4.0 API — the doc comment even says "(FNA extension)"), but
  neither the declaration (`Guide.hpp`, ~line 321) nor the definition carries `NOXNA` anywhere in
  the file, unlike all ~25 other `Guide` members (which are real XNA API and correctly unmarked).
  Add the marker.

- [ ] **Task 7.12** — Fix `FriendCollection::Dispose()` never deleting the raw `FriendGamer*`
  pointers it owns (mirrors the same ownership-model gap as Net's Task 3.1). Confirmed:
  `Dispose()` does `collection_.clear()`, which only drops the pointers from the vector, never
  deleting the pointed-to objects. Currently masked because `SignedInGamer::GetFriends()` only ever
  constructs an empty stub `FriendCollection` today (no real `FriendGamer*` is ever allocated in
  practice) — but there is no documented ownership contract for when real friend-list population is
  eventually implemented. Fix and document the ownership contract now, before any non-stub
  population work begins, using the same design decision as Task 3.1/7.5's ownership-model
  question. Add a test (with a test-double/instance counter) proving no leak on `Dispose()` once
  real `FriendGamer` objects can exist in the collection.

---

## Phase 8 — GamerServices: API Gaps

- [ ] **Task 8.1** — Add an `IDictionary<string, object>`-equivalent surface to `PropertyDictionary`.
  Confirmed FNA's `PropertyDictionary` explicitly implements
  `IDictionary<string, object>`/`ICollection<KeyValuePair<string, object>>`: `Add(key, value)`
  (throws on duplicate key, unlike the indexer setter), `Remove(key)`, `Clear()`, `Keys`, `Values`,
  `IsReadOnly` (hardcoded `true`), `Contains`, `CopyTo` (throws `NotImplementedException` in FNA
  itself, so preserve that). `sharp-runtime` already ships a matching
  `System::Collections::Generic::IDictionary<TKey,TValue>` shape — use it rather than inventing a
  new interface. Add these members (matching FNA's exact semantics, including `Add` throwing on
  duplicate keys) and tests for each.

- [ ] **Task 8.2** — Add `AchievementCollection`'s missing `IList<Achievement>`/`ICollection<Achievement>`
  surface: `IndexOf`, `Insert`, `RemoveAt`, `Add`, `Remove`, `Clear`, `Contains`, `CopyTo`, and
  `IsReadOnly` (hardcoded `true`), matching FNA's `IList<Achievement>, ICollection<Achievement>,
  IEnumerable<Achievement>, IEnumerable, IDisposable` interface list. Lower priority than Task 8.1
  since these are C# explicit-interface members only reachable via an `IList<Achievement>` cast in
  real XNA, but still a real surface gap for full fidelity. Add tests for each new member.

- [ ] **Task 8.3** — Extend `GamerCollection<T>` to expose the full `ReadOnlyCollection<T>` surface
  FNA's equivalent provides (`Contains`, `IndexOf`, `CopyTo`), since FNA's `GamerCollection<T>`
  derives from `ReadOnlyCollection<T>`. `sharp-runtime` already has a full
  `System::Collections::ObjectModel::ReadOnlyCollection<T>` (already used elsewhere in this exact
  namespace by `LeaderboardReader::getEntriesProperty()`), so the infrastructure to fix this already
  exists — either derive `GamerCollection<T>` from it or add equivalent methods directly. This
  affects every collection built on `GamerCollection<T>` (`SignedInGamerCollection`,
  `FriendCollection`). Add tests for the new members on at least one concrete collection type.

---

## Phase 9 — GamerServices: Test Coverage

- [ ] **Task 9.1** — Add a test for `Gamer::setSignedInGamersProperty`'s delete-old-then-replace
  logic — currently only the getter is tested. Cover setting once, setting twice (proving the old
  collection is properly cleaned up per Task 7.5's fix), and setting to the same pointer.

- [ ] **Task 9.2** — Add tests for the 3 untested `FriendGamer` properties: `getInviteReceivedFromProperty()`,
  `getInviteRejectedProperty()`, `getInviteSentToProperty()` — confirmed never referenced anywhere
  in the test suite (the existing `DefaultStubFlags` test only checks `IsJoinable`, `HasVoice`,
  `InviteAccepted`, `Presence`).

- [ ] **Task 9.3** — Add real iteration/mutation tests for `GamerCollection<T>`'s custom enumerator
  and NOXNA mutators, for both `FriendCollection` and `SignedInGamerCollection`. Confirmed a grep
  across all GamerServices test files for enumerator/`Add`/`Remove` usage returns nothing — this is
  exactly the coverage gap that let Task 7.8's `getCurrent()` bug ship undetected. Test
  `GetEnumerator()`, `MoveNext()`, `getCurrent()`, `Reset()`, `Dispose()` on the enumerator, and the
  `Add()`/`Remove()` mutators, with at least 2+ elements (not just 0 or 1).

- [ ] **Task 9.4** — Add ordinal-value assertions to `GamerPresenceModeTest` and sibling enum tests
  (`GamerPrivilegeSettingTest`, `GamerZoneTest`, `ControllerSensitivityTest`, `GameDifficultyTest`,
  `LeaderboardKeyTest`, `LeaderboardOutcomeTest`), not just tautological self-equality checks.
  Confirmed `GamerPresence::setPresenceModeProperty` indexes a 60-entry string table directly by the
  enum's ordinal, so a future accidental reordering of `GamerPresenceMode` would silently break
  presence strings with nothing to catch it (unlike the already-correct `AvatarBodyType`/`AvatarBone`
  tests in the same file, which do check exact ordinal values).

- [ ] **Task 9.5** — Add an out-of-range int-index test for `AchievementCollection::operator[](int)`
  (only the string-key-not-found case is currently tested) — add alongside Task 7.9's exception-type
  fix.

- [ ] **Task 9.6** — Add tests for the 3 untested `PropertyDictionary::GetValueX` overloads
  (`GetValueDateTime`, `GetValueStream`, `GetValueTimeSpan`) and both `operator[]` overloads (get,
  set, and missing-key path) — currently only int/float/double/long/string/outcome plus
  `ContainsKey`/`TryGetValue`/`CountIncrementsOnSet` are covered.

- [ ] **Task 9.7** — Add equal-case and not-equal-case (differing gamer, rating, ranking) tests for
  `LeaderboardEntry::operator==`/`operator!=` — currently zero coverage, despite the class's own
  doc-comment stating the operator exists specifically to support
  `ReadOnlyCollection<T>::IndexOf/Contains`.

- [ ] **Task 9.8** — Add an out-of-process test (mirroring
  `tests/CNA/Internal/Net/GamerServicesDispatcherHangRegressionTest.cpp`'s isolation pattern) for
  `GamerServicesDispatcher::Initialize()`'s actual gamer-population behavior. Confirmed nothing
  currently verifies: the 4 stub gamers get the exact names `"Stub Gamer"`/`"Stub Gamer (1)"`/`"(2)"`/`"(3)"`;
  their `PlayerIndex` values are `One`/`Two`/`Three`/`Four`; `Gamer::getSignedInGamersProperty()`
  ends up with exactly 4 entries; `SignedInGamer::OnSignIn()` fires once per gamer. Ideally also
  exercise Task 7.1's `GetAchievements()` fix in the same isolated process, proving it doesn't hang
  once real initialization has actually happened.

- [ ] **Task 9.9** — Add a populated-collection test for `SignedInGamerCollectionTest::operator[](PlayerIndex)`.
  Confirmed the existing test only covers the empty-collection case (returns `nullptr` for any
  index). Add a test with a populated (2-4 gamer) collection verifying: correct gamer returned at a
  valid index, boundary case `index == size()` returns `nullptr`, and iteration with >1 element
  works correctly.

- [ ] **Task 9.10** — Add message-content assertions to all 6 GamerServices exception types'
  `DefaultCtor` tests. Confirmed every existing `*Test.DefaultCtor` only checks `dynamic_cast`
  succeeds, never what the default (no-message) constructor produces for `what()`/`getMessageProperty()`
  — a regression that silently blanked the default message wouldn't be caught by any existing test.

---

## Phase 10 — GamerServices: Further Investigation

- [ ] **Task 10.1** — Re-evaluate `Gamer`'s empty-string-as-null-sentinel for `displayName`
  (`displayName_(displayName.empty() ? gamertag : displayName)`). Confirmed FNA's `DisplayName =
  displayName ?? gamertag` only substitutes on a true C# `null`, not on an explicit empty string — a
  caller intentionally passing `""` as a blank display name keeps `""` in FNA but gets silently
  overwritten in cna. Already documented in a source comment and tested, satisfying `CHECKLIST.md`'s
  deviation-documentation rule, but worth a decision on whether `std::optional<std::string>` should
  replace the sentinel, since `FriendGamer`'s constructor forwards externally-sourced display names
  through this same path. Decide and implement (or explicitly re-affirm the current documented
  deviation as intentional and sufficient).

- [ ] **Task 10.2** — Design and document the ownership contract for `T*` items inside
  `GamerCollection<T>`-derived collections (broader framing of Tasks 3.1/7.5/7.12). No documented
  contract currently exists for who allocates/frees `FriendGamer*`/`SignedInGamer*`/`NetworkGamer*`
  once real (non-stub) population is implemented anywhere. This should be a single design decision
  applied consistently across both `Net` and `GamerServices` — do this task first, before Tasks
  3.1/7.5/7.12's individual fixes, so they all follow one consistent model rather than three
  independent ad hoc fixes.

- [ ] **Task 10.3** — Investigate whether a lightweight fake-`Game`/mock-`IServiceProvider` test
  double could allow direct unit testing of `GamerServicesComponent::Initialize()`/`Update()`
  forwarding, without needing a full SDL window. Currently `GamerServicesComponent` has no direct
  unit tests (documented precedent, consistent with `GameComponentTests.cpp`'s similar situation),
  but given this exact `Dispatcher`/`Component` pairing has now produced two real hang bugs (the
  already-fixed `NetworkSession` one, and Task 7.1's `GetAchievements` one), a real, direct test of
  this pairing's forwarding logic would add real value beyond what integration-level tests can
  catch.

- [ ] **Task 10.4** — Assess real-world reachability/priority of the double-`Initialize()` leak
  (Task 7.5). FNA's own `GamerServicesDispatcher.Initialize()` has the identical
  no-op-if-already-initialized gap — confirm whether any current/planned caller (a game re-adding a
  second `GamerServicesComponent`, or a multi-`Game`-instance test harness) can trigger this today,
  to properly gauge priority relative to other Phase 7 bugs.

- [ ] **Task 10.5** — Decide whether `GamerCollection<T>` needs a virtual destructor. Confirmed not
  currently exploited (all current code deletes via the concrete derived pointer type, e.g.
  `SignedInGamerCollection*`), but it's a latent risk if any future code ever holds/deletes a
  `GamerCollection<T>*` base pointer. Either add a virtual destructor, or explicitly document (with
  a comment and/or a `static_assert`/deleted-copy-style guard) that this type must never be used
  polymorphically via a base pointer.

- [ ] **Task 10.6** — Re-verify `LeaderboardReader`'s page-slicing constructor loop bound
  (`for (int i = pageStart_; i < pageSize_ && i < entryCache_.size(); ++i)`) against every current
  and future caller of `CreateInternal`. Confirmed this faithfully matches FNA's own identical
  (non-obvious) bound — correctly *not* a cna-introduced bug — but correctness depends entirely on
  whatever populates `entryCache_` already being consistent with this bound. Add a doc comment (or
  an assertion) making this precondition explicit for any future caller, so the non-obvious FNA
  fidelity isn't accidentally "fixed away" later by someone unaware of why it looks odd.

---

## Phase 11 — Avatar: Bugs (faithful API + real-rendering EXT engine layer)

- [ ] **Task 11.1** — Fix the unbounded iterative loop-wraparound in
  `SkinnedModelEXT::ComputeBoneTransformsEXT`. Confirmed (`SkinnedModelEXT.cpp`, ~lines 114-118):
  `while (pos > clip.Duration) { pos = pos - clip.Duration; }` (and the symmetric negative-direction
  loop) subtracts/adds `Duration` one clip-length at a time instead of computing a single modulo
  (`System::TimeSpan` has no modulo operator). If `position` (an accumulated playtime) grows large
  relative to a short clip `Duration` — e.g. a long-running demo/game session — this iterates
  proportionally: a real, unbounded per-frame cost, not just a style nit. Fix: compute wraparound
  via a single division/multiply (`pos - Duration * floor(pos/Duration)`). Add a test with a large
  accumulated position and a short clip duration, asserting correct results and (via a call-count or
  timing bound) that the fix doesn't iterate proportionally to `position`.

- [ ] **Task 11.2** — Add validation that `ParentBoneIndices` is topologically ordered in
  `ComputeBoneTransformsEXT`. Confirmed (~lines 140-149): the code comments "bones are stored in
  topological (breadth-first) order" but never checks `parent < i` for each bone — a pure
  convention enforced only by the content pipeline, not the engine. Malformed/future content with
  `parent[i] >= i` (or a self-referencing/cyclic parent) silently reads a not-necessarily-identity
  `Matrix` from `worldTransforms[parent]` with no error. Add a validation pass (throwing
  `ArgumentException`) either at load time (`SkinnedModelTypeReader`) or in
  `ComputeBoneTransformsEXT` itself. Add a test feeding a deliberately non-topological/cyclic parent
  array and asserting it's rejected cleanly.

- [ ] **Task 11.3** — Add a bounds/size-consistency check between `BoneCount` and
  `ParentBoneIndices`/`BindPoseLocal`/`InverseBindPoseGlobal` in `SkinnedModelEXT`. Confirmed
  (~lines 143-155): all three arrays are indexed by `i` up to `BoneCount` with no check that
  `.size() == BoneCount`. Since these are populated straight from file content, a corrupt/truncated
  `.skeleton.bin` produces real out-of-bounds `std::vector::operator[]` reads (undefined behavior),
  not a hypothetical. Add the size check (throwing a clear `ArgumentException`/`ContentLoadException`
  instead) and a test with a deliberately size-mismatched skeleton.

- [ ] **Task 11.4** — Add slot/replace-by-name semantics to `SkinnedModelEXT::AttachPartEXT` (or a
  new `ReplacePartEXT`). Confirmed a real, live problem: `AttachPartEXT` unconditionally appends
  every part from `other` into `Parts` with no duplicate/slot-replace handling. Both shipped
  wardrobe pieces (`examples/demo_avatar/Content/wardrobe/hair_Cap/` and `hair_Ponytail/`) use the
  *identical* part name `"CNAAvatarHair"` — attaching one after another (e.g. swapping hairstyles at
  runtime) yields two overlapping "CNAAvatarHair" meshes both rendered. `examples/demo_avatar/src/AvatarDemo.cpp`
  (~lines 90-94) already has to manually `Parts.erase(std::remove_if(...Name ==
  "CNAAvatarHair"...))` before every `AttachPartEXT` call as a hand-rolled workaround — proof the
  engine API itself is missing this. Add real replace-by-name semantics to the engine method itself
  (see Task 11.5 for the resource-leak half of this same problem) and remove the demo's manual
  workaround once the engine handles it. Add a test attaching two parts with the same name and
  asserting only one remains/renders.

- [ ] **Task 11.5** — Fix the GPU-resource leak in the demo's manual `Parts.erase()` workaround (and
  add a proper engine-level removal API so this can't recur). Confirmed: `AvatarDemo.cpp` (~lines
  90-94) erases entries straight out of the *public* `SkinnedModelEXT::Parts` vector, but the
  corresponding `vertexBuffers_`/`indexBuffers_`/`ownedParts_`/`textures_` `unique_ptr`s (private,
  `SkinnedModelEXT.hpp`, ~lines 190-193) are never removed — the old hair part's GPU buffers stay
  allocated and owned forever, just no longer drawn. Add a proper `RemovePartEXT(name)`/`DetachPartEXT(name)`
  API to `SkinnedModelEXT` that also frees the underlying owned resources, and stop exposing `Parts`
  as a directly-mutable public vector for this purpose (tie this in with Task 11.4's fix — likely
  one combined API change). Add a test proving a removed part's resources are actually released
  (e.g. via an instance/resource counter).

- [ ] **Task 11.6** — Add `isDisposed_` checks to `AvatarRenderer::EnableRealRenderingEXT`/
  `SetAppearanceEXT`. Confirmed (`AvatarRenderer.cpp`, ~lines 121-137): unlike `DrawRealEXT`/`Draw`/
  `getStateProperty`/`getBindPoseProperty` (which all throw `ObjectDisposedException`), these two
  EXT methods silently succeed after `Dispose()` — `EnableRealRenderingEXT` even re-populates
  `realDevice_`/`realModel_`/`realEffect_`, effectively "undisposing" the object. Add the same
  `isDisposed_` check to both, for consistency with the rest of the class's own `IDisposable`
  contract. Add tests for both methods called after `Dispose()`, asserting `ObjectDisposedException`
  (mirroring the existing `DrawRealThrowsAfterDispose` test's pattern).

- [ ] **Task 11.7** — Add bounds checking to `ContentManager`'s `BinReaderEXT::Read<T>()`. Confirmed
  (`ContentManager.cpp`, ~lines 585-592): `std::memcpy(&value, Data.data() + Pos, sizeof(T)); Pos +=
  sizeof(T);` never checks `Pos + sizeof(T) <= Data.size()`. A truncated or corrupt
  `.skeleton.bin`/`.clip.bin` (or a header value like `boneCount`/`trackCount`/`keyCount`
  inconsistent with the file's actual byte length) causes a real out-of-bounds heap read (undefined
  behavior) instead of a clean `ContentLoadException`. This is the most serious memory-safety
  finding in the Avatar content-loading path. Fix: bounds-check before every read, throwing
  `ContentLoadException` on underflow. Add a test loading a deliberately truncated
  `.skeleton.bin`/`.clip.bin` and asserting a clean exception, not a crash (run under ASan if
  available).

- [ ] **Task 11.8** — Add a sanity check on `boneCount` before `.resize()` in
  `SkinnedModelTypeReader::Read()`. Confirmed (`ContentManager.cpp`, ~lines 679-681): `boneCount` is
  read as a raw `int32_t` with no validation it's `>= 0` or below some sane cap before
  `static_cast<std::size_t>(boneCount)` is used to `.resize()` three vectors — a corrupted/negative
  value produces a huge `std::size_t` and either an allocation failure or a crash rather than a
  graceful `ContentLoadException`. Add the validation and a test with a deliberately corrupt/negative
  `boneCount`.

- [ ] **Task 11.9** — Add vertex/index consistency validation in `SkinnedModelTypeReader::Read()`.
  Confirmed (`ContentManager.cpp`, ~lines 712-715): `numVertices = vertBytes.size() / stride`
  truncates silently if the byte count isn't an exact multiple of `stride`; index values from
  `idxBytes` are never checked to be `< numVertices`. Malformed/corrupted part data can produce an
  index buffer that references out-of-range vertices with no validation anywhere in this path. Add
  the checks (throwing `ContentLoadException` on a mismatch) and a test with deliberately
  inconsistent vertex/index data.

- [ ] **Task 11.10** — Investigate consolidating the vertex-layout-by-magic-stride-number pattern
  for the Skinned (52-byte) vertex format specifically. Confirmed the same `switch(stride){case
  52: ...}` idiom is independently duplicated in `EasyGLGraphicsBackend.cpp` (~lines 1790-1802),
  `BgfxGraphicsBackend.cpp`'s `MakeBgfxLayout` (~lines 1249-1268), and hardcoded
  stride/offsets in `VulkanGraphicsBackend.cpp`'s `GetOrCreatePipelineSkinned3D` (~lines 3384-3399,
  `constexpr kSkinnedStride = 52`) — none derive the layout from
  `VertexPositionNormalTextureSkinned::getVertexDeclarationStatic()` or share a single source of
  truth; only `VertexBuffer::SetData(const VertexPositionNormalTextureSkinned*, int)` has a
  `static_assert(sizeof(GpuVertex) == 52)` guarding its own packing. If the vertex struct's layout
  ever changes, 3 independent backend copies would silently desync with no compile-time error. This
  is a pre-existing project-wide convention, not introduced by this feature, so treat this as a
  design investigation (is a shared helper feasible without a bigger cross-backend refactor?) rather
  than an immediate rewrite — but the Skinned case has 5 attributes (the most complex instance) and
  is the newest, highest-risk case, so it's worth scoping even if the fix is deferred.

---

## Phase 12 — Avatar: API Gaps

- [ ] **Task 12.1** — Add `FindPartEXT`/`RemovePartEXT` (or equivalent) API to `SkinnedModelEXT`.
  Confirmed callers currently have to reach into the public `Parts` vector directly with
  `std::remove_if`/`erase` (see `AvatarDemo.cpp`, ~lines 90-94), which is both undocumented as a
  supported pattern and the direct cause of Tasks 11.4/11.5. This task may be fully subsumed by
  Task 11.5's fix if scoped together — check before starting whether a separate task is still
  needed once 11.4/11.5 land.

---

## Phase 13 — Avatar: Test Coverage

- [ ] **Task 13.1** — Add direct/edge-case test coverage for `AvatarRenderer::PartTintEXT`'s
  substring-match routing logic. Confirmed it's `private`, reachable only through `DrawRealEXT` +
  GPU pixel-readback, and the one existing integration test
  (`avatar_tint_routing_integration_test.cpp`) covers only Hair/Shirt routing — Pants/Shoes/skin-fallback
  routing through the real `PartTintEXT` code path is untested at any level (only
  `AvatarAppearanceEXT`'s own storage round-trip is tested, not the routing logic itself). Also add
  case-sensitivity and substring-collision edge-case coverage.

- [ ] **Task 13.2** — Add a test for `ComputeBoneTransformsEXT`'s defensive bone-index bounds check
  (`if (!track.Keys.empty() && track.BoneIndex >= 0 && track.BoneIndex <
  static_cast<int>(localTransforms.size()))`, ~lines 133-134) — confirmed no existing test exercises
  a track with an out-of-range or negative `BoneIndex` to confirm it's safely skipped rather than
  silently mis-happening.

- [ ] **Task 13.3** — Add a plain (non-GPU-dependent) unit test for `SkinnedModelEXT::AddPartEXT`'s
  own bookkeeping. Confirmed currently only exercised inside 3 GPU-dependent integration tests
  (zero references in the plain `tests/` unit-test tree). Cover the texture-ownership branch
  (`texture.HasBackend()` true vs. false, ~lines 61-66) and growth of the four private ownership
  vectors, independent of a real `GraphicsDevice` if at all feasible (may need a lightweight
  fake/mock graphics device — investigate what's available/precedented elsewhere in the test suite).

- [ ] **Task 13.4** — Add a test for `EnableRealRenderingEXT`/`SetAppearanceEXT` called after
  `Dispose()` — see Task 11.6 (this is the test half of that fix; do them together).

- [ ] **Task 13.5** — Extend `AvatarAnimationPresetNamesEXTTest::NameMatchesEnumeratorSpelling` to
  check all 31 presets for exact string spelling, not just 4 of them. Confirmed the other 27
  mappings are only checked for non-emptiness (`AllThirtyPresetsMapToNonEmptyName`) — a spelling
  typo in any untested mapping (e.g. `MaleSurprised` vs. a hypothetical `MaleSurprized`) would pass
  all existing tests today. Cheap fix: loop all 31 against a parallel string table instead of
  hand-picking 4.

- [ ] **Task 13.6** — Add Vulkan and Bgfx smoke tests for the avatar-rendering path. Confirmed all
  three avatar GPU integration tests (`cna_test_avatar_real_render`, `cna_test_avatar_attach_part`,
  `cna_test_avatar_tint_routing`) are currently wired up for EasyGL only
  (`cna_easygl_test(...)` in `CMakeLists.txt`) — Vulkan's dedicated `GetOrCreatePipelineSkinned3D`
  pipeline (Task 11.10) and Bgfx's bone-uniform wiring have never been run against real (or even
  synthetic) avatar content in CI, exactly matching `docs/avatar-real-rendering-ext.md`'s own
  "not yet smoke-tested" caveat. This may require a fresh Vulkan/Bgfx build configuration (`glslc`
  etc.) — investigate what's needed and either add the smoke tests or clearly document the specific
  remaining blocker if tooling is unavailable in this environment.

---

## Phase 14 — Avatar: Further Investigation (content tooling & minor polish)

- [ ] **Task 14.1** — Investigate hardening the hand-rolled JSON bracket-matching in
  `FindMatchingBracketEXT`/`ParseFlatObjectArrayEXT` (`ContentManager.cpp`, ~lines 605-642) against
  braces/brackets embedded inside string values. Confirmed this is an existing convention shared
  with `ModelTypeReader`/`SpriteFontTypeReader` (not new to this feature), but the Skinned-model
  manifest is the first one fed by a fully automated Python pipeline (`convert_avatar.py`) where a
  part/clip name containing such a character is structurally possible, even if not currently
  produced. Decide whether to add string-literal-aware bracket matching (possibly shared across all
  three readers) or explicitly document the current limitation/constraint on generated names.

- [ ] **Task 14.2** — Investigate texture path re-basing's assumption that a manifest always lives
  under the content root (`ContentManager.cpp`, ~line 732: `fs::relative(manifestDir / texFile,
  root)`). Confirmed both `Content/avatar/` and `Content/wardrobe/` happen to be nested under the
  same root in every existing test/demo, so this is currently unexercised outside that assumption —
  if a manifest is ever loaded from outside the root's directory tree, path resolution through
  `cm.Load<Texture2D>()` is unverified. Add a test loading content from a nested-but-still-under-root
  path at minimum, and decide whether the outside-root case needs explicit support or an explicit
  rejection with a clear error.

- [ ] **Task 14.3** — Polish `examples/demo_avatar/src/Main.cpp`'s CLI argument parsing. Confirmed
  `ParseGenderArg` (~lines 9-21) silently accepts any value other than exactly `"female"` (including
  a typo like `"Female"`) as `Male`, with no warning; `ParseWardrobeHairArg` similarly does zero
  validation against the two known styles (`Cap`/`Ponytail`) — a bogus style throws a raw,
  unfriendly `ContentLoadException` from deep inside `ContentManager` instead of a clear
  usage error. Minor example-code polish, not a core-engine bug; add basic validation with a
  friendly error message for both.

---

## Phase 15 — Demonstration Applications

Each new demo below is a small, focused, real, runnable program under `examples/` proving one or
more Net/GamerServices/Avatar features work end-to-end for a human to see — not a unit test. Reuse
existing demo infrastructure (`examples/demo_2d`'s SpriteBatch/window setup, `examples/demo_avatar`'s
avatar-loading/camera setup, `tools/net/net_two_process_harness.cpp`'s two-process spawn pattern)
wherever it fits, rather than rebuilding boilerplate from scratch. Every demo must build cleanly and
be manually screenshot/run-verified (per this repo's own established rigor) before being considered
done — "it compiles" is not sufficient.

- [ ] **Task 15.1** — `cna_demo_net_client_server_arena`: real two-process `NetworkSession::Create/
  Find/Join`, `LocalNetworkGamer::SendData`/`ReceiveData`, `PacketReader/Writer`, and `GamerJoined`/
  `SessionEnded` events over real ENet. A small 2D arena (reusing `demo_2d`'s SpriteBatch/window
  setup) where each connected gamer controls a colored square with arrow keys, every other gamer's
  square visibly moves too, with gamertag labels drawn above each square. Host launched with
  `--host`, client with `--join <ip>`.

- [ ] **Task 15.2** — `cna_demo_packet_roundtrip`: every XNA-type `PacketWriter::Write`/`PacketReader::Read`
  overload (`Vector2/3/4`, `Matrix`, `Quaternion`, `Color`, `float`, `double`). Console-only: writes
  a table of random values of each type into one `PacketWriter`, reads them back via a `PacketReader`
  over the same bytes, prints a PASS/FAIL row per type. Single process, no networking.

- [ ] **Task 15.3** — `cna_demo_qos_probe`: `QualityOfService` (`AverageRoundtripTime`,
  `MinimumRoundtripTime`, `BytesPerSecondUpstream/Downstream`, `IsAvailable`) measured between two
  real gamers over real ENet — depends on Phase 4's Task 4.1/4.2 wiring real measurements through
  first, otherwise this demo would just show the hardcoded stub. Console output refreshes a live
  line every ~200ms showing RTT/bandwidth. Two real processes (extends
  `net_two_process_harness`'s host/client split).

- [ ] **Task 15.4** — `cna_demo_simulated_network_conditions`: `NetworkSession.SimulatedLatencyProperty`/
  `SimulatedPacketLossProperty` — depends on Phase 4's Task 4.3 actually implementing an effect
  first. A ball bounces between two paddles (host/client), each paddle's position sent every frame;
  Up/Down arrows raise/lower simulated latency/packet-loss live, visible stutter/jitter scales with
  the HUD-displayed values. Two real processes ideally; investigate whether a single-process
  `NetworkSessionType::Local` fallback is viable if the simulated values apply to the local event
  queue too.

- [ ] **Task 15.5** — `cna_demo_session_browser`: `NetworkSession::Find(...)` returning an
  `AvailableNetworkSessionCollection`, and `Join`. One process hosts/advertises (title "Hosting…");
  the other shows a scrollable list of `AvailableNetworkSession` entries (host gamertag,
  current/max gamer counts) with Up/Down to select and Enter/A to `Join` — the classic "session
  lobby" screen. Two real processes.

- [ ] **Task 15.6** — `cna_demo_gamer_roster_hud`: the full gamer-roster event surface —
  `GamerJoined`, `GamerLeft`, `HostChanged`, `SessionEnded`, plus per-gamer `IsHost`/`IsLocal`/
  `IsReady`/`IsTalking` flags. A live-updating panel lists every `NetworkGamer` in `AllGamers` with
  colored flag icons updating in real time. Single process via `NetworkSessionType::Local` with
  multiple local gamers for a quick version, or two real processes for a fuller join/leave/host-migration
  proof (the latter also doubles as a live demo of Phase 2's Task 2.6 host-migration fix, if
  implemented).

- [ ] **Task 15.7** — `cna_demo_session_lifecycle_events`: `NetworkSession::StartGame()`/`EndGame()`,
  `NetworkSessionState` transitions (Lobby→Playing→Ended), `GameStarted`/`GameEnded` events, and a
  manual `Raise()` of `WriteArbitratedLeaderboard`/`WriteUnarbitratedLeaderboard`/`WriteTrueSkill`
  to prove the delegate wiring works even though the real port never triggers them automatically —
  an honest spotlight on that documented gap. Console-only, single process,
  `NetworkSessionType::Local`.

- [ ] **Task 15.8** — `cna_demo_gamerservices_signin_presence`: `GamerServicesComponent`
  registration, the resulting population of `Gamer::SignedInGamers` (4 stub gamers), `SignedInGamer::SignedIn`/
  `SignedOut` static events, and `GamerPresence` (`PresenceMode`, `PresenceValue`,
  `SetPresenceModeStringEXT`). Number keys cycle each signed-in gamer's `GamerPresenceMode`, HUD
  text shows the resulting presence string live. Single process.

- [ ] **Task 15.9** — `cna_demo_achievement_showcase`: `Achievement`, `AchievementCollection`,
  `SignedInGamer::AwardAchievement`/`GetAchievements`. A grid of achievement tiles (built via
  `CreateInternal` since the real `GetAchievements()` is empty on this platform) shows
  locked/unlocked art, gamerscore badges, `EarnedDateTime`; number keys call `AwardAchievement(key)`
  and flip a tile to "earned" with a small animation. Single process, reuses `demo_2d`'s
  SpriteBatch/SpriteFont setup.

- [ ] **Task 15.10** — `cna_demo_leaderboard_viewer`: `LeaderboardReader` (`PageUp`/`PageDown`,
  `CanPageUp/Down`, `Entries`, `PageStart`, `TotalLeaderboardSize`) plus an explicit demonstration of
  `LeaderboardWriter::GetLeaderboard`'s always-throws-`NotSupportedException` platform boundary. A
  scrolling table of fabricated `LeaderboardEntry` rows (via `CreateInternal`) with Up/Down paging;
  a status line also attempts the real throwing calls once and prints "threw NotSupportedException
  as expected". Single process.

- [ ] **Task 15.11** — `cna_demo_guide_overlay_console`: the full `Guide` static API surface —
  `ShowSignIn`, `BeginShowKeyboardInput`/`EndShowKeyboardInput` (completes instantly with an empty
  string), `BeginShowMessageBox`/`EndShowMessageBox` (throws), `IsTrialMode`/`SimulateTrialMode`,
  `IsScreenSaverEnabled`, `NotificationPosition`, `DelayNotifications`. Console menu: each numbered
  key triggers one `Guide` call and prints its result/exception. Single process, no graphics needed.

- [ ] **Task 15.12** — `cna_demo_gamerservices_dispatcher_watchdog`: a visual/interactive version of
  `tools/net/gamerservices_dispatcher_harness.cpp` (proving Task 12.1's/this plan's Task 7.1 hang
  fixes) — calls `GamerServicesDispatcher::Initialize()` then `NetworkSession::Create(...)` (and,
  once Task 7.1 lands, `SignedInGamer::GetAchievements()` too) and shows on-screen ticking text
  "waiting…" followed by "SUCCESS" once each resolves, so a human watching the window can see the
  historical hangs are fixed rather than trusting an exit code. Single process.

- [ ] **Task 15.13** — `cna_demo_gamer_profile_privileges`: `GamerProfile` (`GamerScore`,
  `GamerZone`, `Motto`, `Region`, `Reputation`, `TitlesPlayed`, `TotalAchievements`, via
  `CreateInternal`) and `GamerPrivileges`. Left/Right cycles through the 4 stub `SignedInGamers`,
  showing each one's profile card and privilege flags. Single process.

- [ ] **Task 15.14** — `cna_demo_friends_and_gamercard`: `FriendCollection` (via `CreateInternal`)
  and the no-op `Guide::ShowGamerCard`/`ShowFriendRequest`/`ShowFriends`/`ShowComposeMessage` calls.
  A friends-list panel plus an on-screen scrolling log printing "ShowGamerCard(...) called" etc.
  every time a key triggers one of those `Guide` calls, since none produce real OS UI otherwise.
  Single process.

- [ ] **Task 15.15** — `cna_demo_avatar_animation_gallery`: a completionist version of
  `demo_avatar`'s Space-cycling — programmatically iterates **all 31** `AvatarAnimationPreset`
  values (not a hand-picked subset), resolves each via `AvatarAnimationPresetToClipNameEXT`,
  auto-plays/labels each for ~2 seconds before advancing, switching gender and reloading content
  every full cycle so both Male* and Female* presets play against their own gender's baked clips.
  Reuses `demo_avatar`'s window/camera/renderer setup. Single process.

- [ ] **Task 15.16** — `cna_demo_avatar_wardrobe_hotswap`: `SkinnedModelEXT::AttachPartEXT`/
  `RemovePartEXT` (Task 11.4/11.5) used repeatedly *at runtime* — Tab cycles live between baked-in
  hair, `wardrobe/hair_Cap`, and `wardrobe/hair_Ponytail`, removing the old hair part and
  re-attaching, with the avatar visibly changing hairstyle without restarting the process. Depends
  on Tasks 11.4/11.5/12.1 landing first (otherwise this demo would need the same manual workaround
  `AvatarDemo.cpp` already has, which somewhat defeats its own purpose as a proof of the *engine*
  API). Single process, reuses `demo_avatar`'s Content/renderer.

- [ ] **Task 15.17** — `cna_demo_avatar_appearance_tint_studio`: `AvatarAppearanceEXT`'s 5 tint
  slots (Skin/Hair/Shirt/Pants/Shoes) and `AvatarRenderer::SetAppearanceEXT` as a live color
  customization screen. Number keys 1-5 select a slot, Up/Down cycle preset swatch colors, avatar
  re-tints on the next `DrawRealEXT` call with an on-screen swatch row showing the 5 current colors.
  Single process.

- [ ] **Task 15.18** — `cna_demo_avatar_dual_compare`: two independent `AvatarRenderer`/
  `SkinnedModelEXT` instances alive and drawing simultaneously (not yet exercised anywhere — all
  existing avatar code uses exactly one). Male and female avatars stand side-by-side, each
  independently steppable through animation presets (1/2 select which avatar, Space cycles its
  clip), proving multi-instance rendering and per-instance appearance isolation. Single process.

- [ ] **Task 15.19** — `cna_demo_avatar_multi_attach_stress`: an interactive, human-drivable version
  of `avatar_attach_part_integration_test.cpp`'s idea. Each keypress attaches one more standalone
  wardrobe piece via `AttachPartEXT` (hair variants plus a synthetic quad "accessory"), with an
  on-screen `Parts.size()` counter and all attached parts visibly rendering together, proving
  accumulation doesn't break skinning/tinting as part count grows. Single process.

- [ ] **Task 15.20** — `cna_demo_avatar_bone_state_boundary`: documents the surprising,
  verified-intentional `AvatarRenderer` behavior — `getStateProperty()` always returns
  `Unavailable`, and `getParentBonesProperty()`/`getBindPoseProperty()` always throw
  `InvalidOperationException` — contrasted against the *working* real skeleton reachable through
  `SkinnedModelEXT::ParentBoneIndices`/`BindPoseLocal` (the EXT path `demo_avatar` actually uses).
  Attempts both APIs, catches/prints the expected exception from the faithful path, then prints the
  real bone count/hierarchy from the EXT path — teaching exactly where the "real" boundary sits.
  Console or minimal window, single process.

- [ ] **Task 15.21** — `cna_demo_net_avatar_sync` (bonus, cross-cutting): combines Net + Avatar —
  each of two processes loads its own gendered avatar, and every frame sends local position/yaw
  plus current `AvatarAnimationPreset` index over `PacketWriter`/`SendData(SendDataOptions::InOrder)`;
  each process renders both its own and the remote peer's avatar in one 3D scene, moving with arrow
  keys and switching animation with Space — the smallest possible proof that Net and
  Avatar/GamerServices compose the way a real game would use them together (in the spirit of
  `cna-samples/ClientServerSample` but with avatars instead of tanks). Two real processes.

---

## Notes on scope and sequencing

- **Ownership-model tasks (10.2, and the individual fixes in 3.1/3.2/3.3/7.5/7.12) should be done
  as one coherent design decision**, not three-plus independent point fixes — do Task 10.2 first
  (or fold it into whichever of 3.1/3.2/3.3 is tackled first, then apply the same model to the
  rest).
- **Phase 1 (Net critical security bugs) should be prioritized first** — these are the only
  findings in this plan that are remotely triggerable against an unauthenticated LAN or a connected
  peer, independent of any XNA-fidelity concern.
- **Demo apps in Phase 15 that depend on earlier phases' fixes are noted inline** (e.g. Task 15.3
  depends on 4.1/4.2, Task 15.4 depends on 4.3, Tasks 15.16 depends on 11.4/11.5/12.1) — do the
  underlying fix before or alongside the demo that showcases it, not after, so the demo doesn't ship
  showing broken/stub behavior as if it were real.
- Every task's "add a test" instruction is load-bearing, not decorative — per this repo's own
  `CLAUDE.md`, a task fixing behavior is not done until a test exists that would fail without the
  fix. Where a task explicitly couldn't verify something live (e.g. Vulkan/Bgfx smoke tests needing
  unavailable tooling), say so explicitly rather than silently skipping.
