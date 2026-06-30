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
  Previous graphics phases (1–31) are complete.
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
- **1830 / 1830 unit tests pass** (including all new GamerServices tests).

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

### What is NOT done yet in the Net phase

- **`Gamer` (complete)** — stub exists (`Gamer.hpp`/`Gamer.cpp`), needs full port (Task 2.31).
- **`GamerProfile`, `LeaderboardEntry`, `LeaderboardWriter`, `LeaderboardReader`, `SignedInGamer`** — not started (Tasks 2.31–2.36).
- **`GamerServicesDispatcher`, `GamerServicesComponent` (complete), `Guide` (complete)** — stubs exist but not fully implemented (Tasks 2.37–2.40).
- **Phase 3: Net enums** (15 files) — not started.
- **Phase 4: Net core classes** (`NetworkSession`, etc.) — not started.
- **Phase 5: ENet backend** — not started.
- **Phase 6: Platform support** — not started.
- **Phase 7: Integration tests** — not started.
- **Phase 8: Avatar** — deferred.

---

## 3. Recent changes

| Commit | Files | Change |
|---|---|---|
| Task 2.26–2.30 | `GamerCollection.hpp` (template), `AchievementCollection.hpp/.cpp`, `FriendGamer.hpp/.cpp`, `FriendCollection.hpp/.cpp`, `SignedInGamerCollection.hpp/.cpp`, `Gamer.hpp/.cpp` | Collections hierarchy + Gamer stub; 14 tests |
| Task 2.23–2.25 | `SignedInEventArgs.hpp/.cpp`, `SignedOutEventArgs.hpp/.cpp`, `InviteAcceptedEventArgs.hpp/.cpp` | Event arg classes using forward-declared `SignedInGamer*`; 7 tests |
| Task 2.17–2.22 | `PropertyDictionary.hpp/.cpp`, `LeaderboardIdentity.hpp/.cpp`, `GamerPresence.hpp/.cpp`, `GamerPrivileges.hpp/.cpp`, `GameDefaults.hpp/.cpp`, `Achievement.hpp/.cpp` | Data structures; 23 tests |
| Task 2.11–2.16 | 6 exception headers + `.cpp` + `GamerServicesExceptionsTests.cpp` | Exceptions inheriting `System::Exception`; 34 tests |
| Task 2.1–2.10 | 10 enum headers + `GamerServicesEnumsTests.cpp` | All GamerServices enums; 11 tests |
| Task 0.1, 0.6–0.7 | `third_party/enet/`, `cmake/ThirdPartyENet.cmake`, `CMakeLists.txt` | ENet + CMake targets |
| sharp-runtime | `System/Runtime/Serialization/SerializationInfo.hpp`, `StreamingContext.hpp` | New stubs only |

---

## 4. Current blocker / main problem

**No hard blocker.** Build is clean and all 1830 tests pass.

The natural next step is completing the main gamer class hierarchy before moving to the Net layer.
`Gamer` exists as a minimal stub — it compiles but lacks the full XNA API surface (`LeaderboardWriter`
property, `GamerAction` async pattern, `Dispose()`, `GetHashCode()`/`ToString()`). `SignedInGamer`
does not exist yet, blocking completion of `SignedInEventArgs` tests (currently using `nullptr`
stand-ins) and the collections that are typed on `SignedInGamer*`.

---

## 5. Known bugs and limitations

| Status | Issue |
|---|---|
| **Incomplete** | `Gamer` stub is missing: `Dispose()`, `IDisposable` base, `LeaderboardWriter` property, async `GamerAction` inner class, `ToString()`, `GetHashCode()`. |
| **Incomplete** | `SignedInGamer` not yet ported — blocks `SignedInEventArgs`/`SignedOutEventArgs` real tests, full `SignedInGamerCollection` usage. |
| **Incomplete** | `GamerServicesComponent.hpp` and `Guide.hpp` exist as stubs; method bodies not implemented. |
| **Incomplete** | `GamerServicesDispatcher` not yet ported at all. |
| **Incomplete** | Phase 3–7 (Net enums, core Net classes, ENet backend, platform, integration tests) not started. |
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
| XNA public API (Net) | `include/Microsoft/Xna/Framework/Net/` | Not started |
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
→ Gamer (stub) → GamerCollection<T> (done) → FriendGamer / SignedInGamer (partial)
→ FriendCollection / SignedInGamerCollection (done, but use stubs)
→ GamerProfile / LeaderboardEntry / LeaderboardWriter / LeaderboardReader (not started)
→ SignedInGamer (not started)
→ GamerServicesDispatcher / GamerServicesComponent / Guide (stub/incomplete)
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

1. **Task 2.31 — Complete `Gamer` + port `GamerProfile`**
   - Goal: extend `Gamer.hpp/.cpp` with `Dispose()`, `IDisposable`, `LeaderboardWriter` property stub,
     `GetHashCode()`/`ToString()`; then port `GamerProfile.cs` as a derived class.
   - Files: `include/Microsoft/Xna/Framework/GamerServices/Gamer.hpp/.cpp`, new `GamerProfile.hpp/.cpp`.
   - Reference: `/rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/GamerServices/Gamer.cs`, `GamerProfile.cs`.
   - Verification: `cmake --build cmake-build-debug --target CnaTests && cmake-build-debug/CnaTests`.

2. **Task 2.32–2.34 — Port `LeaderboardEntry`, `LeaderboardWriter`, `LeaderboardReader`**
   - Goal: three data/API classes needed before `SignedInGamer` can compile cleanly.
   - Files: 3 new header + cpp pairs in `include/Microsoft/Xna/Framework/GamerServices/`.
   - Reference: the corresponding `.cs` files in `FNA.NetStub/src/GamerServices/`.
   - Verification: build + new tests pass.

3. **Task 2.35 — Port `SignedInGamer`**
   - Goal: the central class tying `GamerPresence`, `GamerPrivileges`, `GameDefaults`, events, and
     `FriendCollection` together; replaces forward-declared stubs in event args and collections.
   - Files: `include/Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp/.cpp`.
   - Reference: `/rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/GamerServices/SignedInGamer.cs`.
   - Verification: update event args tests to use a real `SignedInGamer`; all pass.

4. **Task 2.36–2.40 — Complete `GamerServicesDispatcher`, `GamerServicesComponent`, `Guide`**
   - Goal: static service classes that are the public entry points for GamerServices on the game side.
   - Files: new/extended headers + cpp in `GamerServices/`.
   - Reference: the corresponding `.cs` files.
   - Verification: build clean, smoke tests.

5. **Task 3.1–3.15 — Port all Net enums**
   - Goal: 8 Net enums (`NetworkSessionType`, `NetworkSessionState`, `NetworkSessionEndReason`,
     `NetworkSessionJoinError`, `SendDataOptions`, `QualityOfService` etc.) plus their headers.
   - Files: new headers under `include/Microsoft/Xna/Framework/Net/`.
   - Reference: `/rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/Net/`.
   - Verification: build + enum value tests pass.

---

## 9. Do not do yet

- **No ENet backend implementation** until the full XNA Net API surface is declared — wiring ENet
  to an incomplete API wastes effort.
- **No Phase 8 (Avatar)** work yet — Avatar depends on graphics systems not yet audited, and has
  low priority relative to completing GamerServices/Net.
- **No changes to graphics-layer code** during the Net phase — the graphics phase (31) is healthy
  and should not be disturbed.
- **No modifications to existing sharp-runtime files** — another Claude Code instance may be editing
  them in parallel; only add new files.
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

Current status: Tasks 2.1–2.30 complete (enums, exceptions, data structs, event args,
collections, Gamer stub); 1830/1830 unit tests pass; CNA_GamerServices and CNA_Net targets build.

Next: Task 2.31 — extend the minimal Gamer stub into a full port, then port GamerProfile.
Reference: /rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/GamerServices/Gamer.cs
           /rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/GamerServices/GamerProfile.cs
Build: cmake --build cmake-build-debug --target CnaTests
Run:   cmake-build-debug/CnaTests
Update NEXT.md after finishing.
```
