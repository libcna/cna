# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`),
built on SDL3 with a pluggable 3D graphics backend layer (EasyGL/OpenGL ES 3.2, Vulkan, Bgfx,
SDL_Renderer). It is a framework/runtime — not a game — designed so that XNA/FNA game code can be
ported to C++ with minimal API-surface changes.

- **Main goal:** Full XNA 4.0 API coverage with pixel-accurate behavior, backed by unit and
  pixel-readback integration tests.
- **Current development phase:** Phase Net — porting `GamerServices`, `Net`, and `Avatar` namespaces
  from FNA.NetStub to C++, backed by ENet (reliable UDP) instead of Xbox Live.
  Previous graphics phases (1–31) are complete. `GamerServices` is now complete; `Net` (enums →
  core classes → ENet backend) is next.
- **Important architectural decisions:**
  - Graphics backend selection is compile-time via `CNA_GRAPHICS_BACKEND`.
  - `CNA_GamerServices` and `CNA_Net` are separate CMake static libraries; they are excluded from
    the main `CNA` GLOB so they do not contaminate the graphics-only build.
  - GamerServices/Net/Avatar will **not** be binary-compatible with the original Xbox Live SDK — they
    are a reimplementation of the XNA API shape backed by ENet.
  - `sharp-runtime` (sibling repo) provides all `System.*` types. Only **new** files are added there;
    existing files must not be modified (another Claude Code instance works on it in parallel).

---

## 2. Current status

### Build
- `cmake-build-debug` (EasyGL): **clean build**, all targets including `CNA_GamerServices`,
  `CNA_Net`, and `CnaTests`.
- **1972 / 1972 unit tests pass**.
- **`GamerServices` namespace is now complete** — every class in the dependency chain
  (enums → exceptions → data structs → event args → Gamer/collections → SignedInGamer →
  GamerServicesDispatcher/GamerServicesComponent/Guide) is fully ported.
- **`Net` namespace: enums done** (5) **+ 14 non-enum classes done** (`NetworkSessionProperties`,
  `QualityOfService`, `AvailableNetworkSession`, `AvailableNetworkSessionCollection`, the 7
  event-arg classes, `NetworkSessionJoinException`, `PacketReader`, `PacketWriter`); 4 non-enum
  classes plus the ENet backend/platform/integration-test phases remain.
- **sharp-runtime fix (unplanned, required to unblock any build in this repo):** sharp-runtime's
  `System::IAsyncResult` gained two pure-virtual members (`getAsyncStateProperty()`,
  `getAsyncWaitHandleProperty()`) since this repo's last successful build, which broke *every*
  target (`CNA`, `CNA_GamerServices`, `CNA_Net`, `CnaTests`) — `Storage::SelectorResult`/
  `Storage::ContainerResult`, `Gamer::GamerAction`, and `Guide.cpp`'s `GuideAction` only
  implemented the interface's original two members. Fixed by implementing the two new
  members on all four classes; `ManualResetEvent` (which doesn't derive `WaitHandle`) was
  replaced with `System::Threading::EventWaitHandle` (which does) for `GamerAction`/
  `GuideAction`'s wait handle, removing the deviation previously documented for them. See
  section 5 for details. Also added `Stream::getPositionProperty()`/`setPositionProperty()`
  (default throws `NotSupportedException`; overridden in `MemoryStream`) to sharp-runtime —
  additive only, no existing method touched — since `PacketReader`/`PacketWriter`'s `Position`
  property has no other way to reach the backing stream's cursor.

### What is done in the Net phase so far

| Task group | What was done |
|---|---|
| Task 0.1 | ENet 1.3.17 vendored under `third_party/enet/` |
| Task 0.6–0.7 | CMake targets `CNA_GamerServices` and `CNA_Net` added; placeholder `.cpp` files |
| Task 1.1 | sharp-runtime prerequisites checked; `SerializationInfo.hpp` and `StreamingContext.hpp` stubs added |
| Task 2.1–2.10 | All 10 GamerServices enums ported |
| Task 2.11–2.16 | All 6 GamerServices exceptions ported (`NetworkException`, `NetworkNotAvailableException`, `GamerPrivilegeException`, `GamerServicesNotAvailableException`, `GameUpdateRequiredException`, `GuideAlreadyVisibleException`) |
| Task 2.17–2.22 | 6 GamerServices data structures: `PropertyDictionary`, `LeaderboardIdentity`, `GamerPresence`, `GamerPrivileges`, `GameDefaults`, `Achievement` |
| Task 2.23–2.25 | 3 event arg classes: `SignedInEventArgs`, `SignedOutEventArgs`, `InviteAcceptedEventArgs` |
| Task 2.26–2.30 | Collections: `GamerCollection<T>` (template), `AchievementCollection`, `FriendGamer`, `FriendCollection`, `SignedInGamerCollection`; minimal `Gamer` stub |
| Task 2.31 | `Gamer` completed to full port; `LeaderboardEntry` and `LeaderboardWriter` ported (pulled forward from 2.32–2.33, hard dependency of `Gamer`'s constructor); `GamerProfile` ported |
| Task 2.34 | `LeaderboardReader` fully ported; minimal `GamerServicesDispatcher::UpdateAsync()` stub added (its only caller, `PageUp`/`PageDown`/`Read`, always throws before reaching it — dead code, kept for line-by-line FNA fidelity) |
| Task 2.35 | `SignedInGamer` fully ported (properties, `IsFriend`/`GetFriends`/`IsHeadset`, `AwardAchievement`/`Begin`/`EndAwardAchievement`, `GetAchievements`/`Begin`/`EndGetAchievements` — this one's poll loop actually runs and terminates correctly, since `GamerServicesDispatcher::UpdateAsync()`'s stub returning `false` marks the action complete on the first iteration; static `SignedIn`/`SignedOut` events + `OnSignIn`/`OnSignOut`). `SignedInEventArgs`/`SignedOutEventArgs`/`InviteAcceptedEventArgs` tests switched from `nullptr` stand-ins to real `SignedInGamer` instances |
| Task 2.36–2.40 | `GamerServicesDispatcher` completed (`IsInitialized`, `WindowHandle`, `InstallingTitleUpdate`, `Initialize()` creating 4 stub `SignedInGamer`s and firing `OnSignIn`, `Update()`, `UpdateAsync()` upgraded from its earlier always-`false` stub to real behavior); `GamerServicesComponent` wired to call it; `Guide` fully rewritten from an old `DEF_PROP`-based stub (which had an invented `Show(PlayerIndex)` method not in FNA) into a complete port. `Gamer` gained `setSignedInGamersProperty()` so `GamerServicesDispatcher::Initialize()` can populate it. **This completes the entire `GamerServices` namespace.** |
| Task 3.1–3.15 | All 5 `Net` enums ported: `NetworkSessionType`, `NetworkSessionState`, `NetworkSessionEndReason`, `NetworkSessionJoinError`, `SendDataOptions`. Corrected an earlier miscount in this file (it said "8 Net enums" / "15 files" / listed `QualityOfService` as an enum — the actual FNA source has only 5 standalone enum files; `QualityOfService.cs` is a class, and a 6th enum, `NetworkEventType`, is nested inside `NetworkSession.cs` and deferred to when that class itself is ported). |
| Task 4.1 | `NetworkSessionProperties` ported — first non-enum `Net` class. Implements `System::Collections::Generic::IList<std::optional<int>>` (C#'s explicit interface implementation of `IList<int?>`/`ICollection<int?>`/`IEnumerable<int?>` has no C++ equivalent, so all interface members ended up directly public). Preserves two FNA quirks as-is: the indexer setter appends instead of extending when given an out-of-range index, and `IsReadOnly` always returns `true` despite `Add`/`Remove`/`Clear` being fully functional. |
| Task 4.2 | `QualityOfService` (trivial all-defaults data class), `AvailableNetworkSession` (gained `operator==`/`operator!=`, same `ReadOnlyCollection<T>` requirement as `LeaderboardEntry`), `AvailableNetworkSessionCollection` (`ReadOnlyCollection<AvailableNetworkSession>` + `IDisposable`) ported. Corrected a dependency-order mistake in this file's own Task 4.2+ suggestion — `QualityOfService` must be ported *before* `AvailableNetworkSession` (which embeds it), not after the event-arg classes as previously listed. |
| Task 4.3 | All 7 `Net` event-arg classes ported: `GameEndedEventArgs`/`GameStartedEventArgs` (empty), `GamerJoinedEventArgs`/`GamerLeftEventArgs` (`NetworkGamer*`), `HostChangedEventArgs` (`NetworkGamer*` OldHost/NewHost), `NetworkSessionEndedEventArgs` (`NetworkSessionEndReason`), `WriteLeaderboardsEventArgs` (`NetworkGamer*` + bool, internal ctor → private + `CreateInternal()`). All `NetworkGamer*` fields are forward-declared pointers, matching the `SignedInGamer*` precedent used before that type existed. |
| Task 4.4 | `NetworkSessionJoinException` ported (`: GamerServices::NetworkException`), mirroring its base class's exact 4-ctor + protected serialization-ctor pattern (no surprises — the base was already ported in Task 2.11–2.16). |
| Task 4.5 | `PacketReader`/`PacketWriter` ported (`: System::IO::BinaryReader`/`BinaryWriter`). Required adding `Stream::getPositionProperty()`/`setPositionProperty()` to sharp-runtime (additive-only; see section 2). Ownership of the backing `MemoryStream` solved with a private "base-from-member" helper (`PacketReaderStream`/`PacketWriterStream`) listed before the `BinaryReader`/`BinaryWriter` base so the buffer exists before the base constructor needs a pointer to it — `int capacity` ctors accept but discard the capacity hint (`MemoryStream` has no preallocating constructor; capacity is a pure preallocation hint in .NET with no observable effect). `PacketWriter` required `using System::IO::BinaryWriter::Write;` to un-hide the base's other `Write(...)` overloads (C++ name-hiding has no C# equivalent). Confirmed and preserved a genuine FNA-native asymmetry: `PacketWriter::Write(Color)` writes 4 bytes but `PacketReader::ReadColor()` reads 4 floats (16 bytes) — not round-trippable through these two methods alone, matching upstream exactly (see its own `ReadSingle`/`ReadDouble` FIXME). 20 new tests. Also fixed the pre-existing, unrelated sharp-runtime `IAsyncResult` build break described in section 2. |

### What is NOT done yet in the Net phase

- **Phase 4: Net core classes** — 4 of 18 non-enum classes remain: `NetworkSession`, `NetworkGamer`, `LocalNetworkGamer`, `NetworkMachine`, and the nested `NetworkEventType` enum (tied to `NetworkSession`).
- **Phase 5: ENet backend** — not started.
- **Phase 6: Platform support** — not started.
- **Phase 7: Integration tests** — not started.
- **Phase 8: Avatar** — deferred.

---

## 3. Recent changes

| Commit | Files | Change |
|---|---|---|
| Task 4.5 | `PacketReader.hpp/.cpp`, `PacketWriter.hpp/.cpp` (new), `PacketReaderWriterTests.cpp` (new); sharp-runtime `Stream.hpp/.cpp`, `MemoryStream.hpp/.cpp`, `StreamTests.cpp` (additive `Position` support); `Storage/StorageDevice.cpp`, `GamerServices/Gamer.hpp/.cpp`, `GamerServices/Guide.cpp` (unplanned `IAsyncResult` fix) | `PacketReader`/`PacketWriter` ported; 20 new tests. Also fixed a pre-existing, unrelated build break (see section 2/4) affecting every target in the repo — not scoped to the Net phase, but nothing could be verified without it |
| Task 4.4 | `NetworkSessionJoinException.hpp/.cpp` (new), `NetworkSessionJoinExceptionTests.cpp` (new) | First `Net` exception; `: GamerServices::NetworkException`, no deviations; 7 new tests |
| Task 4.3 | `GameEndedEventArgs`, `GameStartedEventArgs`, `GamerJoinedEventArgs`, `GamerLeftEventArgs`, `HostChangedEventArgs`, `NetworkSessionEndedEventArgs`, `WriteLeaderboardsEventArgs` (.hpp/.cpp, new), `NetEventArgsTests.cpp` (new) | All 7 `Net` event-arg classes; `NetworkGamer` isn't ported yet, so its pointer fields use `nullptr` stand-ins in tests, matching the earlier `SignedInGamer*` precedent; 13 new tests |
| Task 4.2 | `QualityOfService.hpp/.cpp`, `AvailableNetworkSession.hpp/.cpp`, `AvailableNetworkSessionCollection.hpp/.cpp` (new), `AvailableNetworkSessionTests.cpp` (new) | 3 more `Net` classes. `AvailableNetworkSession` gained NOXNA `operator==`/`operator!=` (same `ReadOnlyCollection<T>` virtual-instantiation requirement hit earlier with `LeaderboardEntry`); `AvailableNetworkSessionCollection::Dispose()` only flips `IsDisposed` since sharp-runtime's `ReadOnlyCollection<T>` has no derived-class mutator for its private storage (documented deviation, not a bug); 7 new tests |
| Task 4.1 | `NetworkSessionProperties.hpp/.cpp` (new), `NetworkSessionPropertiesTests.cpp` (new) | First non-enum `Net` class; 16 new tests |
| Task 3.1–3.15 | `NetworkSessionType.hpp`, `NetworkSessionState.hpp`, `NetworkSessionEndReason.hpp`, `NetworkSessionJoinError.hpp`, `SendDataOptions.hpp` (new), `NetEnumsTests.cpp` (new) | First `Net`-namespace files. `SendDataOptions` is `[Flags]` in C# but FNA's own values are sequential (0-4), not power-of-two bit values, so it was ported as a plain enum with no bitwise operators (documented deviation, not a gap); 5 new tests |
| Task 2.36–2.40 | `Gamer.hpp/.cpp` (`setSignedInGamersProperty` added), `GamerServicesDispatcher.hpp/.cpp` (extended), `GamerServicesComponent.hpp/.cpp` (rewired), `Guide.hpp/.cpp` (full rewrite, replacing `DEF_PROP` stub), `GamerServicesServiceTests.cpp` (new) | Completes `GamerServices`: `GamerServicesDispatcher::Initialize()` now creates 4 stub `SignedInGamer`s and fires `SignedInGamer::OnSignIn`; `UpdateAsync()` upgraded to real `IsInitialized`-driven behavior; `GamerServicesComponent::Initialize()`/`Update()` now call through to it (matching FNA's override not calling `base.Initialize()`/`base.Update()`); `Guide` ported in full (`IsScreenSaverEnabled` via SDL3, keyboard/message-box async pairs, all `Show*` no-ops). 17 new tests; no tests for `GamerServicesComponent` (needs a live `Game`, same limitation as `GameComponent`) |
| Task 2.35 | `SignedInGamer.hpp/.cpp` (new), `GamerServicesEventArgsTests.cpp` (nullptr→real gamer), `GamerServicesGamerTests.cpp` (extended) | Full `SignedInGamer` port, including the first genuinely-functional (non-throwing) async poll loop in GamerServices (`GetAchievements`); 13 new tests, 7 existing event-args tests upgraded off `nullptr` |
| Task 2.34 | `LeaderboardReader.hpp/.cpp`, `GamerServicesDispatcher.hpp/.cpp` (new minimal stub), `LeaderboardEntry.hpp/.cpp` (added `operator==`/`operator!=`), `GamerServicesGamerTests.cpp` (extended) | Full `LeaderboardReader` port (paging, `Entries` via `ReadOnlyCollection<LeaderboardEntry>`, static `Read`/`BeginRead`/`EndRead` families); `LeaderboardEntry` gained equality operators (NOXNA, C++-only — required by `ReadOnlyCollection<T>`'s virtual `IndexOf`/`Contains`, not present in FNA); 19 new tests |
| Task 2.31 | `Gamer.hpp/.cpp` (extended), `GamerProfile.hpp/.cpp`, `LeaderboardEntry.hpp/.cpp`, `LeaderboardWriter.hpp/.cpp`, `GamerServicesGamerTests.cpp` | Full `Gamer` port (LeaderboardWriter property, `GamerAction` nested `IAsyncResult`, `ToString()`, `GetProfile`/`Begin`/`EndGetProfile`, `GetFromGamertag`/`GetPartnerToken` static families); `GamerProfile`, `LeaderboardEntry`, `LeaderboardWriter` ported in full (not stubs — both are trivial in FNA); 25 new tests |
| Task 2.26–2.30 | `GamerCollection.hpp` (template), `AchievementCollection.hpp/.cpp`, `FriendGamer.hpp/.cpp`, `FriendCollection.hpp/.cpp`, `SignedInGamerCollection.hpp/.cpp`, `Gamer.hpp/.cpp` | Collections hierarchy + Gamer stub; 14 tests |
| Task 2.23–2.25 | `SignedInEventArgs.hpp/.cpp`, `SignedOutEventArgs.hpp/.cpp`, `InviteAcceptedEventArgs.hpp/.cpp` | Event arg classes using forward-declared `SignedInGamer*`; 7 tests |
| Task 2.17–2.22 | `PropertyDictionary.hpp/.cpp`, `LeaderboardIdentity.hpp/.cpp`, `GamerPresence.hpp/.cpp`, `GamerPrivileges.hpp/.cpp`, `GameDefaults.hpp/.cpp`, `Achievement.hpp/.cpp` | Data structures; 23 tests |
| Task 2.11–2.16 | 6 exception headers + `.cpp` + `GamerServicesExceptionsTests.cpp` | Exceptions inheriting `System::Exception`; 34 tests |
| Task 2.1–2.10 | 10 enum headers + `GamerServicesEnumsTests.cpp` | All GamerServices enums; 11 tests |
| Task 0.1, 0.6–0.7 | `third_party/enet/`, `cmake/ThirdPartyENet.cmake`, `CMakeLists.txt` | ENet + CMake targets |
| sharp-runtime | `System/Runtime/Serialization/SerializationInfo.hpp`, `StreamingContext.hpp` | New stubs only |

---

## 4. Current blocker / main problem

**No hard blocker right now, but read this before assuming a clean build.** sharp-runtime is a
sibling repo edited by a separate, parallel Claude Code session — this repo has no version pin
on it. Between the previous checkpoint (Task 4.4, 1952/1952 tests) and this one, sharp-runtime's
`System::IAsyncResult` gained two new pure-virtual members, which silently broke **every** build
target here (`CNA`, `CNA_GamerServices`, `CNA_Net`, `CnaTests`) via `Storage::SelectorResult`/
`ContainerResult`, `Gamer::GamerAction`, and `Guide.cpp`'s `GuideAction` — none of which
implemented the two new members. This was fixed this session (see sections 2/3), but it means
**"N/N tests pass" claims in this file's history should be re-verified with a real from-scratch
build before being trusted**, not assumed still true. If a future session hits a similar
cross-repo breakage, the fix belongs in *this* repo (implement the missing interface members),
not in sharp-runtime — sharp-runtime's side of such a change is normally intentional/correct
(closer .NET fidelity), and this repo's implementers are what's incomplete.

The entire `GamerServices` namespace is now complete: enums → exceptions → data structs →
event args → Gamer/collections → SignedInGamer → GamerServicesDispatcher/GamerServicesComponent/
Guide. `Gamer` is a full port (verified line-by-line against `Gamer.cs`): it has no `Dispose()`/
`IDisposable` and no `GetHashCode()` override, because the real FNA `Gamer.cs` has neither — an
earlier version of this file incorrectly assumed those were required; that assumption has been
corrected. `IsDisposed`/`Gamertag` remain public-get-only (C# `internal set`), settable only via
the `protected` fields, since no code yet needs to mutate them from outside the hierarchy.
`Gamer::GamerAction`'s wait handle is now `System::Threading::EventWaitHandle` (a real
`WaitHandle`), not `ManualResetEvent` — see section 5, the previous deviation entry for this is
now resolved.

`Net`'s 5 enums are ported, plus 14 of its 18 non-enum classes: `NetworkSessionProperties`,
`QualityOfService`, `AvailableNetworkSession`, `AvailableNetworkSessionCollection`, the 7
event-arg classes, `NetworkSessionJoinException`, `PacketReader`, `PacketWriter`. What's left is
the other 4 non-enum `.cs` files (core session/gamer classes, the nested `NetworkEventType`
enum) — then the ENet backend, then Avatar (deferred, low priority).

---

## 5. Known bugs and limitations

| Status | Issue |
|---|---|
| **Resolved** | ~~`Gamer::GamerAction::getAsyncWaitHandleProperty()` returns `ManualResetEvent&`, not `WaitHandle&`~~ — sharp-runtime's `IAsyncResult` now declares `getAsyncStateProperty()`/`getAsyncWaitHandleProperty()` as real members (it did not when this deviation was first written). `GamerAction`/`GuideAction` now use `System::Threading::EventWaitHandle` (which does derive `WaitHandle`) and properly `override` both methods. `Storage::SelectorResult`/`ContainerResult` needed the same fix (see section 2/4) since they implement the same interface. |
| **Deviation (documented)** | `PacketWriter::Write(Color)` writes 4 bytes (R/G/B/A) but `PacketReader::ReadColor()` reads 4 floats (16 bytes) — genuinely asymmetric upstream, not something introduced here; preserved as-is rather than symmetrized. Not round-trippable through these two methods alone. |
| **Deviation (documented)** | `PacketReader(int capacity)`/`PacketWriter(int capacity)` discard the `capacity` argument — sharp-runtime's `MemoryStream` has no preallocating constructor, and capacity is a pure preallocation hint in .NET with no effect on observable behavior, so nothing is lost. |
| **Deviation (documented)** | `LeaderboardEntry::operator==`/`operator!=` (NOXNA) added purely to satisfy `ReadOnlyCollection<T>`'s virtual `IndexOf`/`Contains` (they're instantiated regardless of use since they override virtuals). FNA's `LeaderboardEntry` has no custom equality (reference identity); this is structural comparison of gamer/rating/ranking, the closest achievable equivalent given value-type storage. Same reasoning applies to `AvailableNetworkSession::operator==`/`operator!=`. |
| **Deviation (documented)** | `AvailableNetworkSessionCollection::Dispose()` only flips `IsDisposed`. FNA's version clears the underlying shared `List<T>` that its `ReadOnlyCollection<T>` wraps *by reference*, so the collection also empties visibly; sharp-runtime's `ReadOnlyCollection<T>` copies its source into private storage with no mutator exposed to a derived class, so there's no way to replicate the emptying without fighting the base class's encapsulation. Matches the precedent of not asserting count-after-dispose in `AchievementCollectionTest`/`FriendCollectionTest` either. |
| **Note (not a bug)** | `SignedInGamer::IsHeadset()` has no unit test — it requires a real `Microsoft::Xna::Framework::Audio::Microphone`, only constructible via `MicrophoneFactory` against a real SDL audio device. Documented per CHECKLIST.md's "classes that cannot be unit-tested" provision. |
| **Note (not a bug)** | `GamerServicesComponent` has no unit tests — it requires a live `Game&` (SDL/graphics backend) to construct, same limitation already documented for `GameComponent` (see `GameComponentTests.cpp`). |
| **Note (not a bug)** | `GamerServicesDispatcher::Initialize()` is never called from the automated test suite. It sets `IsInitialized = true` as process-lifetime static state with no reset hook (matches FNA, which resets it via an `AppDomain.ProcessExit` handler — no C++ equivalent, intentionally omitted); calling it from a test would silently change `UpdateAsync()`'s behavior for every other test in the same binary. |
| **Incomplete** | Phase 4 (Net core classes) is 14/18 done; Phases 5–7 (ENet backend, platform, integration tests) not started. |
| **Incomplete** | `NetworkGamer` doesn't exist yet — `GamerJoinedEventArgs`/`GamerLeftEventArgs`/`HostChangedEventArgs`/`WriteLeaderboardsEventArgs` all forward-declare it and use `nullptr`/pointer stand-ins in tests, same pattern `SignedInGamer*` went through before it existed. |
| **Incomplete** | `FriendCollection` and `SignedInGamerCollection` store raw pointers — ownership model not yet defined. |
| **Incomplete** | `PropertyDictionary` uses `std::any` internally; `GetValueStream` returns a raw `Stream*` — lifetime management unspecified. |
| **Incomplete** | `GamerCollection<T>` template has no range-based-for adapter returning raw `T&` — returns `T*` from `begin()`/`end()`. |
| **Confirmed bug (graphics)** | `SpriteBatch` multiple `Begin()/End()` per frame on Vulkan: only the last batch renders. |
| **Suspected bug (graphics)** | `DrawUserIndexedPrimitives` typed overloads likely have the silent-return-on-missing-effect bug (not yet audited — Task 252). |

---

## 6. Architecture notes

### Module map

| Layer | Location | Notes |
|---|---|---|
| XNA public API (graphics) | `include/Microsoft/Xna/Framework/…` | Must match XNA 4.0 / FNA exactly |
| XNA public API (GamerServices) | `include/Microsoft/Xna/Framework/GamerServices/` | Same rule; internal ctors → `private` + `CreateInternal()` factory |
| XNA public API (Net) | `include/Microsoft/Xna/Framework/Net/` | Enums done; 12/18 core classes done |
| Backend contracts | `include/CNA/Internal/Backends/Common/` | `IGraphicsBackend`, etc. |
| ENet backend (future) | `src/CNA/Internal/Net/` | `ENetBackend.cpp` placeholder only |
| CNA utilities | `include/CNA/`, `src/CNA/` | NOXNA helpers, logging |
| sharp-runtime | `../sharp-runtime/` (sibling repo) | `System.*` types; only add new files |

### Key invariants

- **`NOXNA` macro** tags every non-XNA extension in public headers (e.g. `CreateInternal()`, `begin()`/`end()`).
- **C# `internal` constructors** → `private` in C++, exposed via a `NOXNA static CreateInternal(…)` factory.
- **C# properties** → `getXProperty()` / `setXProperty()` convention.
- **`System::Exception`** is the base class for all GamerServices exceptions (not `std::runtime_error`).
- **GamerCollection<T>** stores raw `T*` pointers — gamers are not owned by the collection.
- **AchievementCollection** owns `Achievement` values by value (`std::vector<Achievement>`).
- **PropertyDictionary** uses `std::any`; typed GetValue/SetValue methods use `std::any_cast<T>`.
- **Template headers** (e.g. `GamerCollection.hpp`) contain full implementation — no `.cpp` counterpart.
- **SPDX header** `// SPDX-License-Identifier: MS-PL` at top of every `.hpp` and `.cpp`.
- **Doxygen** `/** @brief … @param … @return */` required on every public member.
- sharp-runtime: **only add new files**, never modify existing ones.

### GamerServices class dependency order

```
Enums (done) → Exceptions (done) → Data structs (done) → EventArgs (done)
→ Gamer (done) → GamerCollection<T> (done) → FriendGamer (done) / SignedInGamer (done)
→ FriendCollection (done) / SignedInGamerCollection (done)
→ GamerProfile (done) / LeaderboardEntry (done) / LeaderboardWriter (done) / LeaderboardReader (done)
→ SignedInGamer (done)
→ GamerServicesDispatcher (done) / GamerServicesComponent (done) / Guide (done)
→ Net enums → Net core → ENet backend
```

---

## 7. Useful commands

```bash
# Working directory
cd /rv/data/development/github.com/openeggbert/cna_net

# Build GamerServices library
cmake --build cmake-build-debug --target CNA_GamerServices

# Build all (library + Net + tests)
cmake --build cmake-build-debug --target CnaTests

# Run all tests
cmake-build-debug/CnaTests

# Run only GamerServices tests
cmake-build-debug/CnaTests --gtest_filter="*GamerServices*:*Achievement*:*FriendGamer*:*LeaderboardIdentity*:*GamerPresence*:*GamerPrivileges*:*GameDefaults*:*NetworkException*:*SignedIn*:*SignedOut*:*InviteAccepted*"

# FNA reference source
ls /rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/GamerServices/
ls /rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/Net/

# XNA HTML docs
ls /rv/data/development/github.com/openeggbert/xna4-spec/web/

# sharp-runtime include root
ls /rv/data/development/github.com/openeggbert/sharp-runtime/include/System/
```

---

## 8. Next smallest tasks

In priority order:

1. **Task 4.6+ — Continue Net core classes**
   - Goal: `NetworkSessionProperties`, `QualityOfService`, `AvailableNetworkSession`,
     `AvailableNetworkSessionCollection`, all 7 event-arg classes, `NetworkSessionJoinException`,
     `PacketReader`, `PacketWriter` (Tasks 4.1–4.5) are done. Continue with the remaining 4
     non-enum `.cs` files in `Net`. Suggested order (least- to most-dependent): `NetworkGamer`
     (: `Gamer`) / `LocalNetworkGamer` (: `NetworkGamer`) / `NetworkMachine` → finally
     `NetworkSession` itself (the big one — `IDisposable`, owns the nested `NetworkEventType`
     enum and internal `NetworkEvent` struct). Once `NetworkGamer` exists, revisit
     `GamerJoinedEventArgs`/`GamerLeftEventArgs`/`HostChangedEventArgs`/`WriteLeaderboardsEventArgs`
     tests to swap `nullptr` stand-ins for real instances (same as was done for `SignedInGamer`).
   - Files: new headers + cpp under `include/`+`src/Microsoft/Xna/Framework/Net/`.
   - Reference: `/rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/Net/`.
   - Verification: build + tests pass per class, following the same per-file checklist in CHECKLIST.md
     used throughout GamerServices. For any class stored as an element type of a
     `System::Collections::ObjectModel::ReadOnlyCollection<T>` (like `AvailableNetworkSession` was),
     remember its virtual `IndexOf`/`Contains` get instantiated regardless of use, so `T` needs
     `operator==` even if FNA has none — a by-now-recurring, not-a-surprise deviation. Also: builds
     can occasionally time out on this shared machine if another session is compiling concurrently
     (observed load average >100 on a 16-core box) — retry with reduced `-j` and a longer timeout
     rather than assuming a real compile error; check `pgrep -fl cc1plus` before concluding a build
     is stuck.

---

## 9. Do not do yet

- **No ENet backend implementation** until the full XNA Net API surface is declared — wiring ENet
  to an incomplete API wastes effort.
- **No Phase 8 (Avatar)** work yet — Avatar depends on graphics systems not yet audited, and has
  low priority relative to completing GamerServices/Net.
- **No changes to graphics-layer code** during the Net phase — the graphics phase (31) is healthy
  and should not be disturbed. Exception made this session: `Storage/StorageDevice.cpp` needed the
  `IAsyncResult` fix from section 2/4 because it wouldn't compile otherwise — that fix was
  mechanical (implementing two missing interface members) and did not touch behavior.
- **No modifications to existing sharp-runtime files** — another Claude Code instance may be editing
  them in parallel; only add new files. Exception made this session: `Stream.hpp`/`.cpp` and
  `MemoryStream.hpp`/`.cpp` gained additive `Position` support (new members only, nothing existing
  changed) because `PacketReader`/`PacketWriter`'s `Position` property is otherwise unimplementable
  — see section 2. Prefer additive-only changes and verify sharp-runtime's own full test suite
  still passes before relying on this exception again.
- **No public-field shortcuts** — do not replace `getXProperty()`/`setXProperty()` with public fields
  to save time; the convention must be consistent.
- **No raw `std::runtime_error` base** for new exceptions — all new exceptions must inherit
  `System::Exception` as established in Tasks 2.11–2.16.
- **No speculative template instantiation** for `GamerCollection<T>` beyond `FriendGamer` and
  `SignedInGamer` until those types are fully ported.
- **No Avatar work** while core GamerServices/Net classes are incomplete.

---

## 10. Resume prompt

```
Read NEXT.md first. Open only the files needed for the first task listed in section 8.
Do not refactor unrelated code. Do not expand scope beyond the task.

Current status: GamerServices namespace fully ported (Tasks 2.1-2.40); Net namespace's 5 enums
ported (Task 3.1-3.15); 14 of 18 Net non-enum classes ported (Tasks 4.1-4.5: NetworkSessionProperties,
QualityOfService, AvailableNetworkSession, AvailableNetworkSessionCollection, all 7 event-arg
classes, NetworkSessionJoinException, PacketReader, PacketWriter).
1972/1972 unit tests pass; CNA_GamerServices and CNA_Net targets build.

Before trusting that: do a real `cmake --build cmake-build-debug --target CnaTests` yourself first.
sharp-runtime is a sibling repo edited by a separate session with no version pin from here — see
section 4, an interface change there silently broke every build target once already this project.

Next: Task 4.6+ — continue porting Net's remaining 4 non-enum classes. See section 8 for suggested order.
Reference: /rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/Net/
Build: cmake --build cmake-build-debug --target CnaTests
Run:   cmake-build-debug/CnaTests
Update NEXT.md after finishing.
```
