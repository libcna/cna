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

## Phase 0 — Infrastructure & Build System

- [ ] **Task 0.1** — Add ENet as a third-party dependency under `third_party/enet/`  
  Pull ENet 1.3.x source; verify it compiles as a static library on Linux.

- [ ] **Task 0.2** — Add ENet CMakeLists integration  
  Create `cmake/FindENet.cmake` or embed directly; expose `enet` target.  
  Guard with `if(CNA_ENABLE_NET)` option (default ON).

- [ ] **Task 0.3** — Windows platform support for ENet  
  Link `ws2_32` and `winmm`; confirm ENet compiles on MSVC and MinGW.

- [ ] **Task 0.4** — Web (Emscripten) platform support for ENet  
  ENet supports Emscripten via WebSockets wrapper (`-s USE_PTHREADS=0`).  
  Add Emscripten CMake toolchain guards and linker flags (`-lwebsocket.js`).

- [ ] **Task 0.5** — Android (NDK) platform support for ENet  
  ENet uses POSIX sockets; Android NDK provides these from API level 21.  
  Add `android-ndk` CMake toolchain guards; no extra libs needed.

- [ ] **Task 0.6** — Create `CNA_GamerServices` CMake target  
  Static library; sources under `src/Microsoft/Xna/Framework/GamerServices/`;  
  links against `CNA`.

- [ ] **Task 0.7** — Create `CNA_Net` CMake target  
  Static library; sources under `src/Microsoft/Xna/Framework/Net/` and  
  `src/CNA/Internal/Net/`; links against `CNA_GamerServices` + `enet`.

- [ ] **Task 0.8** — Add CMakePresets for GamerServices and Net  
  Add `gamerservices-debug`, `net-debug`, and `net-release` presets.

---

## Phase 1 — Sharp-Runtime Prerequisites

These .NET runtime types must be added to `sharp-runtime` before any GamerServices or Net code can compile.

- [ ] **Task 1.1** — `System::IAsyncResult`  
  Interface: `getAsyncState()`, `getAsyncWaitHandle()`, `getCompletedSynchronously()`, `getIsCompleted()`.  
  File: `include/System/IAsyncResult.hpp`

- [ ] **Task 1.2** — `System::Threading::WaitHandle`  
  Abstract base; minimal stub.  
  File: `include/System/Threading/WaitHandle.hpp`

- [ ] **Task 1.3** — `System::Threading::ManualResetEvent`  
  Derives from `WaitHandle`; wraps a boolean signaled state.  
  Constructor `ManualResetEvent(bool initialState)`.  
  File: `include/System/Threading/ManualResetEvent.hpp`

- [ ] **Task 1.4** — `System::IO::Stream` (if not present)  
  Abstract: `Read()`, `Write()`, `Seek()`, `getLength()`, `getPosition()`, `setPosition()`.  
  File: `include/System/IO/Stream.hpp`

- [ ] **Task 1.5** — `System::IO::MemoryStream`  
  Derives from `Stream`; backed by `std::vector<uint8_t>`.  
  Constructors: default, capacity-hint, from-span.  
  Methods: `ToArray()`, `GetBuffer()`, `Seek()`, `Read()`, `Write()`.  
  File: `include/System/IO/MemoryStream.hpp`

- [ ] **Task 1.6** — `System::IO::BinaryReader`  
  Wraps `Stream*`; provides `ReadBoolean()`, `ReadByte()`, `ReadInt16()`, `ReadInt32()`,  
  `ReadInt64()`, `ReadSingle()`, `ReadDouble()`, `ReadString()`, `getBaseStream()`.  
  File: `include/System/IO/BinaryReader.hpp`

- [ ] **Task 1.7** — `System::IO::BinaryWriter`  
  Wraps `Stream*`; provides `Write()` overloads for all primitive types and `std::string`,  
  `getBaseStream()`, `Flush()`.  
  File: `include/System/IO/BinaryWriter.hpp`

- [ ] **Task 1.8** — `System::Runtime::Serialization::SerializationInfo` stub  
  Minimal stub; required by exception protected constructors.  
  File: `include/System/Runtime/Serialization/SerializationInfo.hpp`

- [ ] **Task 1.9** — `System::Runtime::Serialization::StreamingContext` stub  
  Same rationale.  
  File: `include/System/Runtime/Serialization/StreamingContext.hpp`

- [ ] **Task 1.10** — `System::Collections::ObjectModel::ReadOnlyCollection<T>`  
  Template wrapper around `std::vector<T>`; read-only `operator[]`, `getCount()`, `begin()`, `end()`.  
  File: `include/System/Collections/ObjectModel/ReadOnlyCollection.hpp`

- [ ] **Task 1.11** — `System::Globalization::RegionInfo` stub  
  Required by `GamerProfile`. Minimal stub with constructor from locale string.  
  File: `include/System/Globalization/RegionInfo.hpp`

- [ ] **Task 1.12** — Sharp-runtime build & unit tests  
  Verify all new types compile and link; add at least one test per new type.

---

## Phase 2 — GamerServices: Complete Port

All 39 source files in `GamerServices/`. Three files already have stub headers  
(`GamerServicesComponent.hpp`, `GamerServicesNotAvailableException.hpp`, `Guide.hpp`);  
these must be completed/verified against FNA source.

### 2a — Enums (no dependencies, port first)

- [ ] **Task 2.1** — `ControllerSensitivity` (enum)  
  Values: `Low`, `Medium`, `High`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/ControllerSensitivity.hpp`

- [ ] **Task 2.2** — `GameDifficulty` (enum)  
  Values: `Easy`, `Normal`, `Hard`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GameDifficulty.hpp`

- [ ] **Task 2.3** — `GamerPresenceMode` (enum, 62 values)  
  Copy all values from FNA verbatim.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerPresenceMode.hpp`

- [ ] **Task 2.4** — `GamerPrivilegeSetting` (enum)  
  Values: `Blocked`, `FriendsOnly`, `Everyone`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerPrivilegeSetting.hpp`

- [ ] **Task 2.5** — `GamerZone` (enum)  
  Values: `Unknown`, `Recreation`, `Pro`, `Family`, `Underground`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerZone.hpp`

- [ ] **Task 2.6** — `LeaderboardKey` (enum)  
  Values: `BestScoreLifeTime`, `BestScoreRecent`, `BestTimeLifeTime`, `BestTimeRecent`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/LeaderboardKey.hpp`

- [ ] **Task 2.7** — `LeaderboardOutcome` (enum)  
  Values: `None`, `Win`, `Loss`, `Tie`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/LeaderboardOutcome.hpp`

- [ ] **Task 2.8** — `MessageBoxIcon` (enum)  
  Values: `None`, `Error`, `Warning`, `Alert`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/MessageBoxIcon.hpp`

- [ ] **Task 2.9** — `NotificationPosition` (enum)  
  9 values: `TopLeft` … `BottomRight`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/NotificationPosition.hpp`

- [ ] **Task 2.10** — `RacingCameraAngle` (enum)  
  Values: `Back`, `Front`, `Inside`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/RacingCameraAngle.hpp`

### 2b — Exceptions

- [ ] **Task 2.11** — `NetworkException`  
  FNA: `GamerServices/NetworkException.cs`  
  Derives from `std::runtime_error` (or `System::Exception` if present).  
  4 constructors: default, message, message+inner, protected serialization.  
  File: `include/Microsoft/Xna/Framework/GamerServices/NetworkException.hpp`

- [ ] **Task 2.12** — `NetworkNotAvailableException`  
  Derives from `NetworkException`; same 4-constructor pattern.  
  File: `include/Microsoft/Xna/Framework/GamerServices/NetworkNotAvailableException.hpp`

- [ ] **Task 2.13** — `GamerPrivilegeException`  
  Derives from `std::runtime_error`; 4 constructors.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp`

- [ ] **Task 2.14** — `GamerServicesNotAvailableException` (complete existing stub)  
  Verify existing `.hpp` against FNA; add `.cpp` if needed.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp`

- [ ] **Task 2.15** — `GameUpdateRequiredException`  
  Derives from `std::runtime_error`; 4 constructors.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GameUpdateRequiredException.hpp`

- [ ] **Task 2.16** — `GuideAlreadyVisibleException`  
  Derives from `std::runtime_error`; 4 constructors.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp`

### 2c — Data Structures and Simple Classes

- [ ] **Task 2.17** — `PropertyDictionary`  
  FNA: `GamerServices/PropertyDictionary.cs`  
  Implements `IDictionary<std::string, System::Object*>`.  
  Backed by `std::unordered_map<std::string, std::shared_ptr<System::Object>>`.  
  Methods: `ContainsKey()`, `TryGetValue()`, `GetEnumerator()`, `operator[]`.  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp`

- [ ] **Task 2.18** — `LeaderboardIdentity` (struct)  
  FNA: `GamerServices/LeaderboardIdentity.cs`  
  Properties: `Key` (String), `GameMode` (intcs).  
  Static factory: `Create(LeaderboardKey)`, `Create(LeaderboardKey, int)`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/LeaderboardIdentity.hpp`

- [ ] **Task 2.19** — `GamerPresence`  
  FNA: `GamerServices/GamerPresence.cs`  
  Properties: `PresenceMode` (get+set triggers string update), `PresenceValue` (get+set).  
  Internal: `presenceModeStrings[]` static array; `SetPresenceModeStringEXT()` no-op stub.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerPresence.hpp`

- [ ] **Task 2.20** — `GamerPrivileges`  
  FNA: `GamerServices/GamerPrivileges.cs`  
  Properties (read-only external): `AllowCommunication`, `AllowOnlineSessions`, `AllowPremiumContent`,  
  `AllowProfileViewing`, `AllowPurchaseContent`, `AllowTradeContent`, `AllowUserCreatedContent`.  
  Internal constructor with stub defaults.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp`

- [ ] **Task 2.21** — `GameDefaults`  
  FNA: `GamerServices/GameDefaults.cs`  
  Properties (read-only external): `GameDifficulty`, `ControllerSensitivity`, `PrimaryColor`,  
  `SecondaryColor`, `AutoAim`, `AutoCenter`, `MoveWithRightThumbStick`, `InvertYAxis`,  
  `ManualTransmission`, `RacingCameraAngle`, `AccelerateWithButtons`.  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GameDefaults.hpp`

- [ ] **Task 2.22** — `Achievement`  
  FNA: `GamerServices/Achievement.cs`  
  Properties (read-only external): `Description`, `DisplayBeforeEarned`, `EarnedDateTime`,  
  `EarnedOnline`, `GamerScore`, `HowToEarn`, `IsEarned`, `Key`, `Name`.  
  Internal constructor (all fields).  
  File: `include/Microsoft/Xna/Framework/GamerServices/Achievement.hpp`

- [ ] **Task 2.23** — `SignedInEventArgs`  
  Derives from `System::EventArgs`; property `Gamer` (`SignedInGamer*`).  
  File: `include/Microsoft/Xna/Framework/GamerServices/SignedInEventArgs.hpp`

- [ ] **Task 2.24** — `SignedOutEventArgs`  
  Derives from `System::EventArgs`; property `Gamer` (`SignedInGamer*`).  
  File: `include/Microsoft/Xna/Framework/GamerServices/SignedOutEventArgs.hpp`

- [ ] **Task 2.25** — `InviteAcceptedEventArgs`  
  Derives from `System::EventArgs`; properties `Gamer` (`SignedInGamer*`), `IsCurrentSession` (`bool`).  
  File: `include/Microsoft/Xna/Framework/GamerServices/InviteAcceptedEventArgs.hpp`

### 2d — Collection Classes

- [ ] **Task 2.26** — `GamerCollection<T>`  
  FNA: `GamerServices/GamerCollection.cs`  
  Template; derives from `System::Collections::ObjectModel::ReadOnlyCollection<T*>`.  
  `T` constrained to `Gamer` subclasses.  
  Internal `collection` field (`std::vector<T*>`).  
  Custom enumerator struct `GamerCollectionEnumerator` with `Current`, `MoveNext()`, `Reset()`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerCollection.hpp`

- [ ] **Task 2.27** — `AchievementCollection`  
  FNA: `GamerServices/AchievementCollection.cs`  
  Implements `IList<Achievement*>`, `IDisposable`.  
  `operator[](int)`, `operator[](std::string)` (throws `std::out_of_range` if not found).  
  `IsDisposed`, `getCount()`, `Dispose()`.  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp`

- [ ] **Task 2.28** — `FriendGamer`  
  FNA: `GamerServices/FriendGamer.cs`  
  `sealed` → `final`; derives from `Gamer`.  
  Properties (read-only external): `FriendRequestReceivedFrom`, `FriendRequestSentTo`, `HasVoice`,  
  `InviteAccepted`, `InviteReceivedFrom`, `InviteRejected`, `InviteSentTo`, `IsAway`, `IsBusy`,  
  `IsJoinable`, `IsOnline`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp`

- [ ] **Task 2.29** — `FriendCollection`  
  FNA: `GamerServices/FriendCollection.cs`  
  `sealed` → `final`; derives from `GamerCollection<FriendGamer>`; implements `IDisposable`.  
  Property `IsDisposed`; method `Dispose()`.  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/GamerServices/FriendCollection.hpp`

- [ ] **Task 2.30** — `SignedInGamerCollection`  
  FNA: `GamerServices/SignedInGamerCollection.cs`  
  `sealed` → `final`; derives from `GamerCollection<SignedInGamer>`.  
  `operator[](PlayerIndex)` → returns `SignedInGamer*` (null if out of range).  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp`

### 2e — Core Gamer Classes

- [ ] **Task 2.31** — `Gamer` (abstract base class)  
  FNA: `GamerServices/Gamer.cs`  
  Abstract; derives from `System::Object`.  
  Properties: `DisplayName`, `Gamertag`, `IsDisposed`, `LeaderboardWriter`, `Tag`.  
  Static property: `SignedInGamers` (`SignedInGamerCollection*`, lazy-init singleton).  
  Internal `GamerAction` class implementing `IAsyncResult`:  
    fields `AsyncState`, `CompletedSynchronously`, `IsCompleted`, `AsyncWaitHandle`, `Callback`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/Gamer.hpp`

- [ ] **Task 2.32** — `GamerProfile`  
  FNA: `GamerServices/GamerProfile.cs`  
  `sealed` → `final`; implements `IDisposable`.  
  Properties (read-only external): `GamerScore`, `GamerZone`, `Motto`, `Region` (`RegionInfo`),  
  `Reputation`, `TitlesPlayed`, `TotalAchievements`, `IsDisposed`.  
  Internal constructor with stub defaults.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerProfile.hpp`

- [ ] **Task 2.33** — `LeaderboardEntry`  
  FNA: `GamerServices/LeaderboardEntry.cs`  
  `sealed` → `final`.  
  Properties: `Columns` (`PropertyDictionary`), `Gamer*`, `Rating` (get+set, `longcs`), `RankingEXT`.  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp`

- [ ] **Task 2.34** — `LeaderboardWriter`  
  FNA: `GamerServices/LeaderboardWriter.cs`  
  `sealed` → `final`.  
  Method `GetLeaderboard(LeaderboardIdentity)` → throws `std::runtime_error("NotSupportedException")`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/LeaderboardWriter.hpp`

- [ ] **Task 2.35** — `LeaderboardReader`  
  FNA: `GamerServices/LeaderboardReader.cs`  
  `sealed` → `final`; implements `IDisposable`.  
  Properties: `IsDisposed`, `CanPageDown`, `CanPageUp`, `Entries` (`ReadOnlyCollection<LeaderboardEntry*>`),  
  `LeaderboardIdentity`, `PageStart`, `TotalLeaderboardSize`.  
  Static async methods: `BeginRead()` overloads, `EndRead()`.  
  Instance methods: `PageDown()`, `PageUp()`, `Dispose()`.  
  Internal state: `entryCache`, `pageSize`, `isFriendBoard`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp`

- [ ] **Task 2.36** — `SignedInGamer`  
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

- [ ] **Task 2.37** — `GamerServicesDispatcher` (static class)  
  FNA: `GamerServices/GamerServicesDispatcher.cs`  
  Static properties: `IsInitialized`, `WindowHandle` (`intptr_t`).  
  Static event: `InstallingTitleUpdate` (`EventHandler<EventArgs>`).  
  Static methods:  
    `Initialize(IServiceProvider*)` — create 4 stub `SignedInGamer` objects, populate `Gamer::SignedInGamers`.  
    `Update()` — no-op stub.  
    `UpdateAsync() → bool` — returns `false` (signals completion).  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp`

- [ ] **Task 2.38** — `GamerServicesComponent` (complete existing stub)  
  FNA: `GamerServices/GamerServicesComponent.cs`  
  Derives from `GameComponent`.  
  `Initialize()` → sets `GamerServicesDispatcher::WindowHandle`, calls `GamerServicesDispatcher::Initialize()`.  
  `Update(GameTime)` → calls `GamerServicesDispatcher::Update()`.  
  Verify existing `.hpp` is correct; add `.cpp`.  
  File: `include/Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp`

- [ ] **Task 2.39** — `Guide` (static class, complete existing stub)  
  FNA: `GamerServices/Guide.cs`  
  Static properties: `IsScreenSaverEnabled` (maps to SDL3 screensaver), `IsTrialMode`, `IsVisible`,  
  `NotificationPosition`, `SimulateTrialMode`.  
  Static methods: `ShowMessageBox()` (async, stub), `ShowKeyboardInput()` (async, stub),  
  `ShowSignIn()`, `BeginShowMessageBox()`, `EndShowMessageBox()`,  
  `BeginShowKeyboardInput()`, `EndShowKeyboardInput()`.  
  Verify existing `.hpp` is complete; add `.cpp` with SDL3-based screensaver calls.  
  File: `include/Microsoft/Xna/Framework/GamerServices/Guide.hpp`

- [ ] **Task 2.40** — Unit tests for Phase 2 types  
  Test all enum values.  
  Test exception constructors (all four variants).  
  Test `GamerCollection<T>` iteration and `operator[]`.  
  Test `AchievementCollection` string indexer throws on miss.  
  Test `GamerServicesDispatcher::Initialize()` populates `Gamer::SignedInGamers` with 4 entries.  
  Test `SignedInGamerCollection::operator[](PlayerIndex)` returns null for out-of-range.

---

## Phase 3 — Net XNA API: Enums and Simple Types

- [ ] **Task 3.1** — `NetworkSessionEndReason` (enum)  
  Values: `ClientSignedOut`, `HostEndedSession`, `RemovedByHost`, `Disconnected`.  
  File: `include/Microsoft/Xna/Framework/Net/NetworkSessionEndReason.hpp`

- [ ] **Task 3.2** — `NetworkSessionJoinError` (enum)  
  Values: `SessionNotFound`, `SessionNotJoinable`, `SessionFull`.  
  File: `include/Microsoft/Xna/Framework/Net/NetworkSessionJoinError.hpp`

- [ ] **Task 3.3** — `NetworkSessionState` (enum)  
  Values: `Lobby`, `Playing`, `Ended`.  
  File: `include/Microsoft/Xna/Framework/Net/NetworkSessionState.hpp`

- [ ] **Task 3.4** — `NetworkSessionType` (enum)  
  Values: `Local`, `SystemLink`, `PlayerMatch`, `Ranked`, `LocalWithLeaderboards`.  
  File: `include/Microsoft/Xna/Framework/Net/NetworkSessionType.hpp`

- [ ] **Task 3.5** — `SendDataOptions` (enum, flags)  
  Values: `None=0`, `Reliable=1`, `InOrder=2`, `ReliableInOrder=3`, `Chat=4`.  
  Use `enum class` with bitwise operator overloads (`|`, `&`, `~`).  
  File: `include/Microsoft/Xna/Framework/Net/SendDataOptions.hpp`

- [ ] **Task 3.6** — `QualityOfService`  
  Properties (read-only): `AverageRoundtripTime` (`TimeSpan`), `BytesPerSecondDownstream` (`intcs`),  
  `BytesPerSecondUpstream` (`intcs`), `IsAvailable` (`bool`), `MinimumRoundtripTime` (`TimeSpan`).  
  Internal constructor only.  
  File: `include/Microsoft/Xna/Framework/Net/QualityOfService.hpp`

- [ ] **Task 3.7** — `NetworkSessionProperties`  
  Implements `IList<std::optional<intcs>>`, `ICollection`, `IEnumerable`.  
  Backed by `std::vector<std::optional<intcs>>`.  
  Public `operator[]`, `getCount()`, `GetEnumerator()`.  
  IList: `IndexOf()`, `Insert()`, `RemoveAt()`.  
  ICollection: `IsReadOnly`, `Add()`, `Remove()`, `Contains()`, `Clear()`, `CopyTo()`.  
  File: `include/Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp`

- [ ] **Task 3.8** — `GameEndedEventArgs`  
  Derives from `System::EventArgs`; default constructor only.  
  File: `include/Microsoft/Xna/Framework/Net/GameEndedEventArgs.hpp`

- [ ] **Task 3.9** — `GameStartedEventArgs`  
  Derives from `System::EventArgs`; default constructor only.  
  File: `include/Microsoft/Xna/Framework/Net/GameStartedEventArgs.hpp`

- [ ] **Task 3.10** — `GamerJoinedEventArgs`  
  Derives from `System::EventArgs`; property `Gamer` (`NetworkGamer*`).  
  File: `include/Microsoft/Xna/Framework/Net/GamerJoinedEventArgs.hpp`

- [ ] **Task 3.11** — `GamerLeftEventArgs`  
  Derives from `System::EventArgs`; property `Gamer` (`NetworkGamer*`).  
  File: `include/Microsoft/Xna/Framework/Net/GamerLeftEventArgs.hpp`

- [ ] **Task 3.12** — `HostChangedEventArgs`  
  Derives from `System::EventArgs`; properties `OldHost`, `NewHost` (`NetworkGamer*`).  
  File: `include/Microsoft/Xna/Framework/Net/HostChangedEventArgs.hpp`

- [ ] **Task 3.13** — `NetworkSessionEndedEventArgs`  
  Derives from `System::EventArgs`; property `EndReason` (`NetworkSessionEndReason`).  
  File: `include/Microsoft/Xna/Framework/Net/NetworkSessionEndedEventArgs.hpp`

- [ ] **Task 3.14** — `WriteLeaderboardsEventArgs`  
  Derives from `System::EventArgs`; properties `Gamer` (`NetworkGamer*`), `IsLeaving` (`bool`).  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/Net/WriteLeaderboardsEventArgs.hpp`

- [ ] **Task 3.15** — `NetworkSessionJoinException`  
  Derives from `GamerServices::NetworkException`.  
  Property `JoinError` (`NetworkSessionJoinError`).  
  4 constructors: default, message, message+error, message+inner; protected serialization ctor.  
  File: `include/Microsoft/Xna/Framework/Net/NetworkSessionJoinException.hpp`

- [ ] **Task 3.16** — Unit tests for Phase 3 types  
  All constructors, enum values, `NetworkSessionProperties` indexer and collection interface,  
  `SendDataOptions` bitwise operations.

---

## Phase 4 — Net XNA API: Core Classes

- [ ] **Task 4.1** — `NetworkMachine`  
  Property `Gamers` (`GamerCollection<NetworkGamer>`).  
  Method `RemoveFromSession()` → throws `std::runtime_error("NotImplementedException")`.  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/Net/NetworkMachine.hpp`

- [ ] **Task 4.2** — `NetworkGamer`  
  Derives from `GamerServices::Gamer`.  
  Properties: `HasLeftSession`, `HasVoice`, `getId()` (`bytecs`, returns 0), `IsGuest`,  
  `IsHost` (returns true), `IsLocal` (returns `dynamic_cast<LocalNetworkGamer*>(this) != nullptr`),  
  `IsMutedByLocalUser`, `IsPrivateSlot`, `IsReady` (get+set), `IsTalking`,  
  `Machine` (`NetworkMachine`), `RoundtripTime` (`TimeSpan`), `Session` (`NetworkSession*`).  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/Net/NetworkGamer.hpp`

- [ ] **Task 4.3** — `LocalNetworkGamer` (`final`)  
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

- [ ] **Task 4.4** — `PacketReader`  
  Derives from `System::IO::BinaryReader`.  
  Properties: `getLength()`, `getPosition()`, `setPosition()`.  
  Constructors: default, capacity-hint.  
  Extra read methods: `ReadColor()`, `ReadMatrix()`, `ReadQuaternion()`, `ReadVector2()`, `ReadVector3()`, `ReadVector4()`.  
  Override `ReadSingle()`, `ReadDouble()`.  
  File: `include/Microsoft/Xna/Framework/Net/PacketReader.hpp`

- [ ] **Task 4.5** — `PacketWriter`  
  Derives from `System::IO::BinaryWriter`.  
  Properties: `getLength()`, `getPosition()`, `setPosition()`.  
  Constructors: default, capacity-hint.  
  Extra write methods: `Write(Color)`, `Write(Matrix)`, `Write(Quaternion)`, `Write(Vector2)`, `Write(Vector3)`, `Write(Vector4)`.  
  Override `Write(float)`, `Write(double)`.  
  File: `include/Microsoft/Xna/Framework/Net/PacketWriter.hpp`

- [ ] **Task 4.6** — `AvailableNetworkSession`  
  Properties (read-only): `CurrentGamerCount`, `HostGamertag`, `OpenPrivateGamerSlots`,  
  `OpenPublicGamerSlots`, `QualityOfService`, `SessionProperties`.  
  Internal constructor (all fields).  
  File: `include/Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp`

- [ ] **Task 4.7** — `AvailableNetworkSessionCollection` (`final`)  
  Derives from `System::Collections::ObjectModel::ReadOnlyCollection<AvailableNetworkSession*>`.  
  Implements `System::IDisposable`.  
  Property `IsDisposed`.  
  Method `Dispose()` — clears collection, sets flag.  
  Internal constructor.  
  File: `include/Microsoft/Xna/Framework/Net/AvailableNetworkSessionCollection.hpp`

- [ ] **Task 4.8** — `NetworkSession` — internal types  
  Internal `NetworkEventType` enum: `PacketSend`, `GamerJoin`, `GamerLeave`, `HostChange`, `StateChange`.  
  Internal `NetworkEvent` struct: `Type`, `Gamer*`, `Packet` (`std::vector<bytecs>`),  
  `Reliable` (`SendDataOptions`), `State`, `Reason`.  
  Internal `NetworkSessionAction` class implementing `IAsyncResult`.  
  (Private section of `NetworkSession.hpp`.)

- [ ] **Task 4.9** — `NetworkSession` — public properties  
  Constants `MaxSupportedGamers = 31`, `MaxPreviousGamers = 100`.  
  Properties: `IsDisposed`, `AllGamers`, `LocalGamers`, `RemoteGamers`, `PreviousGamers`,  
  `AllowHostMigration`, `AllowJoinInProgress`, `BytesPerSecondReceived`, `BytesPerSecondSent`,  
  `Host`, `IsEveryoneReady`, `IsHost`, `MaxGamers`, `PrivateGamerSlots`, `SessionProperties`,  
  `SessionState`, `SessionType`, `SimulatedLatency`, `SimulatedPacketLoss`.

- [ ] **Task 4.10** — `NetworkSession` — public events  
  Instance events: `GameStarted`, `GameEnded`, `GamerJoined`, `GamerLeft`, `HostChanged`,  
  `SessionEnded`, `WriteArbitratedLeaderboard`, `WriteUnarbitratedLeaderboard`, `WriteTrueSkill`.  
  Static event: `InviteAccepted`.

- [ ] **Task 4.11** — `NetworkSession` — constructor and `Dispose()`  
  Internal constructor; initialise gamer lists, event queue, host pointer.  
  `Dispose()`: flush packet queues, clear static `activeSession_`.

- [ ] **Task 4.12** — `NetworkSession` — `Update()` method  
  Drain `networkEvents_` queue; dispatch events by type.

- [ ] **Task 4.13** — `NetworkSession` — session management methods  
  `AddLocalGamer()`, `FindGamerById()`, `ResetReady()`, `StartGame()`, `EndGame()`.  
  Internal `SendNetworkEvent()`.

- [ ] **Task 4.14** — `NetworkSession` — static Create methods  
  `Create(type, maxLocal, maxGamers)`.  
  `Create(type, maxLocal, maxGamers, privateSlots, props)`.  
  `Create(type, localGamers, maxGamers, privateSlots, props)`.  
  All poll `GamerServicesDispatcher::UpdateAsync()`.

- [ ] **Task 4.15** — `NetworkSession` — static BeginCreate / EndCreate  
  Three `BeginCreate` overloads; validate args; populate `activeAction_`.  
  `EndCreate` constructs session; clears action.

- [ ] **Task 4.16** — `NetworkSession` — static Find methods  
  `Find(type, maxLocal, props)`, `Find(type, localGamers, props)`.  
  `BeginFind` (2 overloads), `EndFind`.

- [ ] **Task 4.17** — `NetworkSession` — static Join methods  
  `Join(availableSession)`, `BeginJoin`, `EndJoin`.  
  `JoinInvited(maxLocal)`, `JoinInvited(localGamers)`.  
  `BeginJoinInvited` (2 overloads), `EndJoinInvited`.

- [ ] **Task 4.18** — Unit tests for Phase 4 types  
  PacketReader/PacketWriter round-trip for each XNA type.  
  `NetworkSessionProperties` indexer and IList interface.  
  `NetworkSession` state machine: Create → StartGame → EndGame → Dispose.  
  `GamerJoined` event fires on construction.  
  `FindGamerById` returns correct gamer.  
  `ResetReady` throws when not host.  
  `AvailableNetworkSessionCollection` Dispose clears.

---

## Phase 5 — ENet Backend (CNA Internal Layer)

All backend code under `src/CNA/Internal/Net/` and `include/CNA/Internal/Net/`.

### 5a — Backend Contract

- [ ] **Task 5.1** — Define `CNA::Internal::Net::INetworkBackend` interface  
  Methods: `Initialize()`, `Shutdown()`, `HostSession()`, `FindSessions()`, `JoinSession()`,  
  `SendPacket()`, `Poll()`, `GetRTT()`, `DisconnectPeer()`, `DestroySession()`.  
  File: `include/CNA/Internal/Net/INetworkBackend.hpp`

- [ ] **Task 5.2** — Define supporting data types  
  `NetworkSessionConfig`, `SessionQuery`, `DiscoveredSession`, `NetEvent`, `SessionHandle`, `PeerHandle`.  
  File: `include/CNA/Internal/Net/NetTypes.hpp`

### 5b — ENet Implementation

- [ ] **Task 5.3** — `ENetBackend::Initialize()` / `Shutdown()`  
  `enet_initialize()` / `enet_deinitialize()`.

- [ ] **Task 5.4** — Session advertisement for `SystemLink` (LAN UDP broadcast)  
  Host sends broadcast on port 3074; heartbeat every 2 s.  
  Find listens for replies; collects `DiscoveredSession` list.

- [ ] **Task 5.5** — Session advertisement for `PlayerMatch` (relay/direct)  
  Use relay server address from `CNA_NET_RELAY_HOST` env var; LAN fallback if unset.

- [ ] **Task 5.6** — `ENetBackend::JoinSession()`  
  Connect ENet client to host; assign `PeerHandle`; send gamer-list on connect.

- [ ] **Task 5.7** — `ENetBackend::SendPacket()` with channel mapping  
  `None` → unreliable ch0; `Reliable` → reliable ch0; `InOrder` → unreliable-sequenced ch1;  
  `ReliableInOrder` → reliable ch1; `Chat` → reliable ch2.

- [ ] **Task 5.8** — `ENetBackend::Poll()`  
  Non-blocking `enet_host_service()`; translate ENet events → `NetEvent` list.

- [ ] **Task 5.9** — Host migration  
  On host disconnect: elect new host (lowest peer ID); fire `HostChanged`; update `Host`.

- [ ] **Task 5.10** — Latency simulation  
  `enet_peer_throttle_configure()` for loss; delay queue for latency.

- [ ] **Task 5.11** — QoS measurement  
  Populate `QualityOfService` from `enet_peer->roundTripTime` and bandwidth counters.

### 5c — Wire XNA API to ENet Backend

- [ ] **Task 5.12** — Wire `EndCreate` → `INetworkBackend::HostSession()`
- [ ] **Task 5.13** — Wire `EndFind` → `INetworkBackend::FindSessions()` → `AvailableNetworkSession` list
- [ ] **Task 5.14** — Wire `EndJoin` → `INetworkBackend::JoinSession()`
- [ ] **Task 5.15** — Wire `NetworkSession::Update()` → `INetworkBackend::Poll()` → enqueue events
- [ ] **Task 5.16** — Wire `LocalNetworkGamer::SendData()` → `INetworkBackend::SendPacket()`
- [ ] **Task 5.17** — Wire incoming data → `LocalNetworkGamer::packetQueue`
- [ ] **Task 5.18** — Wire `NetworkSession::Dispose()` → `INetworkBackend::DestroySession()`

---

## Phase 6 — Platform-Specific Work

- [ ] **Task 6.1** — Linux: two-process loopback test (host + client, same machine)
- [ ] **Task 6.2** — Windows: ENet with WinSock2; same loopback test
- [ ] **Task 6.3** — Web (Emscripten): ENet WebSocket adaptation; disable `SystemLink`; relay only
- [ ] **Task 6.4** — Android (NDK): add `INTERNET` permission to manifest; test on emulator
- [ ] **Task 6.5** — Multiplatform CMake guards (`if(EMSCRIPTEN)`, `if(ANDROID)`, `if(WIN32)`)

---

## Phase 7 — Integration Tests

- [ ] **Task 7.1** — Two-endpoint loopback: host + join in same process; PacketWriter/Reader round-trip
- [ ] **Task 7.2** — State machine: Create → StartGame → EndGame → Dispose; verify events fire
- [ ] **Task 7.3** — GamerJoined / GamerLeft event dispatch
- [ ] **Task 7.4** — FindSessions returns discovered host entry
- [ ] **Task 7.5** — SendDataOptions channel mapping (unreliable vs reliable)
- [ ] **Task 7.6** — ResetReady clears all gamer ready flags
- [ ] **Task 7.7** — NetworkSessionJoinException round-trip through all 4 constructors

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

**Not started.** Added after the user decided CNA should actually have avatars, and that the real
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

- [ ] **Task 11.7** — `tools/avatar_builder/export_gltf.py` + `tools/avatar_builder/generate_avatar.py`  
  Orchestrates Tasks 11.1–11.6 and exports via `bpy.ops.export_scene.gltf(...)`. Driven headless:
  `blender --background --python generate_avatar.py -- --gender male --out assets/avatar/generated/male_avatar.glb`
  (and again with `--gender female`, same skeleton/rig, adjusted body proportions/scale). Output
  must be deterministic (same inputs → byte-identical or near-identical output) and must reopen
  cleanly in Blender.

- [ ] **Task 11.8** — `tools/avatar_builder/validate_gltf.py`  
  Sanity-check each exported GLB using `pygltflib` (already a project dependency from Phase 10):
  non-empty mesh, skin/joints present with the expected bone count/names from Task 11.1, both
  `Stand0`/`Wave` animations present, both `Smile`/`Blink` shape keys present. Fail loudly, don't
  silently accept a hollow/broken export.

- [ ] **Task 11.9** — `tools/avatar_builder/README.md`  
  Usage instructions, the canonical skeleton bone list, design rationale (why procedural, why no
  retargeting needed), how to run each script standalone vs. via the top-level driver, and a clear
  statement of what's placeholder-quality vs. intended to be improved later.

### 11b — CNA Integration (first real, non-synthetic proof)

- [ ] **Task 11.10** — Feed `male_avatar.glb`/`female_avatar.glb` through the existing
  `tools/avatar_asset_pipeline/convert_avatar.py` (built in Phase 10, structurally verified only
  against a synthetic fixture until now) to produce real
  `.skinnedmodel.json`/`.skeleton.bin`/`.clip.bin` content. This is the first time that converter
  runs against real generated content — expect and fix real bugs, don't assume it works unchanged.

- [ ] **Task 11.11** — Wire the converted content through
  `ContentManager::Load<shared_ptr<SkinnedModelEXT>>` and `AvatarRenderer::EnableRealRenderingEXT`/
  `DrawRealEXT` in a real, non-headless windowed demo (not another synthetic-fixture integration
  test) — the actual visual proof that Phase 10's engine work and Phase 11's content now draw a
  real, if simple, animated humanoid on screen.

- [ ] **Task 11.12** — Map `AvatarBodyType::Male`/`Female` to the two generated bodies at whatever
  call-site convention makes sense (document the chosen approach in
  `docs/avatar-real-rendering-ext.md`, since Phase 8's faithful `AvatarDescription` doesn't carry
  real body-type data that could drive this automatically).

### 11c — Iteration (procedural variety) — deferred, lower priority than 11a/11b

- [ ] **Task 11.13** — Parametric body variation (height, shoulder width, head size) as script
  parameters, conceptually echoing `AvatarDescription`'s customization intent without attempting
  to reconstruct its real, undocumented byte format.
- [ ] **Task 11.14** — Additional hair styles / clothing variants as separate attachable GLB
  pieces rather than baked into the base body.
- [ ] **Task 11.15** — Additional animation presets beyond `Stand0`/`Wave`, working toward covering
  more of the 31 `AvatarAnimationPreset` values with self-authored placeholder motion.

### 11d — Future, optional, not started

- [ ] **Task 11.16** — Revisit MakeHuman or CharMorph/Blender as a higher-quality body *source*
  (better anatomy/topology than fully procedural generation can practically achieve) only if the
  user wants to invest in resolving the automation/permission questions documented in `NEXT.md`
  directly — not assumed, not scheduled, purely optional future work.

---

## Dependency Graph

```
Phase 0 (build)
  └─> Phase 1 (sharp-runtime)
        └─> Phase 2 (GamerServices — complete)
              └─> Phase 3 (Net enums + simple types)
                    └─> Phase 4 (Net core classes)
                          └─> Phase 5 (ENet backend)
                                ├─> Phase 6 (platform)
                                ├─> Phase 7 (integration tests)
                                └─> Phase 9 (docs/audit)
              └─> Phase 8 (Avatar — complete)
                    └─> Phase 9 (docs/audit)
                          └─> Phase 10 (Avatar real-rendering engine — complete)
                                └─> Phase 11 (procedural avatar asset generator — not started)
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
