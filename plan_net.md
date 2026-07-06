# Plan: Port FNA.Net to CNA (ENet backend, multiplatform)

Reference source: `/rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/`  
XNA 4.0 API documentation (local HTML): `/rv/data/development/github.com/openeggbert/xna4-spec/web/`  
  — GamerServices: `Microsoft.Xna.Framework.GamerServices/`  
  — Net: `Microsoft.Xna.Framework.Net/`  
Backend: **ENet** (reliable UDP, multiplatform: Windows / Linux / Web / Android)  
XNA namespace targets:
- `Microsoft::Xna::Framework::GamerServices`
- `Microsoft::Xna::Framework::Net`
- `Microsoft::Xna::Framework::GamerServices` (Avatar types)

---

## Overview

Work is split into five areas, each a prerequisite for the next:

1. **Sharp-runtime** — missing .NET types (`IAsyncResult`, `BinaryReader/Writer`, `MemoryStream`, threading, serialization stubs)
2. **GamerServices** — complete port of all 39 source files in `GamerServices/`
3. **Net** — all 23 source files in `Net/`, building on GamerServices
4. **ENet backend** — CNA-internal networking layer wiring XNA API to real network I/O
5. **Avatar** — all 12 source files in `Avatar/` (deferred to the end, lower priority)

---

## Phase 0 — Infrastructure & Build System — ✅ COMPLETE

**Status note:** checkboxes in Phases 0-7 were left unchecked for a long time despite the work
being done — same issue documented for Phase 8 above; `NEXT.md` was the actual live source of
truth. Retroactively checked off here to match reality, confirmed by direct inspection: `enet`
vendored under `third_party/enet/` + `cmake/ThirdPartyENet.cmake` (Windows `ws2_32`/`winmm`
guard, `CNA_ENABLE_NET` option), `CNA_GamerServices`/`CNA_Net` CMake targets in `CMakeLists.txt`,
all GamerServices/Net/ENet-backend source files present under `include/`+`src/`, `sharp-runtime`
prerequisites present, `tests/Microsoft/Xna/Framework/{GamerServices,Net}/` populated, and
`tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp` covering Phase 7's loopback scenario.

- [x] **Task 0.1** — Add ENet as a third-party dependency under `third_party/enet/`  
  Pull ENet 1.3.x source; verify it compiles as a static library on Linux.

- [x] **Task 0.2** — Add ENet CMakeLists integration  
  Create `cmake/FindENet.cmake` or embed directly; expose `enet` target.  
  Guard with `if(CNA_ENABLE_NET)` option (default ON).

- [x] **Task 0.3** — Windows platform support for ENet  
  Link `ws2_32` and `winmm`; confirm ENet compiles on MSVC and MinGW.

- [x] **Task 0.4** — Web (Emscripten) platform support for ENet  
  ENet supports Emscripten via WebSockets wrapper (`-s USE_PTHREADS=0`).  
  Add Emscripten CMake toolchain guards and linker flags (`-lwebsocket.js`).

- [x] **Task 0.5** — Android (NDK) platform support for ENet  
  ENet uses POSIX sockets; Android NDK provides these from API level 21.  
  Add `android-ndk` CMake toolchain guards; no extra libs needed.

- [x] **Task 0.6** — Create `CNA_GamerServices` CMake target  
  Static library; sources under `src/Microsoft/Xna/Framework/GamerServices/`;  
  links against `CNA`.

- [x] **Task 0.7** — Create `CNA_Net` CMake target  
  Static library; sources under `src/Microsoft/Xna/Framework/Net/` and  
  `src/CNA/Internal/Net/`; links against `CNA_GamerServices` + `enet`.

- [x] **Task 0.8** — Add CMakePresets for GamerServices and Net  
  Add `gamerservices-debug`, `net-debug`, and `net-release` presets.

---

## Phase 1 — Sharp-Runtime Prerequisites — ✅ COMPLETE

These .NET runtime types must be added to `sharp-runtime` before any GamerServices or Net code can compile.

- [x] **Task 1.1** — `System::IAsyncResult`  
  Interface: `getAsyncState()`, `getAsyncWaitHandle()`, `getCompletedSynchronously()`, `getIsCompleted()`.  
  File: `include/System/IAsyncResult.hpp`

- [x] **Task 1.2** — `System::Threading::WaitHandle`  
  Abstract base; minimal stub.  
  File: `include/System/Threading/WaitHandle.hpp`

- [x] **Task 1.3** — `System::Threading::ManualResetEvent`  
  Derives from `WaitHandle`; wraps a boolean signaled state.  
  Constructor `ManualResetEvent(bool initialState)`.  
  File: `include/System/Threading/ManualResetEvent.hpp`

- [x] **Task 1.4** — `System::IO::Stream` (if not present)  
  Abstract: `Read()`, `Write()`, `Seek()`, `getLength()`, `getPosition()`, `setPosition()`.  
  File: `include/System/IO/Stream.hpp`

- [x] **Task 1.5** — `System::IO::MemoryStream`  
  Derives from `Stream`; backed by `std::vector<uint8_t>`.  
  Constructors: default, capacity-hint, from-span.  
  Methods: `ToArray()`, `GetBuffer()`, `Seek()`, `Read()`, `Write()`.  
  File: `include/System/IO/MemoryStream.hpp`

- [x] **Task 1.6** — `System::IO::BinaryReader`  
  Wraps `Stream*`; provides `ReadBoolean()`, `ReadByte()`, `ReadInt16()`, `ReadInt32()`,  
  `ReadInt64()`, `ReadSingle()`, `ReadDouble()`, `ReadString()`, `getBaseStream()`.  
  File: `include/System/IO/BinaryReader.hpp`

- [x] **Task 1.7** — `System::IO::BinaryWriter`  
  Wraps `Stream*`; provides `Write()` overloads for all primitive types and `std::string`,  
  `getBaseStream()`, `Flush()`.  
  File: `include/System/IO/BinaryWriter.hpp`

- [x] **Task 1.8** — `System::Runtime::Serialization::SerializationInfo` stub  
  Minimal stub; required by exception protected constructors.  
  File: `include/System/Runtime/Serialization/SerializationInfo.hpp`

- [x] **Task 1.9** — `System::Runtime::Serialization::StreamingContext` stub  
  Same rationale.  
  File: `include/System/Runtime/Serialization/StreamingContext.hpp`

- [x] **Task 1.10** — `System::Collections::ObjectModel::ReadOnlyCollection<T>`  
  Template wrapper around `std::vector<T>`; read-only `operator[]`, `getCount()`, `begin()`, `end()`.  
  File: `include/System/Collections/ObjectModel/ReadOnlyCollection.hpp`

- [x] **Task 1.11** — `System::Globalization::RegionInfo` stub  
  Required by `GamerProfile`. Minimal stub with constructor from locale string.  
  File: `include/System/Globalization/RegionInfo.hpp`

- [x] **Task 1.12** — Sharp-runtime build & unit tests  
  Verify all new types compile and link; add at least one test per new type.

---

## Phase 2 — GamerServices: Complete Port — ✅ COMPLETE

All 39 source files in `GamerServices/`. Three files already have stub headers  
(`GamerServicesComponent.hpp`, `GamerServicesNotAvailableException.hpp`, `Guide.hpp`);  
these must be completed/verified against FNA source.

### 2a — Enums (no dependencies, port first)

- [x] **Task 2.1** — `ControllerSensitivity` (enum)  
  Values: `Low`, `Medium`, `High`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/ControllerSensitivity.hpp`

- [x] **Task 2.2** — `GameDifficulty` (enum)  
  Values: `Easy`, `Normal`, `Hard`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GameDifficulty.hpp`

- [x] **Task 2.3** — `GamerPresenceMode` (enum, 62 values)  
  Copy all values from FNA verbatim.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerPresenceMode.hpp`

- [x] **Task 2.4** — `GamerPrivilegeSetting` (enum)  
  Values: `Blocked`, `FriendsOnly`, `Everyone`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerPrivilegeSetting.hpp`

- [x] **Task 2.5** — `GamerZone` (enum)  
  Values: `Unknown`, `Recreation`, `Pro`, `Family`, `Underground`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerZone.hpp`

- [x] **Task 2.6** — `LeaderboardKey` (enum)  
  Values: `BestScoreLifeTime`, `BestScoreRecent`, `BestTimeLifeTime`, `BestTimeRecent`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/LeaderboardKey.hpp`

- [x] **Task 2.7** — `LeaderboardOutcome` (enum)  
  Values: `None`, `Win`, `Loss`, `Tie`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/LeaderboardOutcome.hpp`

- [x] **Task 2.8** — `MessageBoxIcon` (enum)  
  Values: `None`, `Error`, `Warning`, `Alert`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/MessageBoxIcon.hpp`

- [x] **Task 2.9** — `NotificationPosition` (enum)  
  9 values: `TopLeft` … `BottomRight`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/NotificationPosition.hpp`

- [x] **Task 2.10** — `RacingCameraAngle` (enum)  
  Values: `Back`, `Front`, `Inside`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/RacingCameraAngle.hpp`

### 2b — Exceptions

- [x] **Task 2.11** — `NetworkException`  
  FNA: `GamerServices/NetworkException.cs`  
  Derives from `std::runtime_error` (or `System::Exception` if present).  
  4 constructors: default, message, message+inner, protected serialization.  
  File: `include/Microsoft/Xna/Framework/GamerServices/NetworkException.hpp`

- [x] **Task 2.12** — `NetworkNotAvailableException`  
  Derives from `NetworkException`; same 4-constructor pattern.  
  File: `include/Microsoft/Xna/Framework/GamerServices/NetworkNotAvailableException.hpp`

- [x] **Task 2.13** — `GamerPrivilegeException`  
  Derives from `std::runtime_error`; 4 constructors.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp`

- [x] **Task 2.14** — `GamerServicesNotAvailableException` (complete existing stub)  
  Verify existing `.hpp` against FNA; add `.cpp` if needed.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp`

- [x] **Task 2.15** — `GameUpdateRequiredException`  
  Derives from `std::runtime_error`; 4 constructors.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GameUpdateRequiredException.hpp`

- [x] **Task 2.16** — `GuideAlreadyVisibleException`  
  Derives from `std::runtime_error`; 4 constructors.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp`

### 2c — Data Structures and Simple Classes

- [x] **Task 2.17** — `PropertyDictionary`  
  FNA: `GamerServices/PropertyDictionary.cs`  
  Implements `IDictionary<std::string, System::Object*>`.  
  Backed by `std::unordered_map<std::string, std::shared_ptr<System::Object>>`.  
  Methods: `ContainsKey()`, `TryGetValue()`, `GetEnumerator()`, `operator[]`.  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp`

- [x] **Task 2.18** — `LeaderboardIdentity` (struct)  
  FNA: `GamerServices/LeaderboardIdentity.cs`  
  Properties: `Key` (String), `GameMode` (intcs).  
  Static factory: `Create(LeaderboardKey)`, `Create(LeaderboardKey, int)`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/LeaderboardIdentity.hpp`

- [x] **Task 2.19** — `GamerPresence`  
  FNA: `GamerServices/GamerPresence.cs`  
  Properties: `PresenceMode` (get+set triggers string update), `PresenceValue` (get+set).  
  Internal: `presenceModeStrings[]` static array; `SetPresenceModeStringEXT()` no-op stub.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerPresence.hpp`

- [x] **Task 2.20** — `GamerPrivileges`  
  FNA: `GamerServices/GamerPrivileges.cs`  
  Properties (read-only external): `AllowCommunication`, `AllowOnlineSessions`, `AllowPremiumContent`,  
  `AllowProfileViewing`, `AllowPurchaseContent`, `AllowTradeContent`, `AllowUserCreatedContent`.  
  Internal constructor with stub defaults.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp`

- [x] **Task 2.21** — `GameDefaults`  
  FNA: `GamerServices/GameDefaults.cs`  
  Properties (read-only external): `GameDifficulty`, `ControllerSensitivity`, `PrimaryColor`,  
  `SecondaryColor`, `AutoAim`, `AutoCenter`, `MoveWithRightThumbStick`, `InvertYAxis`,  
  `ManualTransmission`, `RacingCameraAngle`, `AccelerateWithButtons`.  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GameDefaults.hpp`

- [x] **Task 2.22** — `Achievement`  
  FNA: `GamerServices/Achievement.cs`  
  Properties (read-only external): `Description`, `DisplayBeforeEarned`, `EarnedDateTime`,  
  `EarnedOnline`, `GamerScore`, `HowToEarn`, `IsEarned`, `Key`, `Name`.  
  Internal constructor (all fields).  
  File: `include/Microsoft/Xna/Framework/GamerServices/Achievement.hpp`

- [x] **Task 2.23** — `SignedInEventArgs`  
  Derives from `System::EventArgs`; property `Gamer` (`SignedInGamer*`).  
  File: `include/Microsoft/Xna/Framework/GamerServices/SignedInEventArgs.hpp`

- [x] **Task 2.24** — `SignedOutEventArgs`  
  Derives from `System::EventArgs`; property `Gamer` (`SignedInGamer*`).  
  File: `include/Microsoft/Xna/Framework/GamerServices/SignedOutEventArgs.hpp`

- [x] **Task 2.25** — `InviteAcceptedEventArgs`  
  Derives from `System::EventArgs`; properties `Gamer` (`SignedInGamer*`), `IsCurrentSession` (`bool`).  
  File: `include/Microsoft/Xna/Framework/GamerServices/InviteAcceptedEventArgs.hpp`

### 2d — Collection Classes

- [x] **Task 2.26** — `GamerCollection<T>`  
  FNA: `GamerServices/GamerCollection.cs`  
  Template; derives from `System::Collections::ObjectModel::ReadOnlyCollection<T*>`.  
  `T` constrained to `Gamer` subclasses.  
  Internal `collection` field (`std::vector<T*>`).  
  Custom enumerator struct `GamerCollectionEnumerator` with `Current`, `MoveNext()`, `Reset()`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerCollection.hpp`

- [x] **Task 2.27** — `AchievementCollection`  
  FNA: `GamerServices/AchievementCollection.cs`  
  Implements `IList<Achievement*>`, `IDisposable`.  
  `operator[](int)`, `operator[](std::string)` (throws `std::out_of_range` if not found).  
  `IsDisposed`, `getCount()`, `Dispose()`.  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp`

- [x] **Task 2.28** — `FriendGamer`  
  FNA: `GamerServices/FriendGamer.cs`  
  `sealed` → `final`; derives from `Gamer`.  
  Properties (read-only external): `FriendRequestReceivedFrom`, `FriendRequestSentTo`, `HasVoice`,  
  `InviteAccepted`, `InviteReceivedFrom`, `InviteRejected`, `InviteSentTo`, `IsAway`, `IsBusy`,  
  `IsJoinable`, `IsOnline`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp`

- [x] **Task 2.29** — `FriendCollection`  
  FNA: `GamerServices/FriendCollection.cs`  
  `sealed` → `final`; derives from `GamerCollection<FriendGamer>`; implements `IDisposable`.  
  Property `IsDisposed`; method `Dispose()`.  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/GamerServices/FriendCollection.hpp`

- [x] **Task 2.30** — `SignedInGamerCollection`  
  FNA: `GamerServices/SignedInGamerCollection.cs`  
  `sealed` → `final`; derives from `GamerCollection<SignedInGamer>`.  
  `operator[](PlayerIndex)` → returns `SignedInGamer*` (null if out of range).  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp`

### 2e — Core Gamer Classes

- [x] **Task 2.31** — `Gamer` (abstract base class)  
  FNA: `GamerServices/Gamer.cs`  
  Abstract; derives from `System::Object`.  
  Properties: `DisplayName`, `Gamertag`, `IsDisposed`, `LeaderboardWriter`, `Tag`.  
  Static property: `SignedInGamers` (`SignedInGamerCollection*`, lazy-init singleton).  
  Internal `GamerAction` class implementing `IAsyncResult`:  
    fields `AsyncState`, `CompletedSynchronously`, `IsCompleted`, `AsyncWaitHandle`, `Callback`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/Gamer.hpp`

- [x] **Task 2.32** — `GamerProfile`  
  FNA: `GamerServices/GamerProfile.cs`  
  `sealed` → `final`; implements `IDisposable`.  
  Properties (read-only external): `GamerScore`, `GamerZone`, `Motto`, `Region` (`RegionInfo`),  
  `Reputation`, `TitlesPlayed`, `TotalAchievements`, `IsDisposed`.  
  Internal constructor with stub defaults.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerProfile.hpp`

- [x] **Task 2.33** — `LeaderboardEntry`  
  FNA: `GamerServices/LeaderboardEntry.cs`  
  `sealed` → `final`.  
  Properties: `Columns` (`PropertyDictionary`), `Gamer*`, `Rating` (get+set, `longcs`), `RankingEXT`.  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp`

- [x] **Task 2.34** — `LeaderboardWriter`  
  FNA: `GamerServices/LeaderboardWriter.cs`  
  `sealed` → `final`.  
  Method `GetLeaderboard(LeaderboardIdentity)` → throws `std::runtime_error("NotSupportedException")`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/LeaderboardWriter.hpp`

- [x] **Task 2.35** — `LeaderboardReader`  
  FNA: `GamerServices/LeaderboardReader.cs`  
  `sealed` → `final`; implements `IDisposable`.  
  Properties: `IsDisposed`, `CanPageDown`, `CanPageUp`, `Entries` (`ReadOnlyCollection<LeaderboardEntry*>`),  
  `LeaderboardIdentity`, `PageStart`, `TotalLeaderboardSize`.  
  Static async methods: `BeginRead()` overloads, `EndRead()`.  
  Instance methods: `PageDown()`, `PageUp()`, `Dispose()`.  
  Internal state: `entryCache`, `pageSize`, `isFriendBoard`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp`

- [x] **Task 2.36** — `SignedInGamer`  
  FNA: `GamerServices/SignedInGamer.cs`  
  `sealed` → `final`; derives from `Gamer`.  
  Properties: `GameDefaults`, `IsGuest`, `IsSignedInToLive`, `PartySize` (get+set), `PlayerIndex`,  
  `Presence`, `Privileges`.  
  Static events: `SignedIn` (`EventHandler<SignedInEventArgs>`), `SignedOut` (`EventHandler<SignedOutEventArgs>`).  
  Internal `statStoreAction`, `statReceiveAction` (`GamerAction`).  
  Async stat methods: `BeginGetAchievements()`, `EndGetAchievements()`, `BeginGetFriends()`,  
  `EndGetFriends()`, `BeginGetProfile()`, `EndGetProfile()`, etc. (all stub → return empty/null).  
  File: `include/Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp`

### 2f — Static Service Classes

- [x] **Task 2.37** — `GamerServicesDispatcher` (static class)  
  FNA: `GamerServices/GamerServicesDispatcher.cs`  
  Static properties: `IsInitialized`, `WindowHandle` (`intptr_t`).  
  Static event: `InstallingTitleUpdate` (`EventHandler<EventArgs>`).  
  Static methods:  
    `Initialize(IServiceProvider*)` — create 4 stub `SignedInGamer` objects, populate `Gamer::SignedInGamers`.  
    `Update()` — no-op stub.  
    `UpdateAsync() → bool` — returns `false` (signals completion).  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp`

- [x] **Task 2.38** — `GamerServicesComponent` (complete existing stub)  
  FNA: `GamerServices/GamerServicesComponent.cs`  
  Derives from `GameComponent`.  
  `Initialize()` → sets `GamerServicesDispatcher::WindowHandle`, calls `GamerServicesDispatcher::Initialize()`.  
  `Update(GameTime)` → calls `GamerServicesDispatcher::Update()`.  
  Verify existing `.hpp` is correct; add `.cpp`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp`

- [x] **Task 2.39** — `Guide` (static class, complete existing stub)  
  FNA: `GamerServices/Guide.cs`  
  Static properties: `IsScreenSaverEnabled` (maps to SDL3 screensaver), `IsTrialMode`, `IsVisible`,  
  `NotificationPosition`, `SimulateTrialMode`.  
  Static methods: `ShowMessageBox()` (async, stub), `ShowKeyboardInput()` (async, stub),  
  `ShowSignIn()`, `BeginShowMessageBox()`, `EndShowMessageBox()`,  
  `BeginShowKeyboardInput()`, `EndShowKeyboardInput()`.  
  Verify existing `.hpp` is complete; add `.cpp` with SDL3-based screensaver calls.  
  File: `include/Microsoft/Xna/Framework/GamerServices/Guide.hpp`

- [x] **Task 2.40** — Unit tests for Phase 2 types  
  Test all enum values.  
  Test exception constructors (all four variants).  
  Test `GamerCollection<T>` iteration and `operator[]`.  
  Test `AchievementCollection` string indexer throws on miss.  
  Test `GamerServicesDispatcher::Initialize()` populates `Gamer::SignedInGamers` with 4 entries.  
  Test `SignedInGamerCollection::operator[](PlayerIndex)` returns null for out-of-range.

---

## Phase 3 — Net XNA API: Enums and Simple Types — ✅ COMPLETE

- [x] **Task 3.1** — `NetworkSessionEndReason` (enum)  
  Values: `ClientSignedOut`, `HostEndedSession`, `RemovedByHost`, `Disconnected`.  
  File: `include/Microsoft/Xna/Framework/Net/NetworkSessionEndReason.hpp`

- [x] **Task 3.2** — `NetworkSessionJoinError` (enum)  
  Values: `SessionNotFound`, `SessionNotJoinable`, `SessionFull`.  
  File: `include/Microsoft/Xna/Framework/Net/NetworkSessionJoinError.hpp`

- [x] **Task 3.3** — `NetworkSessionState` (enum)  
  Values: `Lobby`, `Playing`, `Ended`.  
  File: `include/Microsoft/Xna/Framework/Net/NetworkSessionState.hpp`

- [x] **Task 3.4** — `NetworkSessionType` (enum)  
  Values: `Local`, `SystemLink`, `PlayerMatch`, `Ranked`, `LocalWithLeaderboards`.  
  File: `include/Microsoft/Xna/Framework/Net/NetworkSessionType.hpp`

- [x] **Task 3.5** — `SendDataOptions` (enum, flags)  
  Values: `None=0`, `Reliable=1`, `InOrder=2`, `ReliableInOrder=3`, `Chat=4`.  
  Use `enum class` with bitwise operator overloads (`|`, `&`, `~`).  
  File: `include/Microsoft/Xna/Framework/Net/SendDataOptions.hpp`

- [x] **Task 3.6** — `QualityOfService`  
  Properties (read-only): `AverageRoundtripTime` (`TimeSpan`), `BytesPerSecondDownstream` (`intcs`),  
  `BytesPerSecondUpstream` (`intcs`), `IsAvailable` (`bool`), `MinimumRoundtripTime` (`TimeSpan`).  
  Internal constructor only.  
  File: `include/Microsoft/Xna/Framework/Net/QualityOfService.hpp`

- [x] **Task 3.7** — `NetworkSessionProperties`  
  Implements `IList<std::optional<intcs>>`, `ICollection`, `IEnumerable`.  
  Backed by `std::vector<std::optional<intcs>>`.  
  Public `operator[]`, `getCount()`, `GetEnumerator()`.  
  IList: `IndexOf()`, `Insert()`, `RemoveAt()`.  
  ICollection: `IsReadOnly`, `Add()`, `Remove()`, `Contains()`, `Clear()`, `CopyTo()`.  
  File: `include/Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp`

- [x] **Task 3.8** — `GameEndedEventArgs`  
  Derives from `System::EventArgs`; default constructor only.  
  File: `include/Microsoft/Xna/Framework/Net/GameEndedEventArgs.hpp`

- [x] **Task 3.9** — `GameStartedEventArgs`  
  Derives from `System::EventArgs`; default constructor only.  
  File: `include/Microsoft/Xna/Framework/Net/GameStartedEventArgs.hpp`

- [x] **Task 3.10** — `GamerJoinedEventArgs`  
  Derives from `System::EventArgs`; property `Gamer` (`NetworkGamer*`).  
  File: `include/Microsoft/Xna/Framework/Net/GamerJoinedEventArgs.hpp`

- [x] **Task 3.11** — `GamerLeftEventArgs`  
  Derives from `System::EventArgs`; property `Gamer` (`NetworkGamer*`).  
  File: `include/Microsoft/Xna/Framework/Net/GamerLeftEventArgs.hpp`

- [x] **Task 3.12** — `HostChangedEventArgs`  
  Derives from `System::EventArgs`; properties `OldHost`, `NewHost` (`NetworkGamer*`).  
  File: `include/Microsoft/Xna/Framework/Net/HostChangedEventArgs.hpp`

- [x] **Task 3.13** — `NetworkSessionEndedEventArgs`  
  Derives from `System::EventArgs`; property `EndReason` (`NetworkSessionEndReason`).  
  File: `include/Microsoft/Xna/Framework/Net/NetworkSessionEndedEventArgs.hpp`

- [x] **Task 3.14** — `WriteLeaderboardsEventArgs`  
  Derives from `System::EventArgs`; properties `Gamer` (`NetworkGamer*`), `IsLeaving` (`bool`).  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/Net/WriteLeaderboardsEventArgs.hpp`

- [x] **Task 3.15** — `NetworkSessionJoinException`  
  Derives from `GamerServices::NetworkException`.  
  Property `JoinError` (`NetworkSessionJoinError`).  
  4 constructors: default, message, message+error, message+inner; protected serialization ctor.  
  File: `include/Microsoft/Xna/Framework/Net/NetworkSessionJoinException.hpp`

- [x] **Task 3.16** — Unit tests for Phase 3 types  
  All constructors, enum values, `NetworkSessionProperties` indexer and collection interface,  
  `SendDataOptions` bitwise operations.

---

## Phase 4 — Net XNA API: Core Classes — ✅ COMPLETE

- [x] **Task 4.1** — `NetworkMachine`  
  Property `Gamers` (`GamerCollection<NetworkGamer>`).  
  Method `RemoveFromSession()` → throws `std::runtime_error("NotImplementedException")`.  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/Net/NetworkMachine.hpp`

- [x] **Task 4.2** — `NetworkGamer`  
  Derives from `GamerServices::Gamer`.  
  Properties: `HasLeftSession`, `HasVoice`, `getId()` (`bytecs`, returns 0), `IsGuest`,  
  `IsHost` (returns true), `IsLocal` (returns `dynamic_cast<LocalNetworkGamer*>(this) != nullptr`),  
  `IsMutedByLocalUser`, `IsPrivateSlot`, `IsReady` (get+set), `IsTalking`,  
  `Machine` (`NetworkMachine`), `RoundtripTime` (`TimeSpan`), `Session` (`NetworkSession*`).  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/Net/NetworkGamer.hpp`

- [x] **Task 4.3** — `LocalNetworkGamer` (`final`)  
  Derives from `NetworkGamer`.  
  Properties: `IsDataAvailable` (queue non-empty), `SignedInGamer` (`GamerServices::SignedInGamer*`).  
  Internal: `packetQueue` (`std::queue<NetworkSession::NetworkEvent>`).  
  Methods: `EnableSendVoice()` (no-op), `SendPartyInvites()` (no-op).  
  `ReceiveData(byte[], NetworkGamer*&)`.  
  `ReceiveData(byte[], int, NetworkGamer*&)`.  
  `ReceiveData(PacketReader&, NetworkGamer*&)`.  
  `SendData(byte[], SendDataOptions)`.  
  `SendData(byte[], int, int, SendDataOptions)`.  
  `SendData(byte[], SendDataOptions, NetworkGamer*)`.  
  `SendData(byte[], int, int, SendDataOptions, NetworkGamer*)`.  
  `SendData(PacketWriter&, SendDataOptions)`.  
  `SendData(PacketWriter&, SendDataOptions, NetworkGamer*)`.  
  File: `include/Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp`

- [x] **Task 4.4** — `PacketReader`  
  Derives from `System::IO::BinaryReader`.  
  Properties: `getLength()`, `getPosition()`, `setPosition()`.  
  Constructors: default, capacity-hint.  
  Extra read methods: `ReadColor()`, `ReadMatrix()`, `ReadQuaternion()`, `ReadVector2()`, `ReadVector3()`, `ReadVector4()`.  
  Override `ReadSingle()`, `ReadDouble()`.  
  File: `include/Microsoft/Xna/Framework/Net/PacketReader.hpp`

- [x] **Task 4.5** — `PacketWriter`  
  Derives from `System::IO::BinaryWriter`.  
  Properties: `getLength()`, `getPosition()`, `setPosition()`.  
  Constructors: default, capacity-hint.  
  Extra write methods: `Write(Color)`, `Write(Matrix)`, `Write(Quaternion)`, `Write(Vector2)`, `Write(Vector3)`, `Write(Vector4)`.  
  Override `Write(float)`, `Write(double)`.  
  File: `include/Microsoft/Xna/Framework/Net/PacketWriter.hpp`

- [x] **Task 4.6** — `AvailableNetworkSession`  
  Properties (read-only): `CurrentGamerCount`, `HostGamertag`, `OpenPrivateGamerSlots`,  
  `OpenPublicGamerSlots`, `QualityOfService`, `SessionProperties`.  
  Internal constructor (all fields).  
  File: `include/Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp`

- [x] **Task 4.7** — `AvailableNetworkSessionCollection` (`final`)  
  Derives from `System::Collections::ObjectModel::ReadOnlyCollection<AvailableNetworkSession*>`.  
  Implements `System::IDisposable`.  
  Property `IsDisposed`.  
  Method `Dispose()` — clears collection, sets flag.  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/Net/AvailableNetworkSessionCollection.hpp`

- [x] **Task 4.8** — `NetworkSession` — internal types  
  Internal `NetworkEventType` enum: `PacketSend`, `GamerJoin`, `GamerLeave`, `HostChange`, `StateChange`.  
  Internal `NetworkEvent` struct: `Type`, `Gamer*`, `Packet` (`std::vector<bytecs>`),  
  `Reliable` (`SendDataOptions`), `State`, `Reason`.  
  Internal `NetworkSessionAction` class implementing `IAsyncResult`.  
  (Private section of `NetworkSession.hpp`.)

- [x] **Task 4.9** — `NetworkSession` — public properties  
  Constants `MaxSupportedGamers = 31`, `MaxPreviousGamers = 100`.  
  Properties: `IsDisposed`, `AllGamers`, `LocalGamers`, `RemoteGamers`, `PreviousGamers`,  
  `AllowHostMigration`, `AllowJoinInProgress`, `BytesPerSecondReceived`, `BytesPerSecondSent`,  
  `Host`, `IsEveryoneReady`, `IsHost`, `MaxGamers`, `PrivateGamerSlots`, `SessionProperties`,  
  `SessionState`, `SessionType`, `SimulatedLatency`, `SimulatedPacketLoss`.

- [x] **Task 4.10** — `NetworkSession` — public events  
  Instance events: `GameStarted`, `GameEnded`, `GamerJoined`, `GamerLeft`, `HostChanged`,  
  `SessionEnded`, `WriteArbitratedLeaderboard`, `WriteUnarbitratedLeaderboard`, `WriteTrueSkill`.  
  Static event: `InviteAccepted`.

- [x] **Task 4.11** — `NetworkSession` — constructor and `Dispose()`  
  Internal constructor; initialise gamer lists, event queue, host pointer.  
  `Dispose()`: flush packet queues, clear static `activeSession_`.

- [x] **Task 4.12** — `NetworkSession` — `Update()` method  
  Drain `networkEvents_` queue; dispatch events by type.

- [x] **Task 4.13** — `NetworkSession` — session management methods  
  `AddLocalGamer()`, `FindGamerById()`, `ResetReady()`, `StartGame()`, `EndGame()`.  
  Internal `SendNetworkEvent()`.

- [x] **Task 4.14** — `NetworkSession` — static Create methods  
  `Create(type, maxLocal, maxGamers)`.  
  `Create(type, maxLocal, maxGamers, privateSlots, props)`.  
  `Create(type, localGamers, maxGamers, privateSlots, props)`.  
  All poll `GamerServicesDispatcher::UpdateAsync()`.

- [x] **Task 4.15** — `NetworkSession` — static BeginCreate / EndCreate  
  Three `BeginCreate` overloads; validate args; populate `activeAction_`.  
  `EndCreate` constructs session; clears action.

- [x] **Task 4.16** — `NetworkSession` — static Find methods  
  `Find(type, maxLocal, props)`, `Find(type, localGamers, props)`.  
  `BeginFind` (2 overloads), `EndFind`.

- [x] **Task 4.17** — `NetworkSession` — static Join methods  
  `Join(availableSession)`, `BeginJoin`, `EndJoin`.  
  `JoinInvited(maxLocal)`, `JoinInvited(localGamers)`.  
  `BeginJoinInvited` (2 overloads), `EndJoinInvited`.

- [x] **Task 4.18** — Unit tests for Phase 4 types  
  PacketReader/PacketWriter round-trip for each XNA type.  
  `NetworkSessionProperties` indexer and IList interface.  
  `NetworkSession` state machine: Create → StartGame → EndGame → Dispose.  
  `GamerJoined` event fires on construction.  
  `FindGamerById` returns correct gamer.  
  `ResetReady` throws when not host.  
  `AvailableNetworkSessionCollection` Dispose clears.

---

## Phase 5 — ENet Backend (CNA Internal Layer) — ✅ COMPLETE

All backend code under `src/CNA/Internal/Net/` and `include/CNA/Internal/Net/`.

### 5a — Backend Contract

- [x] **Task 5.1** — Define `CNA::Internal::Net::INetworkBackend` interface  
  Methods: `Initialize()`, `Shutdown()`, `HostSession()`, `FindSessions()`, `JoinSession()`,  
  `SendPacket()`, `Poll()`, `GetRTT()`, `DisconnectPeer()`, `DestroySession()`.  
  File: `include/CNA/Internal/Net/INetworkBackend.hpp`

- [x] **Task 5.2** — Define supporting data types  
  `NetworkSessionConfig`, `SessionQuery`, `DiscoveredSession`, `NetEvent`, `SessionHandle`, `PeerHandle`.  
  File: `include/CNA/Internal/Net/NetTypes.hpp`

### 5b — ENet Implementation

- [x] **Task 5.3** — `ENetBackend::Initialize()` / `Shutdown()`  
  `enet_initialize()` / `enet_deinitialize()`.

- [x] **Task 5.4** — Session advertisement for `SystemLink` (LAN UDP broadcast)  
  Host sends broadcast on port 3074; heartbeat every 2 s.  
  Find listens for replies; collects `DiscoveredSession` list.

- [x] **Task 5.5** — Session advertisement for `PlayerMatch` (relay/direct)  
  Use relay server address from `CNA_NET_RELAY_HOST` env var; LAN fallback if unset.

- [x] **Task 5.6** — `ENetBackend::JoinSession()`  
  Connect ENet client to host; assign `PeerHandle`; send gamer-list on connect.

- [x] **Task 5.7** — `ENetBackend::SendPacket()` with channel mapping  
  `None` → unreliable ch0; `Reliable` → reliable ch0; `InOrder` → unreliable-sequenced ch1;  
  `ReliableInOrder` → reliable ch1; `Chat` → reliable ch2.

- [x] **Task 5.8** — `ENetBackend::Poll()`  
  Non-blocking `enet_host_service()`; translate ENet events → `NetEvent` list.

- [x] **Task 5.9** — Host migration  
  On host disconnect: elect new host (lowest peer ID); fire `HostChanged`; update `Host`.

- [x] **Task 5.10** — Latency simulation  
  `enet_peer_throttle_configure()` for loss; delay queue for latency.

- [x] **Task 5.11** — QoS measurement  
  Populate `QualityOfService` from `enet_peer->roundTripTime` and bandwidth counters.

### 5c — Wire XNA API to ENet Backend

- [x] **Task 5.12** — Wire `EndCreate` → `INetworkBackend::HostSession()`
- [x] **Task 5.13** — Wire `EndFind` → `INetworkBackend::FindSessions()` → `AvailableNetworkSession` list
- [x] **Task 5.14** — Wire `EndJoin` → `INetworkBackend::JoinSession()`
- [x] **Task 5.15** — Wire `NetworkSession::Update()` → `INetworkBackend::Poll()` → enqueue events
- [x] **Task 5.16** — Wire `LocalNetworkGamer::SendData()` → `INetworkBackend::SendPacket()`
- [x] **Task 5.17** — Wire incoming data → `LocalNetworkGamer::packetQueue`
- [x] **Task 5.18** — Wire `NetworkSession::Dispose()` → `INetworkBackend::DestroySession()`

---

## Phase 6 — Platform-Specific Work — ✅ COMPLETE

- [x] **Task 6.1** — Linux: two-process loopback test (host + client, same machine)
- [x] **Task 6.2** — Windows: ENet with WinSock2; same loopback test
- [x] **Task 6.3** — Web (Emscripten): ENet WebSocket adaptation; disable `SystemLink`; relay only
- [x] **Task 6.4** — Android (NDK): add `INTERNET` permission to manifest; test on emulator
- [x] **Task 6.5** — Multiplatform CMake guards (`if(EMSCRIPTEN)`, `if(ANDROID)`, `if(WIN32)`)

---

## Phase 7 — Integration Tests — ✅ COMPLETE

- [x] **Task 7.1** — Two-endpoint loopback: host + join in same process; PacketWriter/Reader round-trip
- [x] **Task 7.2** — State machine: Create → StartGame → EndGame → Dispose; verify events fire
- [x] **Task 7.3** — GamerJoined / GamerLeft event dispatch
- [x] **Task 7.4** — FindSessions returns discovered host entry
- [x] **Task 7.5** — SendDataOptions channel mapping (unreliable vs reliable)
- [x] **Task 7.6** — ResetReady clears all gamer ready flags
- [x] **Task 7.7** — NetworkSessionJoinException round-trip through all 4 constructors

---

## Phase 8 — Avatar (Deferred, Lower Priority) — ✅ COMPLETE

All Avatar types live in the `Microsoft::Xna::Framework::GamerServices` namespace.  
Port after all GamerServices and Net work is stable.

**Status note:** this phase's checkboxes were left unchecked in earlier revisions of this file —
checkboxes here were historically never maintained live; `NEXT.md` was the actual source of truth
for status. They are now checked off retroactively to match reality: FNA has zero real Avatar
implementation, so this port was done from the real, genuine Microsoft
`Microsoft.Xna.Framework.Avatar.dll` reference assembly (decompiled via `monodis`) instead —
see `NEXT.md` for the full methodology and the list of verified-real behavioral quirks preserved
faithfully (e.g. `AvatarRenderer.Draw()` is a genuine, permanent no-op off-Xbox). Committed and
pushed (`1a482b0`).

### 8a — Enums

- [x] **Task 8.1** — `AvatarAnimationPreset` (enum, 31 values)  
  Copy all values from FNA verbatim.  
  File: `include/Microsoft/Xna/Framework/GamerServices/AvatarAnimationPreset.hpp`

- [x] **Task 8.2** — `AvatarBodyType` (enum)  
  Values: `Female`, `Male`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/AvatarBodyType.hpp`

- [x] **Task 8.3** — `AvatarBone` (enum, 71 values with gaps)  
  Copy all values and explicit numeric assignments from FNA verbatim.  
  File: `include/Microsoft/Xna/Framework/GamerServices/AvatarBone.hpp`

- [x] **Task 8.4** — `AvatarEye` (enum, 14 values)  
  File: `include/Microsoft/Xna/Framework/GamerServices/AvatarEye.hpp`

- [x] **Task 8.5** — `AvatarEyebrow` (enum, 5 values)  
  File: `include/Microsoft/Xna/Framework/GamerServices/AvatarEyebrow.hpp`

- [x] **Task 8.6** — `AvatarMouth` (enum, 14 values)  
  File: `include/Microsoft/Xna/Framework/GamerServices/AvatarMouth.hpp`

- [x] **Task 8.7** — `AvatarRendererState` (enum)  
  Values: `Loading`, `Ready`, `Unavailable`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/AvatarRendererState.hpp`

### 8b — Structs and Interfaces

- [x] **Task 8.8** — `AvatarExpression` (struct)  
  Properties (get+set): `Mouth` (`AvatarMouth`), `LeftEye`, `RightEye` (`AvatarEye`),  
  `LeftEyebrow`, `RightEyebrow` (`AvatarEyebrow`).  
  File: `include/Microsoft/Xna/Framework/GamerServices/AvatarExpression.hpp`

- [x] **Task 8.9** — `IAvatarAnimation` (interface)  
  Properties: `BoneTransforms` (`ReadOnlyCollection<Matrix>`), `CurrentPosition` (get+set, `TimeSpan`),  
  `Length` (`TimeSpan`), `Expression` (`AvatarExpression`).  
  Method: `Update(TimeSpan, bool)`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/IAvatarAnimation.hpp`

### 8c — Classes

- [x] **Task 8.10** — `AvatarAnimation`  
  FNA: `Avatar/AvatarAnimation.cs`  
  Derives from `IAvatarAnimation`, implements `IDisposable`.  
  Properties: `BoneTransforms` (71 default-constructed `Matrix` objects in `ReadOnlyCollection`),  
  `CurrentPosition` (get+set), `Length`, `Expression`.  
  Constructors: `AvatarAnimation(AvatarAnimationPreset)`.  
  Methods: `Update(TimeSpan, bool)` (no-op), `Dispose()` (no-op).  
  File: `include/Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp`

- [x] **Task 8.11** — `AvatarDescription`  
  FNA: `Avatar/AvatarDescription.cs`  
  Properties: `Description` (`std::vector<bytecs>`, copy on access), `IsValid` (description[0] != 0),  
  `Height` (`float`), `BodyType` (`AvatarBodyType`).  
  Internal constant `descriptionSize = 1021`.  
  Static event `Changed` (`EventHandler<EventArgs>`).  
  Internal `AvatarDescriptionAction` implementing `IAsyncResult`.  
  Public constructor from `byte[]` (validates length).  
  Internal constructor from `bool isValid`.  
  Static methods: `CreateRandom()`, `CreateRandom(AvatarBodyType)`,  
  `BeginGetFromGamer(Gamer*, AsyncCallback, void*)`, `EndGetFromGamer(IAsyncResult*)`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp`

- [x] **Task 8.12** — `AvatarRenderer`  
  FNA: `Avatar/AvatarRenderer.cs`  
  Derives from `IDisposable`.  
  Constant `BoneCount = 71`.  
  Properties: `World`, `View`, `Projection` (`Matrix`, get+set); `ParentBones`  
  (`ReadOnlyCollection<intcs>`, 71 fixed values from FNA); `BindPose` (throws `std::runtime_error`);  
  `State` (`AvatarRendererState`, starts `Unavailable`); `LightColor`, `LightDirection`,  
  `AmbientLightColor` (`Vector3`, get+set); `IsDisposed`.  
  Constructors: `AvatarRenderer(AvatarDescription*)`, `AvatarRenderer(AvatarDescription*, bool)`.  
  Methods: `Draw(IAvatarAnimation*)` (no-op), `Draw(std::vector<Matrix>, AvatarExpression)` (no-op),  
  `Dispose()`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp`

- [x] **Task 8.13** — Unit tests for Phase 8 types  
  All enum values present.  
  `AvatarDescription` constructor validates length (throws on wrong size).  
  `AvatarDescription::CreateRandom()` returns valid description.  
  `AvatarRenderer::BoneCount == 71`.  
  `AvatarRenderer::BindPose` throws.  
  `AvatarAnimation` BoneTransforms has 71 elements.

---

## Phase 9 — Documentation and Audit — ✅ COMPLETE

- [x] **Task 9.1** — Doxygen comments on all public `.hpp` files in `GamerServices/` and `Net/`
- [x] **Task 9.2** — Add GamerServices and Net sections to `AUDIT.md`
- [x] **Task 9.3** — Update `NEXT.md` with Net/GamerServices/Avatar handoff notes
- [x] **Task 9.4** — Update `README.md` to mention GamerServices, Net, Avatar subsystems and ENet dependency

With Phase 9 complete, **the entire original plan (Phases 0-9) is done.** Everything below (Phase
10, Phase 11) is new work added after the original plan was finished — see each phase's own intro
for rationale.

---

## Phase 10 — Avatar Real-Rendering Extension (NOXNA/EXT) — ✅ COMPLETE

Not part of the original plan — added after Phase 9 completed, when the user asked for Avatar
rendering to actually draw something real instead of remaining a permanent, faithful no-op. Full
design doc: `docs/avatar-real-rendering-ext.md`. Full session narrative: `NEXT.md`. This phase
built the **engine/pipeline side only** — a real GPU-skinned-mesh rendering path, proven against a
synthetic test fixture. It intentionally does **not** include any real avatar body/animation
content; that is Phase 11's job.

Key design point: this is an entirely additive, opt-in (`NOXNA`/`*EXT`-tagged) layer. It never
changes the faithful XNA-spec `AvatarRenderer`/`AvatarAnimation`/`AvatarDescription` behavior from
Phase 8 — those stay exactly as ported, and all their existing tests still pass unmodified.

- [x] **Task 10.1** — `Graphics::VertexPositionNormalTextureSkinned` (NOXNA GPU-skinned vertex:
  position/normal/texcoord/4 blend weights/4 blend indices) + matching `VertexBuffer::SetData`
  overloads.  
  Files: `include/`+`src/Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.{hpp,cpp}`

- [x] **Task 10.2** — `Graphics::SkinnedModelEXT` + `AnimationClipEXT`/`BoneTrackEXT`/`KeyframeEXT`
  containers, with `ComputeBoneTransformsEXT` (Lerp/Slerp interpolation, bone-hierarchy world
  transform composition). Deliberately not built on `Model`/`ModelBone` (those are for *rigid*
  multi-part model animation, the wrong shape for per-vertex GPU skinning). Its bone hierarchy is
  fully independent of `AvatarRenderer`'s real 71-bone Xbox arrays from Phase 8.  
  Files: `include/`+`src/Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.{hpp,cpp}`

- [x] **Task 10.3** — `SkinnedModelTypeReader` content-pipeline reader (new
  `.skinnedmodel.json`/`.skeleton.bin`/`.clip.bin` schema), registered in
  `ContentManager::RegisterBuiltinLoaders()`. Hand-rolled JSON parsing, matching the existing
  `ModelTypeReader`/`SpriteFontTypeReader` style (no new JSON library dependency).  
  File: `src/Microsoft/Xna/Framework/Content/ContentManager.cpp`

- [x] **Task 10.4** — Unit tests for Tasks 10.1–10.3 (synthetic fixtures, no real assets required).

- [x] **Task 10.5** — `AvatarAnimationPresetToClipNameEXT` helper mapping all 31
  `AvatarAnimationPreset` values to clip-name lookup keys.  
  Files: `include/`+`src/Microsoft/Xna/Framework/GamerServices/AvatarAnimationPresetNamesEXT.{hpp,cpp}`

- [x] **Task 10.6** — `AvatarAppearanceEXT` struct (CNA-invented skin/hair tint — explicitly not a
  reconstruction of the real, undocumented, proprietary 1021-byte `AvatarDescription` format).  
  File: `include/Microsoft/Xna/Framework/GamerServices/AvatarAppearanceEXT.hpp`

- [x] **Task 10.7** — `AvatarRenderer::EnableRealRenderingEXT`/`IsRealRenderingEnabledEXT`/
  `SetAppearanceEXT`/`DrawRealEXT`, fully decoupled from the faithful `Draw()` overloads and
  71-bone arrays (both untouched, still tested unchanged).  
  Files: `AvatarRenderer.{hpp,cpp}`

- [x] **Task 10.8** — `AvatarAnimation::SetRealClipNameEXT`/`GetRealClipNameEXT`, defaulting to
  `AvatarAnimationPresetToClipNameEXT(preset)` at construction.  
  Files: `AvatarAnimation.{hpp,cpp}`

- [x] **Task 10.9** — Unit tests for Tasks 10.5–10.8.

- [x] **Task 10.10** — `examples/avatar_real_render_integration_test.cpp`: real GPU-skinned
  rendering through the entire `AvatarRenderer::EnableRealRenderingEXT`/`DrawRealEXT` →
  `SkinnedModelEXT::ComputeBoneTransformsEXT` → `SkinnedEffect` → `GraphicsDevice` pipeline,
  pixel-readback verified on EasyGL. Registered in `CMakeLists.txt` as
  `EasyGL_AvatarRenderer_RealRender`.

- [x] **Task 10.11** — Docs: `docs/avatar-real-rendering-ext.md`, `THIRD_PARTY_NOTICES.md`,
  `AUDIT.md` rows for the new types/members.

- [ ] **Task 10.12** — (Optional, non-blocking) Vulkan/Bgfx smoke test of the real-rendering
  extension. Needs a fresh `cmake-build-vulkan` configure and `glslc` (not installed as of this
  writing).

Committed and pushed to `feature/net` (`2b653bc`, `7b56eab`, `1cc42d1`).

---

## Phase 11 — Procedural Avatar Asset Generator (Blender Pipeline)

**11a complete (Tasks 11.1-11.9); 11b complete (Tasks 11.10-11.12); 11c complete (Tasks 11.13-11.15). Only 11d (Task 11.16, optional/future) remains.** Added after the user decided CNA should actually have avatars, and that the real
body/skeleton/animation content should be generated by Claude Code itself via a procedural Blender
pipeline — rather than relying on external GUI tools (MakeHuman) or external character-creator
repos (CharMorph/Blender), both of which were attempted in the same session and stopped by
permission-classifier safety gates around downloading/executing third-party tooling and code (see
`NEXT.md` section 3 for the full account). A procedural generator sidesteps that class of problem
entirely: every script is original code written by Claude Code, run through the already-installed,
already-trusted local Blender (confirmed this session: Blender 4.3.2, `blender --background
--python script.py` runs fully headless) — no third-party downloads, no unfamiliar external
codebase to execute.

**Key design advantage over MakeHuman/CharMorph:** those approaches needed bone-name retargeting
between an externally-authored rig (MakeHuman's "Mixamo" preset, or CharMorph's Rigify rig) and
externally-sourced Mixamo animation clips — a real source of engineering risk and visual artifacts
(documented in `NEXT.md`). Here, **the same script that builds the skeleton also authors the
animations**, on that exact skeleton, so bone names match by construction. Zero retargeting.

Scope honestly set at "functional, not polished": procedural low-poly stylized geometry, a small
number of shape keys, and a couple of placeholder animations for the first milestone. Realistic
hair/faces/clothing-without-clipping/production-quality skin weights are explicitly **not** a
near-term goal — see the "Do not do yet" style caveats per task below, and iterate visually
(screenshots from Blender) before calling any stage "done."

### 11a — Pipeline Foundation (first milestone: one male + one female avatar that draws)

- [x] **Task 11.1** — `tools/avatar_builder/generate_skeleton.py`  
  Builds a **new, CNA-original canonical skeleton** via `bpy` armature edit-mode bone creation —
  NOT the real Xbox 71-bone hierarchy (Phase 8's `AvatarRenderer::ParentBones` stays untouched and
  unrelated) and NOT Mixamo/Rigify naming. A compact biped is enough for a first milestone: e.g.
  `Hips, Spine, Spine1, Neck, Head, Shoulder.L/R, UpperArm.L/R, LowerArm.L/R, Hand.L/R,
  UpperLeg.L/R, LowerLeg.L/R, Foot.L/R` (~19 bones). Document the exact list and hierarchy in
  `tools/avatar_builder/README.md` as the single source of truth other scripts key off of.
  **Done:** `generate_skeleton.py` builds `CNAAvatarSkeleton` (19 bones) via `bpy`
  edit-mode bone creation and exposes `build_skeleton()`/`BONES` for later scripts to
  reuse; full bone/parent/position table documented in `tools/avatar_builder/README.md`.
  Verified: `blender --background --python tools/avatar_builder/generate_skeleton.py`
  runs clean and asserts every bone name/parent against the documented table.

- [x] **Task 11.2** — `tools/avatar_builder/generate_body.py`  
  Procedural stylized low-poly humanoid mesh (head/torso/arms/legs from primitive-derived shapes,
  e.g. scaled cubes/capsules/UV-spheres, subdivided/shaped as needed). Parent to the Task 11.1
  armature with automatic weights (`bpy.ops.object.parent_set(type='ARMATURE_AUTO')`) as the
  starting point — expect and document that automatic weights need manual correction passes for
  elbows/knees/shoulders before this looks acceptable in motion; do not claim "done" until a
  visual check confirms no gross bending artifacts on the Task 11.6 test animations.
  **Done:** `generate_body.py` builds one cylinder+joint-sphere "flesh" primitive per bone
  (from Task 11.1's own `BONES` head/tail positions), joins them into a single
  `CNAAvatarBody` mesh, and parents it to the skeleton with
  `parent_set(type='ARMATURE_AUTO')`. Verified headless: all 19 bones get a non-empty
  vertex group (1086 vertices). A manual `BLENDER_WORKBENCH` render (T-pose, plus a
  pose-mode elbow/knee bend test) confirmed reasonable proportions and no mesh tearing
  at the bend — **but this is not the plan's full bend-artifact check**, which needs
  Task 11.6's real test animations; automatic weights will very likely still need manual
  correction once those exist.

- [x] **Task 11.3** — `tools/avatar_builder/generate_materials.py`  
  Minimal PBR materials: skin, hair, shirt, pants, shoes, each a simple base-color parameter (no
  texture painting at this stage).
  **Done:** `generate_materials.py` creates 5 flat-color Principled BSDF materials
  (`CNAAvatarSkin/Hair/Shirt/Pants/Shoes`) via `build_materials()` and assigns `Skin` to
  the Task 11.2 body mesh via `assign_body_material()`. `Hair`/`Shirt`/`Pants`/`Shoes`
  are created but not yet assigned anywhere — no hair/clothing geometry exists until
  Task 11.5. Verified: headless run confirms all 5 materials exist and `Skin` is the
  body mesh's sole material slot; a manual `BLENDER_WORKBENCH` material-preview render
  confirmed the skin tone actually renders (not just node-graph plumbing that silently
  no-ops).

- [x] **Task 11.4** — `tools/avatar_builder/generate_morphs.py`  
  At least two shape keys: `Smile`, `Blink`. Document how additional morphs get added later.
  **Done:** `generate_morphs.py` adds `Smile`/`Blink` shape keys to the Task 11.2 body
  mesh via `build_morphs()`. Since the head is a single low-poly UV sphere with no
  separate eye/mouth geometry (Task 11.2), vertex selection is done by radius-relative
  latitude ring (the sphere's fixed z/radius ratios) and forward-facing (+Y) position —
  an explicitly crude, documented placeholder (see `tools/avatar_builder/README.md`'s
  "Adding more morphs" section), not modeled facial geometry. Verified: headless run
  asserts both shape keys exist and each displaces at least one vertex by a non-trivial
  amount; a manual render at `value=1.0` for both showed a visibly different (if crude)
  head shape, confirming the deformation is real, not just non-zero numbers.

- [x] **Task 11.5** — `tools/avatar_builder/generate_hair.py` + `generate_clothes.py`  
  Simple placeholder geometry layered over the body (hair as a basic cap/helmet-like shape;
  shirt/pants/shoes as offset shells over the body mesh). Explicitly expected to look crude at
  this stage (per the ChatGPT-sourced analysis this phase was scoped from: "vlasy... budou vypadat
  jako helma" is an accepted, known limitation of a first pass, not a bug to chase down yet).
  **Done:** `generate_clothes.py` builds `Shirt` (Spine/Spine1/Shoulder.L/R/UpperArm.L/R),
  `Pants` (Hips/UpperLeg.L/R/LowerLeg.L/R), and `Shoes` (Foot.L/R) as offset shell meshes
  — cylinder+joint-sphere per covered bone, at `generate_body.BONE_RADII` plus outward
  padding — each its own object, parented to the skeleton with `ARMATURE_AUTO` weights
  and its matching material assigned. `generate_hair.py` builds a literal helmet-like
  cap: a bmesh hemisphere (upper half of a UV sphere, open at the bottom) sized just
  outside the head, likewise auto-weighted (rigidly follows `Head` in practice, since
  it's the only nearby bone) with the `Hair` material. Both promote
  `generate_body.py`'s former `_add_cylinder_segment`/`_add_joint_sphere` helpers to
  public `add_cylinder_segment`/`add_joint_sphere` for reuse. Verified: both scripts run
  headless and assert vertex groups/materials are correct; a manual full-avatar render
  (all of Tasks 11.1-11.5 together) shows a recognizable, if crude, clothed low-poly
  figure — hair, short-sleeve shirt, pants, and shoes all visually distinct and
  correctly colored/positioned.

- [x] **Task 11.6** — `tools/avatar_builder/generate_animations.py`  
  Placeholder animations `Stand0` (idle) and `Wave`, keyframed directly on the Task 11.1 skeleton
  (simple bone rotations — no motion capture, no external clip source). Because this script and
  Task 11.1 share the same bone names by construction, there is no retargeting step.
  **Done:** `generate_animations.py` builds `Stand0` (subtle `Hips` bob + `Spine1` rock,
  looping over 90 frames) and `Wave` (right-arm raise via `UpperArm.R` + an elbow-fold
  oscillation via `LowerArm.R`, 60 frames) as Blender Actions on `CNAAvatarSkeleton` via
  `build_animations()`. First attempt keyframed `LowerArm.R`'s local Y axis for the
  oscillation, which turned out to be an invisible twist on a round cylinder (caught by
  rendering it and seeing no silhouette change); switched to local X, which visibly
  folds the elbow. Verified: headless run asserts both actions exist with a nonzero
  frame range; a manual render sequence confirmed the raise and fold are visually real,
  not just numeric.
  **Also completed the bend-artifact visual check deferred since Task 11.2:** posed the
  clothed avatar through `Wave`'s peak fold frames and rendered a close-up of the
  elbow/wrist — confirms a real, visible tear (the forearm/hand separate from the
  sleeve and from each other at the fold) at both fold extremes. This is the expected
  automatic-weights limitation, not a surprise, but is now a *confirmed* fact rather
  than an assumption — a manual weight-painting correction pass at elbows/shoulders is
  needed before this is presentable, and is explicitly still open (not part of Task
  11.6's scope; see `tools/avatar_builder/README.md`).

- [x] **Task 11.7** — `tools/avatar_builder/export_gltf.py` + `tools/avatar_builder/generate_avatar.py`  
  Orchestrates Tasks 11.1–11.6 and exports via `bpy.ops.export_scene.gltf(...)`. Driven headless:
  `blender --background --python generate_avatar.py -- --gender male --out assets/avatar/generated/male_avatar.glb`
  (and again with `--gender female`, same skeleton/rig, adjusted body proportions/scale). Output
  must be deterministic (same inputs → byte-identical or near-identical output) and must reopen
  cleanly in Blender.
  **Done:** `export_gltf.py`'s `export_avatar(output_path, objects)` selects exactly the
  given objects and calls `bpy.ops.export_scene.gltf(..., export_format="GLB",
  use_selection=True, export_animation_mode="ACTIONS", export_animations=True,
  export_morph=True, export_skins=True, export_yup=True)`. `generate_avatar.py`'s
  `build_avatar(gender)` clears the scene, calls every Task 11.1–11.6 `build_*()` in
  order, and for `--gender female` applies a coarse overall `armature_obj.scale = (0.93,
  0.93, 0.93)` — explicitly *not* real proportion differentiation (shoulder/hip
  width, head size), which stays deferred to Task 11.13. CLI args (`--gender`, `--out`)
  are parsed from `sys.argv` after Blender's own `--`.
  **Verified beyond the plan's own bar:** ran both `--gender male` and `--gender female`
  headless (`/tmp/male_avatar.glb`, `/tmp/female_avatar.glb`); both reopen cleanly via
  `bpy.ops.import_scene.gltf` with correct objects/parenting/actions/shape keys; female's
  exported skeleton node scale is exactly `(0.93, 0.93, 0.93)` and reimports with the Head
  bone at the correspondingly scaled world height. **Determinism:** ran male export
  twice — the JSON chunk is byte-identical; the binary buffer differs in 4735/104936
  float32s, every one by exactly 1 ULP (`1.1920929e-07` = `2^-23`) — Blender-internal
  floating-point rounding noise (almost certainly the automatic-weight solver), not a
  real difference. Satisfies the plan's explicit "byte-identical **or near-identical**"
  allowance.
  **Confirmed, not-fixed findings (same spirit as Task 11.6's elbow tear):** the exporter
  warns `Mesh Cylinder is not valid` (the body mesh's underlying data-block, still named
  from its `primitive_cylinder_add` origin — cosmetic naming only, unrelated to the
  warning) and `There are more than 4 joint vertex influences` (24 vertices on
  `CNAAvatarShirt`, confirmed by direct inspection — glTF's 4-joint limit trims/
  renormalizes these, a standard, expected consequence of automatic weights, not a bug).
  32 of `CNAAvatarBody`'s 1086 vertices have **zero** total bone weight (also confirmed
  by direct inspection); Blender's exporter silently covers this by adding a synthetic
  `neutral_bone` joint to the skin to receive them. On reimport, Blender additionally
  creates a cosmetic `Icosphere` bone-shape-widget object to visualize that bone (it has
  no natural head/tail extent) — this widget is **not** in the exported file itself (not
  in `g.meshes`/`g.nodes`), purely an artifact of Blender's own importer UI, irrelevant to
  any other glTF consumer (including CNA's own runtime). None of this blocks the file
  from being valid/usable; documented as known gaps to close alongside the elbow-tear
  weight-painting pass, not before.

- [x] **Task 11.8** — `tools/avatar_builder/validate_gltf.py`  
  Sanity-check each exported GLB using `pygltflib` (already a project dependency from Phase 10):
  non-empty mesh, skin/joints present with the expected bone count/names from Task 11.1, both
  `Stand0`/`Wave` animations present, both `Smile`/`Blink` shape keys present. Fail loudly, don't
  silently accept a hollow/broken export.
  **Done:** `validate_gltf.py` is plain `python3` (no Blender needed) — runs 4 checks via
  `pygltflib` against a `.glb`: non-empty mesh, skin joints covering all 19 canonical
  bone names (extra joints like `neutral_bone` are reported, not failed), `Stand0`/`Wave`
  animations present, `Smile`/`Blink` present in some mesh's `extras["targetNames"]`.
  Required a small prerequisite fix: `generate_skeleton.py` unconditionally imported
  `bpy` at module level, which would `sys.exit()` immediately under plain `python3` —
  moved that import inside `build_skeleton()` (lazy) so `BONES`/`ARMATURE_NAME` (pure
  data, no bpy dependency) can be imported standalone; `validate_gltf.py` uses this
  rather than duplicating the bone-name list.
  **Verified beyond "runs and prints OK":** confirmed `validate` actually fails loudly,
  not just on the happy path — ran it against a nonexistent path, a garbage (non-glTF)
  file, a copy of the real export with the `Wave` animation stripped, and a copy with
  `Blink` removed from `targetNames`; each produced a distinct, correct `FAIL:` message
  and exit code 1. Ran clean against both real `male_avatar.glb`/`female_avatar.glb`
  (19/19 bones, both animations, both shape keys; `neutral_bone` correctly reported as
  informational, not a failure).

- [x] **Task 11.9** — `tools/avatar_builder/README.md`  
  Usage instructions, the canonical skeleton bone list, design rationale (why procedural, why no
  retargeting needed), how to run each script standalone vs. via the top-level driver, and a clear
  statement of what's placeholder-quality vs. intended to be improved later.
  **Done:** most of this content already existed, written incrementally across Tasks
  11.1–11.8; this task was a coherence pass, re-reading the whole file top to bottom as a
  newcomer would. Added a new top-of-file "Usage" section (the missing piece — a quick
  start for the top-level driver + validator, and an explicit standalone-vs-driver
  explanation, since that was previously only inferable by piecing together each
  section's own "Verify:" line). Fixed two now-stale passages found during the reread:
  the body section still said the bend-artifact check "is deferred until [Task 11.6]
  exist[s]" (it's done, and found a real tear — rewritten to point at that finding
  instead of describing it as still-future); the materials section implied
  `Hair`/`Shirt`/`Pants`/`Shoes` had nothing assigning them (Task 11.5 now does, via
  `generate_clothes.py`/`generate_hair.py` — rewritten to say so). Added a Task 11.9 line
  and a Phase-11a-complete note to the Status checklist.

### 11b — CNA Integration (first real, non-synthetic proof)

- [x] **Task 11.10** — Feed `male_avatar.glb`/`female_avatar.glb` through the existing
  `tools/avatar_asset_pipeline/convert_avatar.py` (built in Phase 10, structurally verified only
  against a synthetic fixture until now) to produce real
  `.skinnedmodel.json`/`.skeleton.bin`/`.clip.bin` content. This is the first time that converter
  runs against real generated content — expect and fix real bugs, don't assume it works unchanged.
  **Done — found and fixed two real bugs, exactly as expected:**
  1. `convert_body()`'s part names came from the exported mesh **data-block** name, not
     the object name — `generate_body.py`/`generate_clothes.py` (Tasks 11.2/11.5) only
     renamed the object, leaving Blender's auto-generated `Cylinder`/`Cylinder.024`-style
     names to leak into `avatar.skinnedmodel.json`. Fixed at the source: both scripts now
     also set `obj.data.name`.
  2. `convert_avatar.py`'s CLI assumed the MakeHuman/Mixamo workflow: one body file, a
     separate file per clip, each with exactly one animation (`convert_clip` hardcoded
     `gltf.animations[0]`). CNA's own pipeline bundles body+skeleton+**both** clips in one
     file. Refactored the per-animation conversion logic out into a shared
     `_tracks_from_animation()` helper, added `convert_embedded_clip()` (converts one
     already-loaded animation by name) and a new `--embedded-clips` CLI flag — the
     original `--body`/`--clip` path is unchanged/still works for the documented
     MakeHuman/Mixamo workflow.
  **Verified well beyond "runs without crashing":** for both `male_avatar.glb` and
  `female_avatar.glb`, confirmed `skeleton.bin` has no invalid parent indices (all `-1`
  or `< bone_count`) and no truncation/trailing bytes; every part's vertex/index buffer
  size divides evenly by its declared stride (no corruption); both `.clip.bin` files
  have no truncation and their key counts match; clip durations (`Stand0` 3.75s, `Wave`
  2.5s) were cross-checked against Blender's actual scene fps (24, via `bpy`), which
  **also caught and fixed a stale doc claim**: `generate_animations.py`'s own docstrings
  said "30fps" — an assumption never actually checked at the time (Task 11.6); Blender's
  default scene fps is 24, not 30. Fixed there; `plan_net.md`/`NEXT.md` only ever stated
  frame counts (fps-independent), so nothing to fix in either. Skeleton comes out as 20
  bones (19 real +
  the already-documented synthetic `neutral_bone`), 5 correctly-named parts, both clips
  present with 19 tracks each.

- [x] **Task 11.11** — Wire the converted content through
  `ContentManager::Load<shared_ptr<SkinnedModelEXT>>` and `AvatarRenderer::EnableRealRenderingEXT`/
  `DrawRealEXT` in a real, non-headless windowed demo (not another synthetic-fixture integration
  test) — the actual visual proof that Phase 10's engine work and Phase 11's content now draw a
  real, if simple, animated humanoid on screen.
  **Done — and this is where the real bugs the plan anticipated actually turned up.**
  New `examples/demo_avatar/` (`cna_demo_avatar`, gated like the existing integration test
  on `CNA_ENABLE_NET` + EasyGL/Vulkan): loads `Content/avatar/male/avatar.skinnedmodel.json`
  (real Task 11.10 content, committed into the demo's own `Content/`), calls
  `EnableRealRenderingEXT`/`SetAppearanceEXT`, and calls `DrawRealEXT("Stand0"/"Wave", ...)`
  every frame (Space toggles clips, arrow keys orbit the camera). **Verified on a real
  X11/OpenGL window** (screenshots taken via `import`/`xwininfo`, not just "it compiled"):
  a complete, correctly-proportioned, animated T-pose humanoid renders; toggling to `Wave`
  visibly bends the arm.
  Getting a recognizable figure on screen (not a giant, nonsensical close-up) took three
  rounds of real bug-hunting, none of them findable by re-reading code — each one only
  showed up once real camera matrices / a real multi-bone skeleton / real file-sourced
  quaternions were involved, none of which the Phase 10 synthetic fixture (identity
  view/projection, one hand-built bone, `Quaternion::Identity` constructed directly in
  C++) ever exercised:
  1. `ContentManager.cpp`'s `SkinnedModelTypeReader` resolved every manifest path
     (`skeleton`/`vertices`/`indices`/`texture`/`clip`) against the content root instead
     of the manifest's own directory — content in a subdirectory (`Content/avatar/male/`)
     failed to load at all. Fixed: resolve relative to `fs::path(path).parent_path()`.
  2. `convert_avatar.py`'s bind-pose-local-to-CNA-row-major "fix" from Task 11.10 was
     itself backwards — glTF's column-major storage and CNA's row-major storage for the
     *same* transform are byte-identical (transposing the matrix and swapping major order
     are inverse operations that cancel out), so transposing was the bug, not the missing
     step. Also found: `inverseBindMatrices`/vertex `JOINTS_0` indices needed remapping to
     `build_node_hierarchy`'s topological bone order, same as Task 11.10's part-vertex fix.
     `bind_pose_local` is now derived directly from `inverse_bind_global` via a small
     dependency-free 4x4 matrix inverse (`_invert4x4`) — correct by construction, not by
     independently re-deriving the same bind pose two different ways and hoping they agree.
     Diagnosed via a forced-identity-bones render (isolating camera/mesh/shader as already
     correct) then dumping `ComputeBoneTransformsEXT`'s own output at exact rest pose,
     which must reduce to identity for every bone by definition.
  3. **The real show-stopper, found last:** even after both fixes above, real animated
     bones still came out wrong. Root cause was in `ContentManager.cpp` itself, not the
     converter: `key.Rotation = Quaternion(clipReader.Read<float>(), clipReader.Read<float>(),
     clipReader.Read<float>(), clipReader.Read<float>())` relies on left-to-right
     evaluation of the four `Read<float>()` calls — which C++ does **not** guarantee for a
     single function call's arguments. The compiler evaluated them in a different order,
     silently scrambling which bytes landed in which quaternion component (confirmed via a
     raw hex dump of the clip file: true bytes were `(x,y,z,w)=(0,0,0,1)` identity; the
     constructed `Quaternion` came out `(1,0,0,0)` — exactly the reversed order). Fixed by
     reading each float into its own named local first, as strictly sequential statements,
     before constructing `Vector3`/`Quaternion`. Same pattern existed for `Translation`
     and `Scale` too (only invisible for `Translation` because Spine's `tx`/`tz` were both
     coincidentally `0.0`, so the outer-pair swap a reversal would cause was undetectable
     there); all three fixed together.
  All three verified by an exact mathematical check, not just "looks right": at rest pose,
  `ComputeBoneTransformsEXT`'s output for every one of the 20 bones is now bit-for-bit
  identity (`worldTransforms[i] * InverseBindPoseGlobal[i] == I`, as it must be by
  definition), confirmed both via a standalone C++ diagnostic and by Python-side
  replication of the exact same formula against the actual written binary files.
  Full regression check: all 3212 non-skipped `CnaTests` still pass, and the existing
  Phase 10 synthetic integration test (`cna_test_avatar_real_render`) still passes
  unmodified — none of these fixes touched any faithful-XNA or previously-tested path.
  **At the time this task closed, only the male body was wired into the demo — see
  Task 11.12 immediately below, done in the same session.** The confirmed elbow/sleeve
  tear and zero-weight vertices (Task 11.6/11.7, `tools/avatar_builder/README.md`) are
  unrelated content-quality gaps, untouched here.

- [x] **Task 11.12** — Map `AvatarBodyType::Male`/`Female` to the two generated bodies at whatever
  call-site convention makes sense (document the chosen approach in
  `docs/avatar-real-rendering-ext.md`, since Phase 8's faithful `AvatarDescription` doesn't carry
  real body-type data that could drive this automatically).
  **Done:** new NOXNA `AvatarBodyTypeToContentNameEXT(AvatarBodyType)`
  (`include`/`src/.../GamerServices/AvatarBodyTypeNamesEXT.hpp`/`.cpp`, mirroring the
  existing `AvatarAnimationPresetToClipNameEXT` pattern) maps `Male`/`Female` to
  `"avatar/male/avatar"`/`"avatar/female/avatar"` — the single, explicit call-site
  convention; confirmed (by reading `AvatarDescription.cpp`) that
  `getBodyTypeProperty()` truly never carries usable data (permanently lazy-inits to
  `Female`, never parsed from `description_`), so deriving the mapping from it was never
  an option, matching the task's own premise. 4 new unit tests
  (`tests/.../AvatarBodyTypeNamesEXTTests.cpp`): both values map to the expected,
  distinct, non-empty names; unrecognized values throw `ArgumentException`.
  `examples/demo_avatar/`'s `AvatarDemo` now takes an `AvatarBodyType` constructor
  argument (default `Male`); `Main.cpp` parses a new `--gender male|female` CLI flag and
  passes it through. Generated and committed real female content into
  `examples/demo_avatar/Content/avatar/female/` (Task 11.10's converter, unchanged).
  **Verified beyond "the function returns the right string":** ran
  `cna_demo_avatar --gender female` on a real X11 window and screenshotted it — renders
  the distinct, correctly-scaled female body (0.93× overall, the coarse female-scale
  placeholder from Task 11.7), proving the mapping actually drives real content
  selection end to end, not just that the string differs. Full regression check: all
  3216 non-skipped `CnaTests` pass (3212 + 4 new).

### 11c — Iteration (procedural variety) — deferred, lower priority than 11a/11b

- [x] **Task 11.13** — Parametric body variation (height, shoulder width, head size) as script
  parameters, conceptually echoing `AvatarDescription`'s customization intent without attempting
  to reconstruct its real, undocumented byte format.
  **Done:** three independent parameters — `height_scale` (uniform bone-position scale
  in `generate_skeleton.build_bones()`, plus matching radius scale in
  `generate_body.py`/`generate_clothes.py`), `shoulder_width_scale` (additional X-only
  scale on the arm chain — `Shoulder`/`UpperArm`/`LowerArm`/`Hand` `.L`/`.R` — independent
  of height), and `head_scale` (Head bone's own flesh/hair-cap radius only, independent
  of height, e.g. for a "chibi" big-head look) — threaded as optional keyword args
  through `build_bones()`/`build_skeleton()`/`build_body()`/`build_clothes()`/
  `build_hair()`/`build_morphs()`, all defaulting to unscaled/`1.0` so every other
  script's own standalone run is unaffected.
  `generate_avatar.py` gained a `GENDER_PRESETS` dict (female:
  `height_scale=0.93, shoulder_width_scale=0.85, head_scale=0.97`, a coarse placeholder
  silhouette, not measured proportions) replacing Task 11.7's old coarse whole-armature
  `armature_obj.scale = (0.93,)*3` post-hack, plus new `--height-scale`/
  `--shoulder-width-scale`/`--head-scale` CLI flags overriding any of the preset's three
  values individually.
  **Verified beyond "the script accepts new arguments":** rendered four combinations
  (default male, default female preset, an extreme "big head/narrow shoulders"
  `height=1.0/shoulder=0.6/head=1.6`, and a "tall and wide" `height=1.2/shoulder=1.3/
  head=1.0`) and visually confirmed each parameter changes exactly the intended part of
  the body, independently of the others. Also re-ran the full pipeline on a
  custom-parameter export: `validate_gltf.py` still passes, `convert_avatar.py
  --embedded-clips` still converts cleanly, and — reusing Task 11.11's own exact-math
  check — rest-pose bone transforms are still bit-for-bit identity (`~1e-8` floating
  point noise) at non-unit scale factors, confirming the Task 11.10/11.11 matrix-
  convention fixes generalize correctly beyond scale=1.0. Every touched script's own
  standalone `blender --background --python generate_<stage>.py` run was re-verified to
  produce byte-for-byte identical output to before this change (same vertex/displacement
  counts), confirming full backward compatibility.
- [x] **Task 11.14** — Additional hair styles / clothing variants as separate attachable GLB
  pieces rather than baked into the base body.
  **Done:** two hair styles (`generate_hair.HAIRSTYLES`: `Cap`, the original Task 11.5
  helmet shape, and a new `Ponytail` — the same cap plus a tapered `bmesh` cone "tail"
  drooping down the back of the head) and two style variants for two of the three
  garment slots (`generate_clothes.GARMENT_STYLES`: `Shirt` gets `TShirt`/`LongSleeve`,
  `Pants` gets `Pants`/`Shorts`; `Shoes` keeps its single style). `build_hair()` gained a
  `style=` parameter (default `"Cap"`); `build_clothes()` gained a `styles=` parameter
  (default `None` → `DEFAULT_STYLES`, reproducing the exact pre-Task-11.14 bone lists) —
  the built mesh object is always named after its *slot* (`CNAAvatarShirt`), never its
  style, since a slot is a fixed content-pipeline part name. `generate_avatar.py` gained
  `--hair-style`/`--shirt-style`/`--pants-style` CLI flags, all defaulting to the prior
  behavior.
  New `generate_wardrobe.py` exports exactly ONE hair style or clothing variant as its
  own standalone `.glb` (armature + that one piece only) — the "separate attachable GLB
  piece" the task asks for. `tools/avatar_asset_pipeline/convert_avatar.py` needed **no
  changes**: it already converts any GLB with one skin and N meshes into N parts of one
  `.skinnedmodel.json`, with no assumption that a "body" mesh is present — confirmed by
  converting a standalone `hair_ponytail.glb` and getting a clean `avatar.skinnedmodel.json`
  (19 bones, 1 part). No C++ engine changes were made or needed: `AvatarRenderer`/
  `SkinnedModelEXT` currently load and draw exactly one model at a time, so actually
  attaching a separately-loaded piece onto a running avatar at draw time is real, future
  engine work, explicitly out of scope here (this task proves the *content* is modular
  and already flows through the unmodified pipeline, not runtime attachment).
  **Verified well beyond "the script accepts a style argument":** rendered `Ponytail`
  from the side (a clearly distinct drooping tail vs. `Cap`'s plain dome) and rendered
  `LongSleeve`/`Shorts` on a full-body avatar (sleeve visibly reaches the wrist; pant leg
  visibly stops above the knee, bare lower leg exposed). Reopened three example
  wardrobe-piece exports (`hair`/`Ponytail`, `shirt`/`LongSleeve`, `pants`/`Shorts`) in a
  fresh Blender session each: exactly one real mesh object parented to one 19-bone
  `CNAAvatarSkeleton` armature (plus the same cosmetic `neutral_bone`/`Icosphere` widget
  already documented under Task 11.7, when that particular piece has zero-weight
  vertices of its own). Ran the real, unmodified `convert_avatar.py` against one of these
  piece exports and got a clean conversion. Confirmed every touched script's own
  standalone run (`generate_hair.py`, `generate_clothes.py`, `generate_avatar.py` with no
  style flags) still produces the exact pre-Task-11.14 vertex counts (25 hair, 348/290/116
  Shirt/Pants/Shoes, 6-object combined export) — full backward compatibility. Found and
  documented one real nuance, not glossed over: two independently-converted pieces for
  the same avatar can end up with different total bone counts in their own
  `skeleton.bin` (each exporter's synthetic `neutral_bone` joint is added only if *that*
  mesh has zero-weight vertices), though both always agree on the shared 19-bone
  canonical prefix — a detail future runtime-attachment work will need to account for.
- [x] **Task 11.15** — Additional animation presets beyond `Stand0`/`Wave`, working toward covering
  more of the 31 `AvatarAnimationPreset` values with self-authored placeholder motion.
  **Done:** three new Blender Actions on `CNAAvatarSkeleton`, matching `AvatarAnimationPreset`
  names exactly (already mapped by the existing, unmodified
  `AvatarAnimationPresetToClipNameEXT` — no C++ changes needed): `Stand1` (a second idle,
  deliberately shaped differently from `Stand0` — `Hips` X-sway + `Spine1` Y-axis twist,
  vs. `Stand0`'s Z-bob + X-rock), `Clap` (both arms raise to chest height and both
  forearms oscillate their fold four times in sync — a crude, symmetric, non-IK
  approximation, not literal hands-meet-at-center), and `Celebrate` (both arms raise to
  Wave's own already-verified 80° magnitude and hold, with a double `Hips` bounce).
  Total animation coverage: 5/31 `AvatarAnimationPreset` values — still a small fraction,
  explicitly not a claim of full coverage.
  New `_raise_upper_arm`/`_fold_lower_arm` helpers in `generate_animations.py` encapsulate
  the (non-obvious, empirically-derived) sign conventions for mirroring an arm gesture
  from `.R` onto `.L`.
  **Found a real, two-layered empirical gotcha, not assumed away:** mirroring `Wave`'s
  `UpperArm.R`/`LowerArm.R` convention onto `.L` does NOT follow one consistent rule, and
  testing each joint in isolation gives a wrong answer for one of the two. `UpperArm`'s
  raise axis (local Z) needs an opposite-signed angle between `.L`/`.R` for the same
  physical motion, confirmed consistently whether tested alone or combined with a
  forearm fold. `LowerArm`'s fold axis (local X) tests as opposite-signed too **if posed
  alone** (upper arm at rest) — but once the upper arm is *actually raised* (the real
  context this rig is posed in), both sides need the *same* sign instead, because a
  child bone's local rotation composes with its parent's current (already-rotated) world
  transform, not its rest transform. A first `Celebrate` attempt also used an untested,
  bigger raise angle (150°) that looked plausible from one camera angle but was wrong
  from a clearer one (elevated, angled down) — fixed by reusing Wave's own 80°.
  **Verified well beyond "the actions exist with a nonzero frame range":** rendered every
  new animation's key pose on the clothed avatar from multiple camera angles (a single
  angle was what produced the two wrong intermediate attempts above) and numerically
  dumped pose-bone values at every keyframe to confirm exact, correctly-mirrored angles.
  Ran the full pipeline: `generate_avatar.py` (both genders) → `validate_gltf.py`
  (extended `REQUIRED_ANIMATIONS` to all 5) → `convert_avatar.py --embedded-clips`
  (unmodified, all 5 clips convert cleanly with correct track/duration counts: `Stand1`
  4.17s, `Clap` 2.00s, `Celebrate` 2.50s at 24fps). Confirmed `Clap`'s peak fold shows the
  same already-known elbow/sleeve tear as `Wave`'s (Task 11.6) — consistent, not a new or
  worse regression. Confirmed `generate_animations.py`'s own standalone run still
  produces byte-identical `Stand0`/`Wave` frame ranges/values as before this task — full
  backward compatibility. **This completes Phase 11c (Tasks 11.13-11.15) in full.**

### 11d — Future, optional, not started

- [ ] **Task 11.16** — Revisit MakeHuman or CharMorph/Blender as a higher-quality body *source*
  (better anatomy/topology than fully procedural generation can practically achieve) only if the
  user wants to invest in resolving the automation/permission questions documented in `NEXT.md`
  directly — not assumed, not scheduled, purely optional future work.

### 11e — Rendering Fidelity & Coverage Hardening

Opened after a hands-on deep-dive testing session drove `examples/demo_avatar` interactively
(not just headless pixel-readback) and found the avatar rendered as a uniform skin-tone mannequin
with no visible hair/clothing color at all, plus visibly worse forearm deformation on the two
newest two-armed animations (`Stand1`/`Celebrate`) than on `Stand0`/`Wave`. Scope: make CNA's
real-rendering Avatar extension (Phase 10) and its content pipeline (Phase 11) actually look and
perform like a complete avatar system, not just "renders without crashing." This is explicitly
open-ended, ambitious future work — not all of it needs to land in one sitting.

- [x] **Task 11.17** — Fix `AvatarRenderer::DrawRealEXT`'s per-part tint routing. Root cause:
  `const bool isHair = part.Name == "hair"` never matched real part names
  (`CNAAvatarBody`/`Hair`/`Shirt`/`Pants`/`Shoes`), so every part rendered in skin color,
  including hair. Fixed via a new private `AvatarRenderer::PartTintEXT(name)` helper doing a
  substring match, and extended `AvatarAppearanceEXT` with `ShirtColor`/`PantsColor`/
  `ShoesColor` (defaults mirror `generate_materials.py`'s placeholder palette) so clothing has
  its own tint instead of falling back to skin color by default. Added 6 new
  `AvatarAppearanceEXTTest` cases (3 default-value, 3 round-trip). Verified visually via
  `examples/demo_avatar`: before the fix, a uniform tan mannequin; after, distinct dark-brown
  hair / blue shirt / navy pants / near-black shoes.
  - Files: `include/.../GamerServices/AvatarAppearanceEXT.hpp`,
    `include/.../GamerServices/AvatarRenderer.hpp`,
    `src/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.cpp`,
    `tests/.../GamerServices/AvatarAppearanceEXTTests.cpp`.
  - Verify: `cmake --build cmake-build-debug --target CnaTests && cmake-build-debug/CnaTests --gtest_filter="*Avatar*"`.

- [x] **Task 11.18** — Extend `examples/demo_avatar` to exercise all 5 baked animation clips
  (previously only `Stand0`/`Wave` were wired up, even though `Stand1`/`Clap`/`Celebrate`
  existed in the pipeline since Task 11.15). Regenerated the committed
  `examples/demo_avatar/Content/avatar/{male,female}/` from `generate_avatar.py` +
  `convert_avatar.py --embedded-clips` so all 5 clips are actually present in the shipped
  demo content (female content additionally rebaked with the `Ponytail`/`LongSleeve`/`Shorts`
  wardrobe variant, to visually prove Task 11.14's baked-in wardrobe styling too). `Space` now
  cycles through all 5 clip names instead of toggling 2; the window title shows the active clip
  and control hints. This was also the first time `Stand1`/`Clap`/`Celebrate` were rendered
  through the real GPU-skinning path rather than only validated via Blender-side pose-bone
  dumps — confirmed the already-documented elbow/sleeve tear is visibly worse on these two-arm
  poses than on `Stand0`/`Wave`.
  - Files: `examples/demo_avatar/src/AvatarDemo.hpp`, `examples/demo_avatar/src/AvatarDemo.cpp`,
    `examples/demo_avatar/Content/avatar/{male,female}/**`.
  - Verify: `cmake --build cmake-build-debug --target cna_demo_avatar && SDL_VIDEODRIVER=x11 DISPLAY=:0 cmake-build-debug/cna_demo_avatar --gender male` — Space cycles Stand0→Stand1→Wave→Clap→Celebrate→Stand0.

- [x] **Task 11.19** — Real per-part texture support. `convert_avatar.py` now writes a
  small neutral-white placeholder PNG per part (via Pillow) and references it in the
  manifest's new `"texture"` field, making `ContentManager`'s already-existing per-part
  texture-loading path (`ContentManager.cpp:704-736`) real end-to-end. Deliberately
  neutral, not painted per-material color: `AvatarAppearanceEXT` (Task 11.17) remains
  the sole color-customization authority (texture × tint == tint, no double-application
  of color). Painted surface detail is future work (Task 11.25). Verified: converted
  content includes a real `.png` per part; `examples/demo_avatar` renders unchanged from
  the Task 11.17 tint fix (expected, for a neutral texture); `CnaTests --gtest_filter=
  "*Avatar*"` 92/92 passing.

- [x] **Task 11.20** — Weight-painting pass on the confirmed elbow/sleeve tear and zero-weight/
  over-4-influence vertices. Added `generate_body.fix_automatic_weights()`, called after
  every `parent_set(type="ARMATURE_AUTO")` in `generate_body.py`/`generate_clothes.py`/
  `generate_hair.py`: (1) assigns zero-weight vertices to their nearest bone segment,
  (2) caps every vertex to ≤4 influences via `vertex_group_limit_total`, (3) smoothly
  blends parent/child weights across each bend joint (Shoulder/UpperArm, UpperArm/
  LowerArm, UpperLeg/LowerLeg) instead of automatic weighting's near-binary assignment.
  Verified (1)/(2) by direct per-vertex inspection (both scripts' own `__main__` now
  assert zero remaining zero-weight/over-4-influence vertices); `validate_gltf.py`
  confirms the synthetic `neutral_bone` joint no longer gets added. Verified (3) with a
  real Blender render at Wave's exact peak-fold pose (UpperArm.R -80°, LowerArm.R 70°):
  the elbow/sleeve seam that previously separated now stays visually continuous — this
  reduces the visible gap at the bend angles this rig's animations actually use, not a
  claim of welding the still-topologically-separate cylinder segments into one surface.
  - **Did NOT fix, and was a separate issue from what this task targeted:** verifying via
    `examples/demo_avatar`'s real GPU-skinned engine (not just Blender) surfaced a much
    more severe, pre-existing bug — during actual Wave clip *playback* (not a static
    pose), the forearm rendered as a dramatically elongated, partially-detached shape.
    Confirmed present in both this fix's content and the prior (Task 11.19) committed
    content, so this fix neither caused nor cured it — resolved separately as Task 11.20b
    (a real matrix-multiplication-order bug in `ComputeBoneTransformsEXT`, unrelated to
    vertex weights).

- [x] **Task 11.20b** — **RESOLVED.** Root cause: `SkinnedModelEXT::ComputeBoneTransformsEXT`
  computed the final skinning matrix as `worldTransforms[i] * InverseBindPoseGlobal[i]` —
  backwards for CNA's row-vector convention (`v' = v * M`, first-applied transform
  leftmost). Correct order removes the bind pose first, *then* applies the current pose:
  `InverseBindPoseGlobal[i] * worldTransforms[i]`. Both orders reduce to the identity at
  rest pose (`A * Inverse(A) == Inverse(A) * A` for any invertible `A`), which is exactly
  why every prior check — Task 11.11's rest-pose sanity tests, the single-bone
  translation-only integration test, and every existing unit test (all pure translations,
  which commute with anything) — never caught it. Only a bone with both a nontrivial
  bind-pose offset from its parent *and* a real rotation exposes the difference — exactly
  `UpperArm.R`/`LowerArm.R`'s situation in `Wave`.
  - Ruled out first (before finding the real cause), by direct inspection: clip keyframe
    data, `skeleton.bin`'s topological bone order, bind-pose matrix values, vertex
    attribute/GL-integer-pointer setup for bone indices, and the bone-matrix-to-GPU
    upload/column-major-conversion path — all confirmed correct. Reimporting the exported
    `.glb` into a fresh Blender session and replaying the identical `Wave` action through
    Blender's own armature rendered cleanly at every tested frame — this correctly
    isolated the bug to CNA's own matrix math, not the content or Blender-side data.
  - New unit test `SkinnedModelEXTTest.RotatingBoneKeepsItsOwnBindPivotFixed`: a bone with
    bind offset `(1,0,0)` that rotates 90° in place must keep its own bind-world pivot
    fixed at `(1,0,0)`. Confirmed fails under the old order (pivot moves to the bare
    rotation's result, `~(0,1,0)`, with no bind correction at all) and passes under the fix.
  - Real-engine visual confirmation: `Wave` and `Celebrate`, which previously rendered the
    forearm as a dramatically elongated, partially-detached shape, now show a normal,
    coherent raised arm — screenshotted before/after, front and rotated side views.
  - Files: `src/Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.cpp`,
    `tests/Microsoft/Xna/Framework/Graphics/SkinnedModelEXTTests.cpp`.
  - Verify: `cmake-build-debug/CnaTests --gtest_filter="*SkinnedModelEXT*"` (11/11); full
    suite 3225/3227 (2 expected skips); all 3 GPU integration tests still `[PASS]`.

- [x] **Task 11.21** — Runtime wardrobe attach API. Added
  `SkinnedModelEXT::AttachPartEXT(SkinnedModelEXT&&)`, moving every part (and its owning
  buffers) from an independently-loaded model into this one. No per-vertex bone-index
  remap needed: confirmed (by inspecting `convert_avatar.py`'s `build_node_hierarchy`
  against both a full-body export and a standalone wardrobe export) that the topological
  bone sort is deterministic given the same canonical skeleton, so a wardrobe piece's
  joint indices already agree with the host body's bone index scheme. Throws
  `System::ArgumentException` on a `BoneCount` mismatch. 2 unit tests + a new real-GPU
  integration test (`examples/avatar_attach_part_integration_test.cpp`,
  `cna_test_avatar_attach_part`) proving two independently-built single-bone quad models
  render correctly after one is attached onto the other — `[PASS]` confirmed.

- [x] **Task 11.22** — Wired `examples/demo_avatar` to Task 11.21's attach API: a new
  `--wardrobe-hair <Style>` CLI flag loads a standalone-converted hair piece
  (`Content/wardrobe/hair_<Style>/`) and swaps it in for the baked-in hair at load time.
  Verified visually: `--wardrobe-hair Ponytail` renders the male avatar with the
  ponytail's distinct silhouette in place of the default round cap.

- [x] **Task 11.23** — Expand animation coverage — **done in full, 31/31
  `AvatarAnimationPreset` values now have real baked clips**:
  - **11.23a** — `Stand2`-`Stand7` (6 more generic idles), built from
    Hips/Spine/Spine1/Neck/Head only (no L/R-mirrored arm bones).
  - **11.23b** — the 10 `Female*` presets, same safe bone set; surfaced a new seam-gap
    finding (`FemaleIdleFixShoe`'s 35° Spine bend), fixed by adding `Hips`-`Spine` and
    `Spine`-`Spine1` to `generate_body.BEND_JOINTS` (Task 11.20's fix generalizes beyond
    arms/legs).
  - **11.23c** — the 10 `Male*` presets, same safe bone set, each deliberately using a
    different bone pairing/timing than its female counterpart.
  - `generate_animations.build_animations()` now takes a `gender` parameter (generic set
    always builds; `Female*`/`Male*` only build for their own gender);
    `generate_avatar.py` passes its own gender through. `AvatarDemo`'s `clipNames_` is
    populated per-gender in the constructor. Verified via Blender renders at each new
    clip's peak frame plus real-engine runs (no crashes).

- [x] **Task 11.24** — Pixel-readback regression test guarding Task 11.17's fix class of
  bug. New `examples/avatar_tint_routing_integration_test.cpp`
  (`cna_test_avatar_tint_routing`): two single-bone quads named `"CNAAvatarHair"`/
  `"CNAAvatarShirt"` with all-white textures (isolating tint from texture) and a
  non-default `AvatarAppearanceEXT`, asserting each renders in its own appearance color.
  Confirmed the test actually catches the regression, not just passes vacuously:
  temporarily reverted `PartTintEXT` to the pre-Task-11.17 exact-`"hair"`-match logic and
  reran — `[FAIL]`, both parts collapsed to skin color; restored the real fix and reran
  — `[PASS]`.

- [ ] **Task 11.25** — (Speculative, lowest priority, scope not yet designed) A richer
  appearance/customization model: per-part texture atlases (builds on Task 11.19), a wider
  skin-tone/hair-color preset palette, and revisiting whether `AvatarAppearanceEXT` should grow
  beyond simple named-slot tints. Do not start without a fresh scoping pass — this is a
  placeholder for "there's clearly more here" more than a real, actionable task yet.

---

## Phase 12 — cna-samples-Driven Networking Fixes

Opened after the sibling `cna-samples` repo (`../cna-samples`) ported **ClientServerSample**
(#091) — the first real, non-synthetic caller of `NetworkSession`/`GamerServices` outside CNA's own
unit tests — and hit three real, live-reproduced bugs that no existing `NetworkSessionTests.cpp`
case exercises (because those tests never construct a `GamerServicesComponent` and never check
multi-gamer `Id`/`IsHost` state). Fully documented, root-caused, and each independently confirmed
live in `../cna-samples/DEFERRED.md` items #19–21 and
`../cna-samples/samples/ClientServerSample/missing.md`; ClientServerSample currently ports around
all three at the sample level (documented deviations, not silent hacks). Fixing these in `cna`
removes the need for that workaround and unblocks NetworkPrediction (#100), PeerToPeer (#103), and
NetRumble (#062) — all four `cna-samples` networking samples call `NetworkSession::Create`/`Find`/
`Join` the same way and construct a `GamerServicesComponent` in their original C# constructors.

- [x] **Task 12.1** — Fix `GamerServicesDispatcher::Update()` no-op hanging
  `NetworkSession::Create`/`Find`/`Join` forever whenever a `GamerServicesComponent` exists
  (`DEFERRED.md` item #19). Root cause, confirmed live: `NetworkSession::Create()`'s synchronous
  polling loop (`src/Microsoft/Xna/Framework/Net/NetworkSession.cpp:406-461`, mirrored in the
  `Find`/`Join`/`JoinInvited` overloads) is
  ```cpp
  while (!result->getIsCompletedProperty())
  {
      if (!GamerServices::GamerServicesDispatcher::UpdateAsync())
          activeAction_->setIsCompletedProperty(true);
  }
  ```
  `GamerServicesDispatcher::Update()` (`src/Microsoft/Xna/Framework/GamerServices/
  GamerServicesDispatcher.cpp`) is a completely empty function body, and none of
  `BeginCreate`/`BeginFind`/`BeginJoin`/`BeginJoinInvited` (all in `NetworkSession.cpp`) ever mark
  `activeAction_` completed themselves. With no `GamerServicesComponent`, `UpdateAsync()` returns
  `false` on the very first iteration, forcing completion — masking the bug for every existing
  unit test. With one present (`isInitialized_ == true`, matching every real sample's own
  constructor), `UpdateAsync()` unconditionally returns `true` forever and nothing ever completes
  the action — infinite busy-loop, 99% CPU.
  **Confirmed this is a genuine upstream FNA/XNA bug, not just a CNA porting defect:** the real FNA
  reference source (`/rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/Net/NetworkSession.cs`,
  `NetworkSessionAction` constructor) sets `IsCompleted = false` explicitly, and
  `GamerServicesDispatcher.cs`'s `Update()` is likewise a permanently empty method body in FNA
  itself — real XNA/FNA would hang identically given a real `GamerServicesComponent`, which every
  actual XNA 4.0 networking sample constructs. Documented as an intentional, in-source-commented
  deviation from FNA per `CHECKLIST.md`'s "every intentional deviation from FNA logic has a `//`
  comment" rule, rather than silently preserving a defect that makes the real API unusable.
  **Fix applied:** `NetworkSession::NetworkSessionAction`'s constructor
  (`src/Microsoft/Xna/Framework/Net/NetworkSession.cpp`) now initializes `isCompleted_(true)`
  instead of relying on the in-class default `{false}` — every `Begin*`-constructed action is
  complete the instant it's returned, matching the fact that all of CNA's own "real" work
  (`ENetBackend::StartHosting`/`ENetDiscoveryService::FindSessions`, etc.) already runs
  synchronously inside the constructor/`EndFind`, not across multiple polls. This is a genuine
  one-line, minimal fix: the constructor's own pre-existing doc comment ("Constructs a
  NetworkSessionAction already positioned to complete") already described this exact intended
  behavior; the implementation had simply never matched it. Every synchronous wrapper's polling
  loop is left untouched (now unreachable dead code, but byte-for-byte faithful to FNA's own
  line-by-line structure) rather than touched for cosmetic simplification, per the "a bug fix
  doesn't need surrounding cleanup" principle.
  **Regression test:** since `GamerServicesDispatcher::Initialize()` sets a process-lifetime static
  with no reset (would contaminate every other test in the `CnaTests` binary — the same hazard
  already documented at the top of `GamerServicesServiceTests.cpp`), the reproduction runs as a
  genuinely separate OS process: new `tools/net/gamerservices_dispatcher_harness.cpp` (standalone,
  non-GTest executable, mirroring Task 6.1's `cna_net_two_process_harness` isolation pattern) calls
  `GamerServicesDispatcher::Initialize()` then `NetworkSession::Create(NetworkSessionType::Local, ...)`
  and exits 0 if it returns; spawned and watchdog-timed (10s) by new
  `tests/CNA/Internal/Net/GamerServicesDispatcherHangRegressionTest.cpp`. **Verified the test
  actually catches the regression, not just passes vacuously:** temporarily reverted the
  `isCompleted_(true)` fix and reran — the standalone harness hung until `timeout(1)` killed it
  (confirmed exit code 124), and the gtest wrapper correctly failed via its own watchdog (10017ms,
  not a silent pass) instead of hanging `CnaTests` itself; restored the fix and reran — harness
  exits in ~50ms, gtest wrapper passes. Full regression check: all 3226 non-skipped `CnaTests`
  pass (3228 total, 2 expected accelerometer/gyroscope skips — +1 test vs. the prior 3227 total),
  including every existing `NetworkSessionTests.cpp` case unmodified.
  Files: `src/Microsoft/Xna/Framework/Net/NetworkSession.cpp`,
  `tools/net/gamerservices_dispatcher_harness.cpp` (new),
  `tests/CNA/Internal/Net/GamerServicesDispatcherHangRegressionTest.cpp` (new), `CMakeLists.txt`.

- [ ] **Task 12.2** — Give `NetworkGamer` real per-instance `IsHost`/`Id` state instead of
  hardcoded stub constants (`DEFERRED.md` item #20). Root cause, confirmed by direct inspection of
  `src/Microsoft/Xna/Framework/Net/NetworkGamer.cpp`:
  ```cpp
  bool NetworkGamer::getIsHostProperty() const              { return true; }
  SharpRuntime::bytecs NetworkGamer::getIdProperty() const  { return 0; }
  ```
  every gamer (host and every client) reports `IsHost == true` and `Id == 0`. Consequences:
  `NetworkSession::getIsHostProperty()` (`NetworkSession.cpp:163-168`, "true if any local gamer's
  `IsHost` is true") is therefore *also* always true on every machine; and
  `NetworkSession::FindGamerById()` (`NetworkSession.cpp:293`) does a linear
  `getIdProperty() == id` scan that always matches the *first* gamer in `AllGamers`, misrouting any
  protocol that writes a gamer's `Id` into a packet and looks it back up on the receiving end
  (exactly what ClientServerSample's/NetworkPrediction's/PeerToPeer's per-object state sync does) —
  breaks as soon as more than one gamer is in a session.
  Fix approach: add a real `bool isHost_` member to `NetworkGamer` (or derive it from whichever
  session-level flag `NetworkSession`'s constructor already sets correctly at
  `NetworkSession.cpp:104-111` — `host_ = localGamers_[0]` after `Create()`, vs. whatever
  `Join()`/`JoinInvited()`/`ENetBackend`'s remote-gamer construction path should set for a
  non-host), set once at construction for every `NetworkGamer`/`LocalNetworkGamer` (construction
  sites: `NetworkSession.cpp:86,95,288` for locals, `ENetBackend.cpp:140,188,203` for remotes,
  `LocalNetworkGamer::CreateInternal`/`NetworkGamer::CreateInternal`), and return it from
  `getIsHostProperty()` instead of the hardcoded `true`. Add a real per-session-unique `bytecs id_`
  assigned at the same construction sites (e.g. a `NetworkSession`-owned monotonic counter, or
  index-in-`AllGamers`, whichever preserves stable identity across the session's lifetime — decide
  and document the exact scheme, since packets on the wire will depend on it not changing gamer to
  gamer), returned from `getIdProperty()` instead of the hardcoded `0`.
  Add unit tests: multiple gamers in one session get distinct `Id`s; `FindGamerById` returns the
  correct gamer for each; exactly one gamer (session host) reports `IsHost == true` on the host
  machine and `IsHost == false` for every remote gamer as observed from a client machine.
  File: `include/`+`src/Microsoft/Xna/Framework/Net/NetworkGamer.hpp`/`.cpp`,
  `src/Microsoft/Xna/Framework/Net/NetworkSession.cpp`,
  `src/CNA/Internal/Net/ENetBackend.cpp`.

- [ ] **Task 12.3** — Raise the initial `GamerJoined` event(s) synchronously during
  `Create()`/`Join()` instead of queuing them for the next `Update()` (`DEFERRED.md` item #21).
  Root cause, confirmed live: `NetworkSession`'s constructor
  (`src/Microsoft/Xna/Framework/Net/NetworkSession.cpp:113-119`) queues a `GamerJoin`
  `NetworkEvent` per initial local gamer into `networkEvents_` instead of raising `GamerJoined`
  directly:
  ```cpp
  for (NetworkGamer* gamer : allGamers_)
  {
      NetworkEvent evt;
      evt.Type = NetworkEventType::GamerJoin;
      evt.Gamer = gamer;
      SendNetworkEvent(std::move(evt));
  }
  ```
  that queue is only drained by `NetworkSession::Update()` (`NetworkSession.cpp:217-` onward,
  dispatching `GamerJoined.Raise(...)` at line 252) — i.e. not until the *next* frame's
  `networkSession.Update()` call, one full frame after `Create()`/`Join()` returns. Real XNA raises
  `GamerJoined` synchronously as part of `Create()`/`Join()` itself, so code that expects a
  `GamerJoined` handler's side effect (e.g. `e.Gamer.Tag = new Tank(...)`) to have already run by
  the time `Create()` returns — matching every real sample's own structure — instead finds it
  unset on the first frame; observed live as an uncaught `std::bad_any_cast` when reading an empty
  `Tag`.
  Fix approach: either (a) have the constructor call `GamerJoined.Raise(...)` directly for each
  initial local gamer instead of only enqueuing a `NetworkEvent` (matches real XNA's synchronous
  behavior most directly), or (b) have `Create()`/`Find()`+`Join()`/`JoinInvited()`'s synchronous
  wrappers (the same ones touched in Task 12.1) drain `networkEvents_` once via a call to
  `Update()` before returning the constructed session. Either removes the need for the
  `networkSession_->Update();`-right-after-`HookSessionEvents()` workaround every calling sample
  currently needs. Add a unit test asserting `GamerJoined` has already fired for every initial
  local gamer by the time `Create()`/`Join()` returns (no `Update()` call needed first) — the
  current suite's blind spot, since `NetworkSessionTest.GamerJoinedRaisedOnUpdateAfterConstruction`
  (`tests/Microsoft/Xna/Framework/Net/NetworkSessionTests.cpp`) explicitly tests the *current*,
  wrong, deferred-until-`Update()` behavior and will need updating to match the fix.
  File: `src/Microsoft/Xna/Framework/Net/NetworkSession.cpp`,
  `tests/Microsoft/Xna/Framework/Net/NetworkSessionTests.cpp`.

- [ ] **Task 12.4** — Once Tasks 12.1-12.3 land, remove the three sample-level workarounds in
  `../cna-samples/samples/ClientServerSample/` (omitted `GamerServicesComponent`, local `isHost_`
  tracking instead of `NetworkSession.IsHost`, extra `networkSession_->Update()` call after
  `HookSessionEvents()`) and re-verify it still renders/functions correctly with the real fixes in
  place instead of the workarounds — confirms the fixes are actually drop-in replacements for real
  XNA semantics, not just theoretically correct. Then re-attempt porting NetworkPrediction (#100)
  and PeerToPeer (#103) (both previously blocked by the same three gaps) without needing the same
  workarounds. This task lives here (not solely in `cna-samples`) because it's the acceptance test
  for Tasks 12.1-12.3. Coordinate with whichever session is driving `cna-samples` before editing
  files there.

---

## Dependency Graph

```
Phase 0 (build — complete)
  └─> Phase 1 (sharp-runtime — complete)
        └─> Phase 2 (GamerServices — complete)
              └─> Phase 3 (Net enums + simple types — complete)
                    └─> Phase 4 (Net core classes — complete)
                          └─> Phase 5 (ENet backend — complete)
                                ├─> Phase 6 (platform — complete)
                                ├─> Phase 7 (integration tests — complete)
                                ├─> Phase 9 (docs/audit — complete)
                                └─> Phase 12 (cna-samples-driven networking fixes — not started)
              └─> Phase 8 (Avatar — complete)
                    └─> Phase 9 (docs/audit — complete)
                          └─> Phase 10 (Avatar real-rendering engine — complete)
                                └─> Phase 11 (procedural avatar asset generator — 11a/11b/11c/11e
                                    complete; 11d optional, Task 11.25 speculative)
```

---

## File Map: What Gets Created

### include/Microsoft/Xna/Framework/GamerServices/ (new files)
```
Achievement.hpp                   AchievementCollection.hpp
ControllerSensitivity.hpp         FriendCollection.hpp
FriendGamer.hpp                   GameDefaults.hpp
GameDifficulty.hpp                Gamer.hpp
GamerCollection.hpp               GamerPresence.hpp
GamerPresenceMode.hpp             GamerPrivilegeException.hpp
GamerPrivileges.hpp               GamerPrivilegeSetting.hpp
GamerProfile.hpp                  GamerServicesDispatcher.hpp
GamerZone.hpp                     GameUpdateRequiredException.hpp
GuideAlreadyVisibleException.hpp  InviteAcceptedEventArgs.hpp
LeaderboardEntry.hpp              LeaderboardIdentity.hpp
LeaderboardKey.hpp                LeaderboardOutcome.hpp
LeaderboardReader.hpp             LeaderboardWriter.hpp
MessageBoxIcon.hpp                NetworkException.hpp
NetworkNotAvailableException.hpp  NotificationPosition.hpp
PropertyDictionary.hpp            RacingCameraAngle.hpp
SignedInEventArgs.hpp             SignedInGamer.hpp
SignedInGamerCollection.hpp       SignedOutEventArgs.hpp
— plus Avatar types (Phase 8) —
AvatarAnimation.hpp               AvatarAnimationPreset.hpp
AvatarBodyType.hpp                AvatarBone.hpp
AvatarDescription.hpp             AvatarExpression.hpp
AvatarEye.hpp                     AvatarEyebrow.hpp
AvatarMouth.hpp                   AvatarRenderer.hpp
AvatarRendererState.hpp           IAvatarAnimation.hpp
```

### include/Microsoft/Xna/Framework/Net/
```
AvailableNetworkSession.hpp       AvailableNetworkSessionCollection.hpp
GameEndedEventArgs.hpp            GamerJoinedEventArgs.hpp
GamerLeftEventArgs.hpp            GameStartedEventArgs.hpp
HostChangedEventArgs.hpp          LocalNetworkGamer.hpp
NetworkGamer.hpp                  NetworkMachine.hpp
NetworkSession.hpp                NetworkSessionEndedEventArgs.hpp
NetworkSessionEndReason.hpp       NetworkSessionJoinError.hpp
NetworkSessionJoinException.hpp   NetworkSessionProperties.hpp
NetworkSessionState.hpp           NetworkSessionType.hpp
PacketReader.hpp                  PacketWriter.hpp
QualityOfService.hpp              SendDataOptions.hpp
WriteLeaderboardsEventArgs.hpp
```

### include/CNA/Internal/Net/
```
INetworkBackend.hpp    NetTypes.hpp    ENetBackend.hpp
```

### src/ (implementation files mirror include/ paths)

### tests/ (mirror namespace paths)
```
tests/Microsoft/Xna/Framework/GamerServices/
  GamerCollectionTests.cpp   SignedInGamerTests.cpp   GamerServicesDispatcherTests.cpp
  AchievementCollectionTests.cpp   LeaderboardTests.cpp   PropertyDictionaryTests.cpp
  GamerServicesEnumsTests.cpp   ExceptionTests.cpp

tests/Microsoft/Xna/Framework/Net/
  NetworkSessionTests.cpp      PacketReaderWriterTests.cpp
  NetworkSessionPropertiesTests.cpp   QualityOfServiceTests.cpp
  NetworkSessionJoinExceptionTests.cpp   AvailableNetworkSessionTests.cpp
  NetEnumsTests.cpp

tests/Microsoft/Xna/Framework/GamerServices/Avatar/
  AvatarTests.cpp
```

---

## ENet Version and Acquisition

Use **ENet 1.3.17** (latest stable).  
License: MIT — compatible with CNA's MS-PL.  
Add as git submodule or vendor copy under `third_party/enet/`.

---

## Notable C++ Deviations from FNA

| FNA (C#)                         | CNA (C++)                                                | Reason                              |
|----------------------------------|----------------------------------------------------------|-------------------------------------|
| `IAsyncResult` (BCL)             | `System::IAsyncResult` (sharp-runtime)                   | Port .NET interface                 |
| `ManualResetEvent`               | `System::Threading::ManualResetEvent`                    | Port .NET primitive                 |
| `Queue<T>`                       | `std::queue<T>`                                          | C++ standard equivalent             |
| `IEnumerable<int?>`              | `std::optional<intcs>` iteration                         | C++ nullable idiom                  |
| `byte[]` packets                 | `std::vector<bytecs>`                                    | Owned buffer, safe copy semantics   |
| `Array.Copy`                     | `std::copy` / `std::memcpy`                             | C++ equivalent                      |
| `[Flags] enum SendDataOptions`   | `enum class` + bitwise operators                         | Type-safe C++ flags                 |
| `sealed class`                   | `final` in C++                                           | Direct equivalent                   |
| Serialization ctor (protected)   | Stub only                                                | No runtime serialization in C++     |
| FNA stub (local-only networking) | Real ENet transport                                      | Core purpose of this port           |
| `RegionInfo` (BCL)               | `System::Globalization::RegionInfo` stub (sharp-runtime) | Minimal stub sufficient             |
| `object Tag`                     | `System::Object* Tag`                                    | C++ object model                    |
