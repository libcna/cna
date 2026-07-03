# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`),
built on SDL3 with a pluggable graphics backend layer (EasyGL/OpenGL ES 3.2, Vulkan, Bgfx,
SDL_Renderer). It is a framework/runtime, not a game — the goal is to let existing XNA/FNA game
code be ported to C++ with minimal API-surface changes. The authoritative behavioral reference is
the local FNA source tree (`/rv/data/library/github.com/FNA-XNA/FNA`).

- **Main goal:** full XNA 4.0 API coverage with behavior fidelity to FNA, backed by unit tests
  (plus pixel-readback integration tests for graphics).
- **Current phase:** Phase 6 — platform-specific work for `Microsoft::Xna::Framework::Net`.
  - Phase 5 (real ENet networking backend for `Net`) is **complete**.
  - Task 6.1 (Linux two-process real ENet loopback test) is **complete, committed, pushed**
    (`9c7ce0b` on `feature/net`).
  - Task 6.2 (Windows cross-build + Wine verification) is **in progress, blocked on user
    confirmation** — see section 4.
  - Graphics (Phases 1–31) and `GamerServices` are complete and stable; not touched this phase.
- **Important architectural decisions:**
  - Graphics backend selection is compile-time via `CNA_GRAPHICS_BACKEND`.
  - `CNA_GamerServices` and `CNA_Net` are separate CMake static libraries, excluded from the main
    `CNA` GLOB so they don't contaminate the graphics-only build.
  - `GamerServices`/`Net`/`Avatar` are **not** binary-compatible with Xbox Live — they reimplement
    the XNA API shape, backed by ENet (reliable UDP) instead of Xbox Live for real networking.
  - Phase 5's ENet backend has **no abstract `INetworkBackend` interface** (unlike the graphics
    backends) — ENet is the only implementation, so an interface would be speculative abstraction.
    Instead: a static-class facade `CNA::Internal::Net::ENetBackend` keeps ENet's C API entirely
    out of `Microsoft::Xna::Framework::Net` public headers.
  - `sharp-runtime` (sibling repo, `../sharp-runtime/`) supplies all `System.*` types. It is
    maintained by a separate parallel session with no version pin from this repo — only add new
    files there; don't modify existing files without a build-break-level reason, and never
    commit/push changes there without asking the user first, even for approved fixes.

---

## 2. Current status

### Build
- **Native Linux** (`cmake-build-debug`, target `CnaTests`): clean, **2077/2077 tests passing**,
  stable under `--gtest_shuffle --gtest_repeat=8`.
- **Windows cross-build** (`cmake-build-windows/`, `cmake/toolchains/mingw-w64.cmake`): configured
  and partially built. A real, pre-existing `sharp-runtime` mingw build break was found and fixed
  (uncommitted there — see section 3/4), but the `cna_net` Windows build has **not been re-run**
  since. Unverified whether `CnaTests.exe`/`cna_net_two_process_harness.exe` link and pass under
  Wine.
- `GamerServices` namespace: complete, all classes ported and tested.
- `Net` namespace: complete API surface (5 enums + 18 classes). `SystemLink` sessions do real ENet
  networking (Phase 5); every other `NetworkSessionType` remains a synthetic, non-networked stub.

### Tools/executables available
- `CnaTests` — the main GoogleTest binary (all graphics + GamerServices + Net tests).
- `cna_net_two_process_harness` — standalone executable (`tools/net/net_two_process_harness.cpp`),
  `--role=host`/`--role=client`, used only by `TwoProcessLoopbackTest.cpp` to spawn two independent
  real processes for a genuine cross-process ENet loopback test.

### Recently implemented / working
- Real ENet-backed `SystemLink` networking end to end: hosting, connect/handshake
  (`ClientHello`/`ServerWelcome`/`GamerJoinBroadcast`), `AppData` send/receive relay, disconnect/
  leave handling, `StartGame`/`EndGame` state broadcast, LAN discovery via
  `ENetDiscoveryService` (well-known port 61190).
- Two-process real ENet loopback test (Task 6.1): proves the transport works across independent OS
  processes, not just one process playing both roles (every Phase 5 test's pattern, forced by
  `NetworkSession`'s process-wide `activeSession_` gate).

### What does not work / unverified yet
- Windows cross-build's actual link + Wine test run (`CnaTests.exe`/
  `cna_net_two_process_harness.exe`) — configured, not yet executed with the sharp-runtime fixes in
  place.
- Web (Emscripten) and Android (NDK) targets for `Net` — no SDK installed in this environment;
  not started, not attempted.
- Runtime-added local gamers (`AddLocalGamer()` called after `Create()`) don't get a wire-id
  assigned or announced to already-connected peers — not needed by any task's scope yet.
- Host migration, `SimulatedLatency`/`SimulatedPacketLoss`, cross-machine `IsReady` sync: inert/
  unimplemented (documented as out of scope in the Phase 5 plan).
- `NetworkSession::Find()`'s full public path (`BeginFind`→`EndFind`) can't be end-to-end tested
  with a real hosted session alive in the same process (see section 5).

---

## 3. Recent changes

- **Committed & pushed** (`9c7ce0b`, `feature/net`): Task 6.1 — two-OS-process ENet loopback test.
  `CMakeLists.txt` extended (new `cna_net_two_process_harness` target, guarded
  `CNA_ENABLE_NET AND CNA_BUILD_TESTS`); `tools/net/net_two_process_harness.cpp` (new); new
  `tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp`. Passed first try, stable across 10 repeats.
  Does not touch `sharp-runtime`.
- **sharp-runtime (sibling repo, uncommitted, not pushed):** 4 Windows/mingw-only bug fixes
  required to make the Windows cross-build compile — all pre-existing, never exercised under a
  real Windows/GCC target before:
  - `src/System/Environment.cpp`: `SetEnvironmentVariable`/`SetCurrentDirectory` collide with
    `<windows.h>` A/W-suffix macros; missing `#include <shlobj.h>` for `SHGetFolderPathA`; an
    MSVC-only `#pragma warning(suppress: 4996)` that GCC's `-Werror=unknown-pragmas` rejects.
  - `src/System/Net/Sockets/{NetworkStream,TcpClient,UdpClient}.cpp`: same MSVC-only-pragma issue
    (`#pragma comment(lib, "ws2_32.lib")` — redundant under GCC, `ws2_32` is already linked at the
    CMake level).
  - All changes confined to `#if defined(_WIN32)`/`#if defined(_MSC_VER)` guards. Verified via
    sharp-runtime's own Linux test suite twice: **8467/8467 passing both times**.
  - **Not committed or pushed** — needs explicit user go-ahead in that repo before committing.
- **Discovered, not yet fixed:** `.sdl-prebuilt/` (SDL3 build cache, see `cmake/ThirdPartySDL.cmake`)
  is keyed only by source checkout path, not by target platform — configuring the Windows
  cross-build silently overwrote the same directory the native Linux build's cached `SDL3::SDL3`
  target pointed at, breaking the native build with `multiple definition of ...` linker errors
  mentioning Windows `.dll` files. Recovered via `rm -rf .sdl-prebuilt` + native rebuild (confirmed
  2077/2077 restored). See section 5 for the durable fix.

---

## 4. Current blocker / main problem

**Task 6.2 (Windows cross-build + Wine verification) is blocked pending user confirmation — not a
technical dead end.**

- **Symptom:** no command is currently failing. The last real attempt stopped after fixing
  `sharp-runtime`'s mingw build break (section 3), before the `cna_net` Windows build was re-run
  with those fixes in place.
- **Last failing command** (believed fixed, not yet re-verified):
  ```bash
  cmake --build cmake-build-windows --target CnaTests --target cna_net_two_process_harness -j"$(nproc)"
  ```
  failed while compiling `sharp-runtime` object files (WinAPI macro collisions / MSVC-only pragmas
  under this project's `-Werror`).
- **Affected modules:** `sharp-runtime`'s `System::Environment` and `System::Net::Sockets` (Windows
  code paths only) — **not** `cna_net`/`Net` code itself.
- **Suspected cause:** those sharp-runtime files were written and previously only compiled under
  MSVC; this was the first time they were built with mingw-w64 GCC.
- **What's already been tried:** the 4 fixes described in section 3, verified safe on Linux
  (sharp-runtime's own suite, 8467/8467, twice, user-approved both times). The actual re-run of the
  Windows build with those fixes in the tree has **not** happened — two `AskUserQuestion` prompts
  asking to proceed timed out unanswered (60s each, not declined). Per this project's convention,
  that is "not yet decided," not "permanently blocked" — do not keep retrying without a fresh ask.
- **To resume:**
  1. Confirm with the user first — `sharp-runtime`'s working tree still has uncommitted changes.
  2. Before rebuilding for Windows, expect the `.sdl-prebuilt` collision (section 5) to happen
     again — either accept it and `rm -rf .sdl-prebuilt` + rebuild `cmake-build-debug` natively
     afterward, or fix `CNA_SDL_PREBUILT_ROOT` to be platform-keyed first (see section 8, task 5).
  3. `cd /rv/data/development/github.com/openeggbert/cna_net && cmake --build cmake-build-windows --target CnaTests --target cna_net_two_process_harness -j"$(nproc)"`.
  4. If it succeeds: `wine cmake-build-windows/CnaTests.exe --gtest_filter="*Network*:*Gamer*:*ENet*:*Packet*"` and record the pass/fail count here.

---

## 5. Known bugs and limitations

| Status | Issue |
|---|---|
| **Blocked on user input** | Task 6.2 Windows build not yet re-attempted since sharp-runtime fixes landed — see section 4. |
| **Real infra bug, not yet fixed** | `.sdl-prebuilt/` (`cmake/ThirdPartySDL.cmake`, `CNA_SDL_PREBUILT_ROOT`) is keyed only by `CMAKE_CURRENT_SOURCE_DIR`, not by target platform — a Windows cross-build silently overwrites the native Linux build's cached SDL3. Recovery: `rm -rf .sdl-prebuilt` + rebuild natively. Durable fix: append `${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}` to the cache root. |
| **Design constraint (not a bug)** | `NetworkSession::BeginCreate`/`BeginFind` gate on a single **process-wide** `activeSession_` static — only one real `NetworkSession` can exist per OS process at a time. Loopback tests within one process must use one real `NetworkSession` + a raw `ENetHostHandle` peer stand-in (see `ENetBackendTests.cpp`'s `SystemLinkSessionFixture`); Task 6.1's two-process test sidesteps this by using two real *processes* instead. |
| **Confirmed bug (upstream FNA, preserved)** | `NetworkSession::EndCreate`/`EndJoin`/`EndJoinInvited` null the static `activeAction` **after** constructing `NetworkSession`, so a constructor throw strands it non-null for the rest of the process (no public API to reset it). FNA has the identical ordering; not something to fix. |
| **Confirmed bug (upstream FNA, preserved)** | `NetworkSession::EndJoin`/`EndJoinInvited` hardcode the resulting session's type to `NetworkSessionType::PlayerMatch` regardless of what was actually joined — so a session built via the public `Join()` never does real networking (`RealNetworkingEnabled(PlayerMatch)` is `false`). Tests/harnesses use `Create()` + `ENetBackend::ConnectToHost()` on both sides instead. |
| **Confirmed bug (upstream FNA, preserved)** | `NetworkGamer::getIsHostProperty()` always returns `true` (FNA's own stub) — it cannot distinguish "real ENet host" from "real ENet client." Anything needing to know the real host checks `ENetBackend`/`SessionState` internals (`HostPeer == nullptr`) instead. |
| **Design constraint (not a bug)** | `NetworkSession::Find()`'s full public path can't be end-to-end tested with a real hosted session alive in the same process (`BeginFind` throws immediately). `ENetDiscoveryService::FindSessions()` is tested directly instead; `EndFind`'s delegation to it is verified by inspection only. |
| **Note (not a bug)** | `ENetDiscoveryService`'s discovery port (61190) is a fixed, hardcoded value — an unrelated concurrent process binding it on this shared dev machine is a narrow, accepted risk. |
| **Deviation (documented)** | `PacketWriter::Write(Color)` writes 4 bytes but `PacketReader::ReadColor()` reads 4 floats (16 bytes) — asymmetric upstream, preserved as-is. |
| **Deviation (documented)** | `PacketReader(int capacity)`/`PacketWriter(int capacity)` discard the `capacity` argument — no observable effect in .NET either. |
| **Deviation (documented)** | `NetworkSession::BeginJoin`/`BeginJoinInvited`/`EndJoin`/`EndJoinInvited` substitute a default-constructed `NetworkSessionProperties` for FNA's `null` (this port's type isn't nullable). |
| **Deviation (documented)** | `LocalNetworkGamer::ReceiveData(PacketReader&, ...)` always returns 0 — FNA declares a length variable it never updates. Preserved as-is. |
| **Incomplete** | `GamerJoinedEventArgs`/`GamerLeftEventArgs`/`HostChangedEventArgs`/`WriteLeaderboardsEventArgs` tests still use `nullptr` stand-ins for `NetworkGamer*` instead of real instances. |
| **Confirmed bug (graphics)** | `SpriteBatch` multiple `Begin()`/`End()` per frame on Vulkan: only the last batch renders. |
| **Suspected bug (graphics)** | `DrawUserIndexedPrimitives` typed overloads likely have the silent-return-on-missing-effect bug (not yet audited — Task 252). |
| **Note (not a bug)** | `plan_net.md`'s own Phase 5 checklist describes a different, more elaborate design (abstract `INetworkBackend`, host migration, relay server, QoS simulation) that was deliberately not built. Its checkboxes were never kept live — don't infer project status from it; `NEXT.md` is the maintained source of truth. |

---

## 6. Architecture notes

### Module map

| Layer | Location | Notes |
|---|---|---|
| XNA public API (graphics) | `include/Microsoft/Xna/Framework/…` | Must match XNA 4.0 / FNA exactly |
| XNA public API (GamerServices) | `include/Microsoft/Xna/Framework/GamerServices/` | Complete. Internal ctors → `private` + `CreateInternal()` factory |
| XNA public API (Net) | `include/Microsoft/Xna/Framework/Net/` | Complete API surface (5 enums + 18 classes); fully wired to real networking for `SystemLink` — **public shapes here are a fixed point, must not change** |
| ENet backend (Phase 5, complete) | `include/CNA/Internal/Net/`, `src/CNA/Internal/Net/` | `ENetLibrary`/`ENetHostHandle`, `NetPacketCodec`/`NetDiscoveryProtocol`, `ENetBackend` registry/wiring, connect+handshake, `AppData` relay, disconnect/leave, state broadcast, `ENetDiscoveryService` |
| Two-process test harness (Task 6.1) | `tools/net/net_two_process_harness.cpp`, `tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp` | Standalone executable + GTest orchestrator, outside `tests/`'s glob to avoid a second `main()` |
| Backend contracts (graphics only) | `include/CNA/Internal/Backends/Common/` | `IGraphicsBackend`, etc. — not the pattern used for networking |
| CNA utilities | `include/CNA/`, `src/CNA/` | NOXNA helpers, logging |
| sharp-runtime | `../sharp-runtime/` (sibling repo) | `System.*` types; only add new files; no version pin from this repo |

### Key invariants

- **`NOXNA` macro** tags every non-XNA extension in public headers under `Microsoft::Xna::…`. Does
  not apply to `CNA::Internal::Net` — that's already outside the XNA namespace.
- **C# `internal` constructors** → `private` in C++, exposed via `NOXNA static CreateInternal(…)`.
- **C# properties** → `getXProperty()`/`setXProperty()`. Public-field style is never used to
  shortcut this, except where FNA/XNA itself already uses public fields (`Vector2`/`Matrix`/etc.).
- **`System::Exception`** is the base for all GamerServices/Net exceptions, never
  `std::runtime_error`.
- **CNA-internal ENet code lives entirely under `CNA::Internal::Net`**, never
  `Microsoft::Xna::Framework::Net` — `enet/enet.h` must not leak into any public XNA-facing header.
  `ENetBackend`'s per-session state (`SessionState`) is a private `.cpp`-only struct keyed by
  `NetworkSession*` in a function-local static registry.
- **Only one real `NetworkSession` can exist per OS process at a time** (`activeSession_` static
  gate). See section 5.
- **`ENetDiscoveryService`'s discovery port is `61190`**, hardcoded; its raw UDP socket is
  process-wide, lazily created, never torn down.
- **Template headers** (e.g. `GamerCollection.hpp`) contain full implementation, no `.cpp`.
- **SPDX headers:** `MS-PL` for files ported from FNA; `MIT` + `Copyright (c) Robert Vokac and
  contributors` for original CNA-internal code with no FNA equivalent (Phase 5/6's new files).
- **Doxygen** `/** @brief … @param … @return */` required on every public member in every `.hpp`.
- sharp-runtime: only add new files; modifying existing ones is a last resort requiring explicit
  user go-ahead each time, not a routine option.

---

## 7. Useful commands

```bash
# Working directory
cd /rv/data/development/github.com/openeggbert/cna_net

# Native Linux build (all targets: CNA, CNA_GamerServices, CNA_Net, CnaTests)
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"

# Run all tests
cmake-build-debug/CnaTests

# Run just Net/GamerServices/Phase-5/6 tests
cmake-build-debug/CnaTests --gtest_filter="*Network*:*Gamer*:*ENet*:*Packet*"

# Windows cross-build (configure once, see mingw-w64.cmake for details)
cmake -B cmake-build-windows \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_BACKEND=SDL_RENDERER \
      -DCNA_ENABLE_NET=ON
cmake --build cmake-build-windows --target CnaTests --target cna_net_two_process_harness -j"$(nproc)"
wine cmake-build-windows/CnaTests.exe --gtest_filter="*Network*:*Gamer*:*ENet*:*Packet*"

# Recover native build if a Windows cross-build clobbered the SDL3 cache (see section 5)
rm -rf .sdl-prebuilt
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"

# FNA reference source
ls /rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/GamerServices/
ls /rv/data/library/github.com/FNA-XNA/FNA.NetStub/src/Net/

# sharp-runtime (sibling repo) — check its status before touching it
cd ../sharp-runtime && git status
```

Builds can time out on this shared machine if another session is compiling concurrently — retry
with a reduced `-j` and a longer timeout rather than assuming a real compile error; check
`pgrep -fl cc1plus` before concluding a build is stuck.

---

## 8. Next smallest tasks

1. **Get user go-ahead, then re-attempt the Windows build.**
   Goal: verify `CnaTests`/`cna_net_two_process_harness` actually build and pass under Wine with
   the sharp-runtime fixes in place. Files: none new — just re-run the build.
   Verify: `cmake --build cmake-build-windows --target CnaTests --target cna_net_two_process_harness -j"$(nproc)"`
   succeeds, then `wine cmake-build-windows/CnaTests.exe --gtest_filter="*Network*:*Gamer*:*ENet*:*Packet*"`
   passes.

2. **Restore native build health if step 1 clobbers `.sdl-prebuilt` again.**
   Goal: keep `cmake-build-debug` green.
   Files: none (cache directory only).
   Verify: `rm -rf .sdl-prebuilt && cmake --build cmake-build-debug --target CnaTests` → 2077/2077.

3. **Fix `.sdl-prebuilt` platform-cache collision permanently.**
   Goal: make `CNA_SDL_PREBUILT_ROOT` platform-specific so native and cross builds stop
   overwriting each other's SDL3.
   Files: `cmake/ThirdPartySDL.cmake` (append `${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}` to
   the cache root path).
   Verify: build `cmake-build-debug` then `cmake-build-windows` back-to-back; both stay green.

4. **Decide (with the user) whether to commit the 4 sharp-runtime Windows fixes.**
   Goal: land `Environment.cpp`/`NetworkStream.cpp`/`TcpClient.cpp`/`UdpClient.cpp` fixes in the
   sibling repo, or leave them local if the parallel session maintaining it should own that.
   Files: `../sharp-runtime/src/System/Environment.cpp`,
   `../sharp-runtime/src/System/Net/Sockets/{NetworkStream,TcpClient,UdpClient}.cpp`.
   Verify: sharp-runtime's own test suite still 8467/8467 on Linux before committing.

5. **Once Task 6.2 is fully verified, update this file and decide on Phase 6.3/6.4/7/8.**
   Goal: document the Windows verification outcome; ask the user whether to pursue Web/Android
   (needs SDKs not present here), integration tests, or Avatar next — none is an assumed default.
   Files: `NEXT.md` only.
   Verify: N/A (documentation task).

---

## 9. Do not do yet

- No changes to `Microsoft::Xna::Framework::Net` public class shapes, method signatures, or
  property names — the entire Net API surface is a fixed point.
- No `INetworkBackend` abstract interface — ENet is the only implementation; an interface would be
  speculative abstraction.
- No "fixing" the FNA-preserved `activeAction`/`EndJoin`-hardcodes-`PlayerMatch`/
  `getIsHostProperty()` bugs described in section 5 — they are faithfully-preserved upstream FNA
  behavior, not defects to correct.
- No test that constructs two real `NetworkSession` instances in one process, and no test that
  calls the public `NetworkSession::Find()`/`BeginFind()` while a real hosted session is alive in
  the same process — both throw via the `activeSession_` gate.
- No `enet.h` includes in any `Microsoft::Xna::Framework::Net` header.
- No changes to graphics-layer code beyond genuine build-break emergencies.
- No modifications to existing `sharp-runtime` files without a build-break-level reason, and never
  commit/push changes there without asking the user first — even for already-approved fixes.
- No starting Phase 6.3 (Web/Emscripten) or 6.4 (Android/NDK) — no SDK available in this
  environment; would produce unverifiable code.
- No unilaterally starting Phase 7 (integration tests) or Phase 8 (Avatar) — ask the user first,
  see section 8.

---

## 10. Resume prompt

```
Read NEXT.md first, in full, before doing anything else. Phase 5 (ENet networking backend for
Microsoft::Xna::Framework::Net) is COMPLETE — do not re-open it. Phase 6 Task 6.1 (Linux
two-process real ENet loopback test) is COMPLETE, committed and pushed (9c7ce0b). Task 6.2
(Windows cross-build + Wine verification) is BLOCKED ON USER CONFIRMATION, not a technical dead
end — see section 4 for the exact state and section 8 for the ordered next steps.

Before doing anything else:
1. Run `cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"` then `cmake-build-debug/CnaTests`
   to confirm the native Linux build is still healthy (expect 2077/2077). This does not depend on
   the paused Windows work.
2. Run `cd ../sharp-runtime && git status` — if Environment.cpp and
   Net/Sockets/{NetworkStream,TcpClient,UdpClient}.cpp show modified, that is the known,
   already-verified-safe, uncommitted state described in section 3 — do not redo that work, and do
   not commit/push it without asking the user explicitly first.

Then: ask the user whether to proceed with Task 6.2 (section 8, tasks 1–2) given sharp-runtime's
uncommitted state. Do not assume Phase 6.3/6.4/7/8 — see section 9.

Make one small, verified improvement at a time; do not refactor unrelated code. After finishing,
update NEXT.md to reflect the new state.

Build: cmake --build cmake-build-debug --target CnaTests
Test:  cmake-build-debug/CnaTests
```
