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
- Last verified clean build + full test run: commit `6dd0fdd` (Task 5.1), **2025 / 2025 unit
  tests passing**. This was NOT re-verified after the most recent commit (`4556200`, Task 5.2
  in-progress) — see section 4.
- `4556200` adds one new header (`include/CNA/Internal/Net/NetPacketCodec.hpp`) that nothing else
  in the codebase includes or references yet. Since headers aren't compiled on their own and
  nothing calls into it, this should not affect the build — but this has **not been confirmed
  with an actual build** this session (see the "do not build" note in section 4).
- `GamerServices` namespace: **complete**, all classes ported and tested.
- `Net` namespace: **complete** API surface — 5 enums + all 18 non-enum classes (enums →
  exceptions → data structs → event args → `NetworkSessionProperties`/`QualityOfService`/
  `AvailableNetworkSession(Collection)` → `PacketReader`/`PacketWriter` → `NetworkGamer`/
  `NetworkMachine` → `NetworkSession`/`LocalNetworkGamer`). All ported and unit-tested as of Task
  4.7.
- Phase 5 (ENet backend): **Task 5.1 complete** (ENet lifecycle + host/peer RAII wrapper, tested
  with a real loopback UDP smoke test). **Task 5.2 in progress** — only the header
  (`NetPacketCodec.hpp`) declaring the wire-message structs and codec API exists; no `.cpp`
  implementation, no `NetDiscoveryProtocol`, no tests yet.

### What works
- The entire graphics stack (Phases 1–31).
- The entire `GamerServices`/`Net` synthetic (non-networked) API surface — every class, property,
  method, and static factory family that existed before Phase 5 still works exactly as before;
  Phase 5 has not touched any of it in a way that changes behavior yet.
- `CNA::Internal::Net::ENetLibrary`/`ENetHostHandle` (Task 5.1): real ENet host creation
  (ephemeral or fixed port), real loopback UDP connect + packet exchange — proven by a passing
  automated test (`ENetHostHandleTests.cpp`), not just a design claim.

### What does not work yet
- No real networking is wired into `NetworkSession`/`NetworkGamer`/`LocalNetworkGamer` yet —
  `SendData`/`ReceiveData`/`Find`/`Join`/`Create` all still behave exactly as the pre-Phase-5
  synthetic stub (this is intentional and unchanged so far; Tasks 5.3+ wire it in incrementally).
  `NetPacketCodec`'s message types/encode/decode functions are declared but not implemented, so
  nothing can actually be serialized yet.

---

## 3. Recent changes

This session's commits, newest first (all on branch `feature/net`, pushed to
`origin/feature/net`):

| Commit | Files | Change |
|---|---|---|
| `4556200` | `include/CNA/Internal/Net/NetPacketCodec.hpp` (new) | **In progress / incomplete.** Declares `MessageTag`, `RosterEntry`, `ClientHelloMessage`, `ServerWelcomeMessage`, `GamerJoinBroadcastMessage`, `GamerLeaveBroadcastMessage`, `StateChangeBroadcastMessage`, `AppDataMessage`, and the `NetPacketCodec` facade's `Encode`/`Decode`/`PeekTag`/`SendDataOptionsToEnetFlags`/`ExtractBytes`/`FillReader` API — designed to reuse the already-shipped `PacketWriter`/`PacketReader` for serialization instead of hand-rolled byte packing. **No `.cpp`, no implementation, no tests.** Nothing references this header, so it's inert. |
| `6dd0fdd` | `include/CNA/Internal/Net/ENetLibrary.hpp/.cpp`, `ENetHostHandle.hpp/.cpp` (new), `tests/CNA/Internal/Net/ENetHostHandleTests.cpp` (new) | Task 5.1: ENet lifecycle guard (lazy `enet_initialize()`, never torn down) + move-only RAII `ENetHost*` wrapper (create/connect/service/send/broadcast/flush/disconnect). Includes a real loopback smoke test (bind two hosts, connect, exchange one UDP packet) that passed, retiring the "can this sandboxed machine even do loopback UDP" risk before building protocol/relay logic on top. 5 new tests. 2025/2025 total passing. |
| `34e5bfb` | `NetworkSession.hpp/.cpp`, `LocalNetworkGamer.hpp/.cpp` (new), `NetworkSessionTests.cpp` (new); `GamerCollection.hpp`, `SignedInGamerCollection.hpp` (extended) | Task 4.7: ported `NetworkSession` (1071 lines in FNA, the largest class in `Net`) and `LocalNetworkGamer`, **completing the entire Net API surface**. Found and documented a real, faithfully-preserved FNA bug: `EndCreate`/`EndJoin`/`EndJoinInvited` null out the static `activeAction` *after* constructing `NetworkSession`, so a constructor throw strands it non-null forever (see section 4/5). 41 new tests. |
| `588af52` | `NetworkGamer.hpp/.cpp`, `NetworkMachine.hpp/.cpp` (new); `GamerCollection.hpp` (extended) | Task 4.6: ported `NetworkGamer`/`NetworkMachine`. 7 new tests. |
| `2592841` | `PacketReader.hpp/.cpp`, `PacketWriter.hpp/.cpp` (new); `Storage/StorageDevice.cpp`, `GamerServices/Gamer.hpp/.cpp`, `GamerServices/Guide.cpp` (fixed); sharp-runtime `Stream.hpp/.cpp`, `MemoryStream.hpp/.cpp` (additive) | Task 4.5: ported `PacketReader`/`PacketWriter`. Also fixed a pre-existing, unrelated build break affecting the *entire* repo: sharp-runtime's `System::IAsyncResult` had gained two pure-virtual members that `Storage::SelectorResult`/`ContainerResult`/`Gamer::GamerAction`/`GuideAction` didn't implement. 20 new tests. |

Earlier task history (4.1–4.4, 3.1–3.15, 2.x, 1.x, 0.x — GamerServices + Net enums/early classes)
is preserved in git log; not repeated here to keep this file scannable. See `git log --oneline`
on `feature/net` for the full sequence.

---

## 4. Current blocker / main problem

**No build or test failure right now** — the last actual build+test run (commit `6dd0fdd`) was
clean at 2025/2025. The "blocker" is simply that **this session was stopped mid-task by an
explicit user instruction to not build or develop further**, with Task 5.2 (wire protocol codec)
partially done:

- **Symptom:** `include/CNA/Internal/Net/NetPacketCodec.hpp` exists and declares the full wire-
  message API, but has **no corresponding `.cpp`** — every `NetPacketCodec::Encode`/`Decode`/etc.
  method is declared, not defined. If anything tried to call one of these methods right now, it
  would fail to **link** (undefined reference), not fail to compile.
- **Currently affected:** nothing — no other file includes or calls into `NetPacketCodec.hpp` yet,
  so the existing build is not expected to be affected. This has not been re-verified with an
  actual build this session (explicitly deferred per user instruction).
- **What's needed to unblock:** write `src/CNA/Internal/Net/NetPacketCodec.cpp` (the actual
  encode/decode logic using `PacketWriter`/`PacketReader`, plus the `SendDataOptions`→ENet-flags
  mapping), then `include/CNA/Internal/Net/NetDiscoveryProtocol.hpp`/`.cpp` (the LAN-discovery
  query/announce message codec, same style), then round-trip unit tests for both — exactly as
  scoped in the approved plan's Task 5.2 (see
  `/home/robertvokac/.claude/plans/scalable-swimming-feigenbaum.md`).
- **Nothing has been "tried and failed"** — this is a clean pause point, not a stuck bug.

A second, unrelated thing worth flagging as context for whoever resumes (not a current blocker,
but will become one the moment `Join()`/`Create()`/`JoinInvited(int)` need to be exercised to
completion in a test): `NetworkSession::EndCreate`/`EndJoin`/`EndJoinInvited` null out the static
`activeAction`/`activeSession` **after** constructing the `NetworkSession`, so if that constructor
throws (e.g. an empty `LocalGamers` list — the default in this test binary, since
`Gamer::SignedInGamers` is never populated by `GamerServicesDispatcher::Initialize()` in tests),
`activeAction` is stranded non-null for the rest of the process with **no public API to reset it**.
This is a real, faithfully-preserved FNA bug (FNA has the identical ordering), not something to
fix. The approved Phase 5 plan's testing strategy works around it by driving both sides of a test
connection through the safe explicit-local-gamers `Create()` overload and calling
`ENetBackend::ConnectToHost(...)` directly instead of the public `Join()`.

---

## 5. Known bugs and limitations

| Status | Issue |
|---|---|
| **Incomplete (this session, paused mid-task)** | `NetPacketCodec` (Task 5.2) — header only, no `.cpp`, no `NetDiscoveryProtocol`, no tests. See section 4. |
| **Confirmed bug (upstream FNA, preserved)** | `NetworkSession::EndCreate`/`EndJoin`/`EndJoinInvited` set the static `activeAction` back to `null` **after** constructing the `NetworkSession`, so a constructor throw (e.g. empty `LocalGamers`) strands `activeAction` non-null for the rest of the process — every later `NetworkSession::Begin*` call then throws a spurious `InvalidOperationException`. FNA has the identical ordering; not introduced here. This is why `Create(sessionType, maxLocalGamers, maxGamers)`, `Join(AvailableNetworkSession*)`, and `JoinInvited(int)` aren't exercised past argument validation in `NetworkSessionTests.cpp`. |
| **Real gap found during Phase 5 planning (not yet fixed)** | `NetworkSession::Update()`'s `GamerJoin`/`GamerLeave` branches (`NetworkSession.cpp:208-215`, verified by direct inspection) only `.Raise()` the C# event — they never mutate `AllGamers`/`RemoteGamers`. There is currently no mechanism at all to add a *remote* gamer to any roster; this must be added in Task 5.3 (`AddRemoteGamer`/`RemoveGamer`, see the approved plan). |
| **Real gap found during Phase 5 planning (not yet fixed)** | `NetworkGamer::CreateInternal`'s single-arg constructor hardcodes `Gamer("Stub Gamer", "Stub Gamer")` — every remote gamer proxy would show gamertag "Stub Gamer" forever unless the constructor gains a real gamertag parameter (planned for Task 5.3, defaulted so existing call sites are unaffected). |
| **Real gap found during Phase 5 planning (not yet fixed)** | `LocalNetworkGamer::SendData`/`ReceiveData` have a field-meaning collision: `SendData` sets `NetworkEvent.Gamer = target` when enqueuing to the session-level queue, but `ReceiveData` expects `.Gamer` to mean *sender* once popped from a gamer's own `packetQueue_`. Invisible today only because `Update()`'s `PacketSend` branch is empty. Fix planned for Task 5.3/5.5: add `NetworkEvent::Sender`, populate it in `SendData`, and have `Update()` re-map `.Gamer = evt.Sender` when moving an event into a per-gamer queue. |
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
| ENet backend (Phase 5, in progress) | `include/CNA/Internal/Net/`, `src/CNA/Internal/Net/` | `ENetLibrary`/`ENetHostHandle` done (Task 5.1); `NetPacketCodec` header-only (Task 5.2, in progress); `NetDiscoveryProtocol`/`ENetBackend` not started (Tasks 5.2–5.9) |
| Backend contracts (graphics only) | `include/CNA/Internal/Backends/Common/` | `IGraphicsBackend`, etc. — **not** the pattern used for networking (see section 1) |
| CNA utilities | `include/CNA/`, `src/CNA/` | NOXNA helpers, logging |
| sharp-runtime | `../sharp-runtime/` (sibling repo) | `System.*` types; only add new files; no version pin from this repo (see section 1) |

### Key invariants

- **`NOXNA` macro** tags every non-XNA extension in public headers under `Microsoft::Xna::…`.
  Does **not** apply to `CNA::Internal::Net` (Phase 5's new code) — that's already outside the XNA
  namespace, so nothing there needs the marker.
- **C# `internal` constructors** → `private` in C++, exposed via `NOXNA static CreateInternal(…)`.
- **C# properties** → `getXProperty()`/`setXProperty()`. Plain PascalCase methods (e.g.
  `SendNetworkEvent`, `ClearPacketQueue`, and Phase 5's planned `GetConnectAddress`) signal a
  CNA-only concept that has no FNA property to match the getter/setter convention against.
  Public-field style is never used to shortcut this convention — except where FNA/real XNA
  itself already uses public fields, e.g. `Vector2`/`Vector3`/`Vector4`/`Matrix`/`Quaternion`.
- **`System::Exception`** is the base for all GamerServices/Net exceptions (never `std::runtime_error`).
- **`GamerCollection<T>`** stores raw non-owning `T*` pointers. Gained `CreateInternal`/`Add`
  (Task 4.6/4.7) as NOXNA escape hatches for the C# `internal` same-assembly access FNA relies on
  that C++ `protected` doesn't replicate across sibling classes.
  `Remove(T*)` is planned for Task 5.3.
- **CNA-internal ENet code lives entirely under `CNA::Internal::Net`**, never
  `Microsoft::Xna::Framework::Net` — `enet/enet.h` and its types must not leak into any public
  XNA-facing header. `ENetBackend`'s per-session state is a private `.cpp`-only struct, not
  declared in any header, keyed by `NetworkSession*` in a static registry.
  See the approved plan for the exact new-file layout.
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
→ [Phase 5, in progress] ENetLibrary/ENetHostHandle (done, 5.1) → NetPacketCodec (in progress, 5.2)
  → NetDiscoveryProtocol (5.2) → ENetBackend + NetworkSession wiring (5.3) → handshake/roster
  sync (5.4) → AppData relay (5.5) → disconnect handling (5.6) → state broadcast (5.7) →
  discovery (5.8) → regression pass (5.9)
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
cmake-build-debug/CnaTests --gtest_filter="ENetHostHandleTest.*"

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
starting; this section is a summary, not a replacement):

1. **Finish Task 5.2 — Wire protocol codec.**
   - Goal: implement `src/CNA/Internal/Net/NetPacketCodec.cpp` (the `Encode`/`Decode` bodies for
     `ClientHelloMessage`/`ServerWelcomeMessage`/`GamerJoinBroadcastMessage`/
     `GamerLeaveBroadcastMessage`/`StateChangeBroadcastMessage`/`AppDataMessage`, `PeekTag`,
     `SendDataOptionsToEnetFlags`, `ExtractBytes`/`FillReader` — using `PacketWriter`/
     `PacketReader`, per the header already committed in `4556200`). Then add
     `include/CNA/Internal/Net/NetDiscoveryProtocol.hpp`/`.cpp` (`DiscoveryQueryMessage`/
     `DiscoveryAnnounceMessage`, including sparse `NetworkSessionProperties` encoding — see the
     plan's wire-format table for exact field layouts). No sockets needed for this task — pure
     encode/decode with round-trip unit tests.
   - Files: `src/CNA/Internal/Net/NetPacketCodec.cpp` (new), `include/CNA/Internal/Net/
     NetDiscoveryProtocol.hpp`/`src/CNA/Internal/Net/NetDiscoveryProtocol.cpp` (new),
     `tests/CNA/Internal/Net/NetPacketCodecTests.cpp` (new).
   - Verification: `cmake --build cmake-build-debug --target CnaTests` clean, then
     `cmake-build-debug/CnaTests --gtest_filter="NetPacketCodecTest.*"` plus a full run to confirm
     2025+ tests still pass.
2. **Task 5.3 — `ENetBackend` registry + `NetworkSession` internals wiring.** See the plan for the
   exact API (`StartHosting`/`TeardownSession`/`PumpSession`/`RealNetworkingEnabled`) and the 3
   real gaps to fix (`NetworkEvent::Sender`, `AddRemoteGamer`/`RemoveGamer`,
   `NetworkGamer::CreateInternal` gamertag param). Verify a hosted `SystemLink` session with no
   peers runs `Update()` cleanly and the full existing suite stays green (no existing test creates
   a `SystemLink` session, so this is a pure-addition risk check).
3. Tasks 5.4–5.9 as detailed in the plan (client handshake/roster sync → `AppData` relay →
   disconnect handling → state broadcast → LAN discovery → final `NetworkSessionType` policy
   regression pass).

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
- **No new development or builds beyond finishing Task 5.2's already-declared API** until the next
  session picks this up deliberately — this file was written as an explicit session-end
  checkpoint, not mid-flow.

---

## 10. Resume prompt

```
Read NEXT.md first, then read the approved Phase 5 design plan in full:
/home/robertvokac/.claude/plans/scalable-swimming-feigenbaum.md
Open only the files needed for the first task listed in section 8. Do not refactor unrelated
code. Do not expand scope beyond the task.

Current status: GamerServices + Net API surface fully ported and tested (2025/2025 tests passing
as of commit 6dd0fdd). Phase 5 (ENet backend) Task 5.1 is done and tested (ENetLibrary +
ENetHostHandle, real loopback UDP smoke test passing). Task 5.2 (wire protocol codec) is
in-progress: include/CNA/Internal/Net/NetPacketCodec.hpp (commit 4556200) declares the message
types and API but has no .cpp implementation yet, and NetDiscoveryProtocol doesn't exist yet.

Before trusting the test count: do a real `cmake --build cmake-build-debug --target CnaTests`
yourself first — it was not re-verified after the most recent (header-only, inert) commit.

Next: finish Task 5.2 (see section 8, item 1), verify build + tests, commit, then continue to
Task 5.3 per the plan.
Build: cmake --build cmake-build-debug --target CnaTests
Run:   cmake-build-debug/CnaTests
Update NEXT.md after finishing each task.
```
