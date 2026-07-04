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
  - Task 6.2 (Windows cross-build + Wine verification) is **complete, verified, not yet
    committed** — see section 3/4.
  - Graphics (Phases 1–31) and `GamerServices` are complete and stable; not touched this phase
    beyond one real latent-bug fix surfaced by the Windows build (section 3).
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
- **Native Linux** (`cmake-build-debug`, target `CnaTests`): clean, **2077/2077 tests passing**.
- **Windows cross-build** (`cmake-build-windows/`, `cmake/toolchains/mingw-w64.cmake`): clean,
  **both `CnaTests.exe` and `cna_net_two_process_harness.exe` build and pass under Wine**
  (**2076/2076** — one fewer than native because `TwoProcessLoopbackTest.cpp` is POSIX-only and is
  excluded from the Windows build; see section 3).
- `GamerServices` namespace: complete, all classes ported and tested.
- `Net` namespace: complete API surface (5 enums + 18 classes). `SystemLink` sessions do real ENet
  networking (Phase 5); every other `NetworkSessionType` remains a synthetic, non-networked stub.

### Tools/executables available
- `CnaTests` — the main GoogleTest binary (all graphics + GamerServices + Net tests).
- `cna_net_two_process_harness` — standalone executable (`tools/net/net_two_process_harness.cpp`),
  `--role=host`/`--role=client`, used only by `TwoProcessLoopbackTest.cpp` to spawn two independent
  real processes for a genuine cross-process ENet loopback test. Builds on Windows too, but the
  orchestrator test that spawns it is POSIX-only (see section 3) so it isn't exercised there.

### Recently implemented / working
- Real ENet-backed `SystemLink` networking end to end: hosting, connect/handshake
  (`ClientHello`/`ServerWelcome`/`GamerJoinBroadcast`), `AppData` send/receive relay, disconnect/
  leave handling, `StartGame`/`EndGame` state broadcast, LAN discovery via
  `ENetDiscoveryService` (well-known port 61190).
- Two-process real ENet loopback test (Task 6.1): proves the transport works across independent OS
  processes, not just one process playing both roles (every Phase 5 test's pattern, forced by
  `NetworkSession`'s process-wide `activeSession_` gate).
- Windows cross-build (Task 6.2): fully green, native and cross builds now coexist without clobbering
  each other's SDL3 cache (see section 3).

### What does not work / unverified yet
- Web (Emscripten) and Android (NDK) targets for `Net` — no SDK installed in this environment;
  not started, not attempted.
- FFmpeg-backed video decoding (`VideoDecoder`/`VideoPlayer`/`Video`) is unavailable on the Windows
  cross-build — no mingw-w64 FFmpeg dev packages in this environment (see section 3). Native Linux
  and (previously) Emscripten/Android are unaffected; no test coverage depends on video on Windows.
- Runtime-added local gamers (`AddLocalGamer()` called after `Create()`) don't get a wire-id
  assigned or announced to already-connected peers — not needed by any task's scope yet.
- Host migration, `SimulatedLatency`/`SimulatedPacketLoss`, cross-machine `IsReady` sync: inert/
  unimplemented (documented as out of scope in the Phase 5 plan).
- `NetworkSession::Find()`'s full public path (`BeginFind`→`EndFind`) can't be end-to-end tested
  with a real hosted session alive in the same process (see section 5).

---

## 3. Recent changes

- **Committed & pushed** (`9c7ce0b`, `feature/net`): Task 6.1 — two-OS-process ENet loopback test.
  Does not touch `sharp-runtime`.
- **Not yet committed (this session) — Task 6.2, Windows cross-build + Wine verification, now
  fully green.** Six real, previously-latent issues were found and fixed along the way — this was
  the first time this codebase was actually built and run on a non-Linux target, so none of these
  had ever been exercised before:
  1. **`CMakeLists.txt`** — `pkg_check_modules(... REQUIRED libavcodec ...)` ran unconditionally
     for every non-Emscripten/Android build, including the mingw cross-build. With no mingw-w64
     FFmpeg dev packages installed, pkg-config silently resolved to the **host's native** FFmpeg
     `.pc` files and injected `-I/usr/include/x86_64-linux-gnu` into the cross-compile, which
     collided with mingw's own headers (`fatal error: features.h: No such file or directory`).
     Fixed by introducing `CNA_FFMPEG_AVAILABLE` (OFF when `MINGW`) and reusing the existing
     Emscripten/Android exclusion pattern for `VideoDecoder.cpp`/`VideoPlayer.cpp`/`Video.cpp`. No
     test depends on these on Windows.
  2. **`CMakeLists.txt`** — `CNA_GamerServices` never declared a dependency on SDL3, even though
     `Guide.cpp` includes `<SDL3/SDL.h>` directly. It only ever compiled on this machine because a
     stray system-wide `/usr/local/include/SDL3` happens to be on the default include path. Fixed
     by linking `SDL3::SDL3` `PRIVATE` to `CNA_GamerServices`.
  3. **`src/CNA/Internal/Net/ENetDiscoveryService.cpp`** — two call sites built `ENetBuffer` via
     positional aggregate init (`{data, size}`). ENet's `ENetBuffer` struct has **opposite member
     order** on win32 (`dataLength` then `data`) vs. unix (`data` then `dataLength`)
     (`third_party/enet/include/enet/{win32,unix}.h`), so the positional init silently swapped the
     pointer and length fields under mingw. Fixed with field-by-field assignment (order-independent).
  4. **`tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp`** — uses POSIX-only process APIs
     (`posix_spawn`, `poll`, `sys/wait.h`) to orchestrate two real OS processes; not portable to
     mingw. Excluded from the Windows `CnaTests` build via a `CMakeLists.txt` glob filter (`WIN32`).
     Not a loss for Task 6.2's own verification scope — its suite name (`TwoProcessLoopbackTest`)
     was never part of the `*Network*:*Gamer*:*ENet*:*Packet*` filter anyway.
  5. **`CMakeLists.txt`** — mingw-w64's `<cmath>` only exposes `M_PI` when `_USE_MATH_DEFINES` is
     defined (unlike glibc, which always defines it); three test files
     (`MathHelperTests.cpp`/`QuaternionTests.cpp`/`MatrixTests.cpp`) use `M_PI` as a reference
     value. Fixed with a `MINGW`-guarded `target_compile_definitions(CnaTests PRIVATE
     _USE_MATH_DEFINES)`. No library/production code uses `M_PI`.
  6. **`CMakeLists.txt`** — a known mingw-w64/GCC PE-COFF toolchain limitation: `CnaTests.exe`
     failed to link with `multiple definition of 'std::type_info::operator=='` between
     `libstdc++.a` and one of the test object files. Both definitions are byte-identical (the
     linker just fails to fold the COMDAT on this target); worked around with a `MINGW`-guarded
     `target_link_options(CnaTests PRIVATE -Wl,--allow-multiple-definition)`.
  - **Also fixed (test-only, not a production bug):** 3 `TitleContainerTest` cases
    (`OpenStreamRelativeNameReadsContent`/`OpenStreamAbsolutePathReadsContent`/
    `OpenStreamNormalizesBackslashes`) kept the `unique_ptr<Stream>` returned by `OpenStream(...)`
    open while calling `std::filesystem::remove_all()` on the temp directory. POSIX allows deleting
    a file with an open handle; Windows doesn't (without `FILE_SHARE_DELETE`), so this only failed
    under Wine. Fixed by resetting the stream before cleanup in all three tests.
  - **Durable fix for the `.sdl-prebuilt` collision** (previously "discovered, not yet fixed"; see
    section 5's old entry): `cmake/ThirdPartySDL.cmake`'s default `CNA_SDL_PREBUILT_ROOT` now
    appends `${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}` to the cache root (except the
    Emscripten branch, which already had its own suffix). `.gitignore` updated to
    `.sdl-prebuilt-*/`. Verified: `cmake-build-debug` (pre-existing cache, still plain
    `.sdl-prebuilt` since CMake cache variables don't pick up new defaults once already configured)
    and a freshly-configured `cmake-build-windows` (`.sdl-prebuilt-Windows-x86_64`) now coexist —
    both stayed green rebuilt back-to-back.
  - None of the above touch `Microsoft::Xna::Framework::Net`'s public API shapes, and none touch
    `sharp-runtime`.
- **sharp-runtime (sibling repo, still uncommitted, not pushed — untouched this session):** 4
  Windows/mingw-only bug fixes from the prior session, still needed to make the Windows cross-build
  compile at all:
  - `src/System/Environment.cpp`: `SetEnvironmentVariable`/`SetCurrentDirectory` collide with
    `<windows.h>` A/W-suffix macros; missing `#include <shlobj.h>` for `SHGetFolderPathA`; an
    MSVC-only `#pragma warning(suppress: 4996)` that GCC's `-Werror=unknown-pragmas` rejects.
  - `src/System/Net/Sockets/{NetworkStream,TcpClient,UdpClient}.cpp`: same MSVC-only-pragma issue
    (`#pragma comment(lib, "ws2_32.lib")` — redundant under GCC, `ws2_32` is already linked at the
    CMake level).
  - All changes confined to `#if defined(_WIN32)`/`#if defined(_MSC_VER)` guards. Verified via
    sharp-runtime's own Linux test suite twice: **8467/8467 passing both times** (prior session).
  - **Still not committed or pushed** — needs explicit user go-ahead in that repo before committing
    (see section 8).

---

## 4. Current blocker / main problem

**None — Task 6.2 is complete and fully verified.** The only open decision points are:

1. Whether to commit the 4 sharp-runtime Windows fixes in the sibling repo (still uncommitted,
   still needs explicit user go-ahead — see section 8, task 1).
2. Whether to commit/push this session's `cna_net` changes (section 3) to `feature/net`.
3. What to work on next: Phase 6.3 (Web/Emscripten), 6.4 (Android/NDK), Phase 7 (integration
   tests), or Phase 8 (Avatar) — none is an assumed default (see section 8, task 3, and section 9).

---

## 5. Known bugs and limitations

| Status | Issue |
|---|---|
| **Fixed this session** | `.sdl-prebuilt/` platform-cache collision — see section 3 for the durable fix (`cmake/ThirdPartySDL.cmake`). |
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
| **Platform limitation (Windows cross-build only)** | FFmpeg-backed video decoding is unavailable on the mingw-w64 Windows cross-build — no mingw-w64 FFmpeg dev packages in this environment. `VideoDecoder.cpp`/`VideoPlayer.cpp`/`Video.cpp` are excluded from that build only (same pattern as Emscripten/Android). No test coverage depends on it. |
| **Platform limitation (Windows cross-build only)** | `TwoProcessLoopbackTest.cpp` (Task 6.1) is excluded from the Windows `CnaTests` build — it uses POSIX-only process APIs (`posix_spawn`/`poll`/`sys/wait.h`) that don't exist under mingw. |
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
| Two-process test harness (Task 6.1) | `tools/net/net_two_process_harness.cpp`, `tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp` | Standalone executable + GTest orchestrator, outside `tests/`'s glob to avoid a second `main()`; test file is POSIX-only, excluded on Windows (section 5) |
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
- **`ENetBuffer`'s member order is platform-dependent** (win32.h: `dataLength` then `data`; unix.h:
  `data` then `dataLength`) — always assign fields by name, never positional-initialize.
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
- **`CNA_SDL_PREBUILT_ROOT` is now platform-keyed** (`.sdl-prebuilt-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}`)
  — native and cross builds no longer clobber each other's cached SDL3 install.
- **FFmpeg is gated by `CNA_FFMPEG_AVAILABLE`** (OFF when `MINGW`) — don't assume video decoding
  works on every non-Emscripten/Android target; check that variable, not just `EMSCRIPTEN`/`ANDROID`.

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
# Full suite also passes: wine cmake-build-windows/CnaTests.exe  →  2076/2076

# Recover native build if a Windows cross-build ever clobbers the SDL3 cache again
# (should no longer happen now that the cache root is platform-keyed — see section 3/6)
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

1. **Decide (with the user) whether to commit the 4 sharp-runtime Windows fixes.**
   Goal: land `Environment.cpp`/`NetworkStream.cpp`/`TcpClient.cpp`/`UdpClient.cpp` fixes in the
   sibling repo, or leave them local if the parallel session maintaining it should own that.
   Files: `../sharp-runtime/src/System/Environment.cpp`,
   `../sharp-runtime/src/System/Net/Sockets/{NetworkStream,TcpClient,UdpClient}.cpp`.
   Verify: sharp-runtime's own test suite still 8467/8467 on Linux before committing (already
   verified twice in a prior session; re-verify if more time has passed / more changes landed
   there).

2. **Decide (with the user) whether to commit this session's `cna_net` changes to `feature/net`.**
   Goal: land the Task 6.2 fixes (section 3) — `cmake/ThirdPartySDL.cmake`, `.gitignore`,
   `CMakeLists.txt`, `src/CNA/Internal/Net/ENetDiscoveryService.cpp`,
   `tests/Microsoft/Xna/Framework/TitleContainerTests.cpp`.
   Verify: both `cmake-build-debug/CnaTests` (2077/2077) and
   `wine cmake-build-windows/CnaTests.exe` (2076/2076) pass before committing.

3. **Once section 8 tasks 1–2 are resolved, decide on Phase 6.3/6.4/7/8.**
   Goal: ask the user whether to pursue Web/Android (needs SDKs not present here), integration
   tests, or Avatar next — none is an assumed default.
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
- No attempting to install/build a mingw-w64 FFmpeg toolchain to restore Windows video decoding
  unless the user asks for it — out of scope for Task 6.2, which was about `Net`/Wine verification.

---

## 10. Resume prompt

```
Read NEXT.md first, in full, before doing anything else. Phase 5 (ENet networking backend for
Microsoft::Xna::Framework::Net) is COMPLETE. Phase 6 Task 6.1 (Linux two-process real ENet
loopback test) is COMPLETE, committed and pushed (9c7ce0b). Task 6.2 (Windows cross-build + Wine
verification) is COMPLETE AND VERIFIED this session (2076/2076 under Wine, 2077/2077 native Linux)
but NOT YET COMMITTED — see section 3 for the six real bugs found and fixed along the way, and
section 8 for the ordered next steps (mainly: decide with the user whether to commit).

Before doing anything else:
1. Run `cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"` then `cmake-build-debug/CnaTests`
   to confirm the native Linux build is still healthy (expect 2077/2077).
2. Run `git status` in this repo — expect the Task 6.2 fixes (section 3) still uncommitted on
   `feature/net`. Run `cd ../sharp-runtime && git status` — expect the 4 Windows fixes still
   uncommitted there too (from a prior session). Neither should be committed/pushed without asking
   the user explicitly first.

Then: ask the user whether to commit (a) the sharp-runtime fixes, (b) this session's cna_net
fixes, and what to work on next (section 8, task 3; section 9 lists what NOT to assume).

Make one small, verified improvement at a time; do not refactor unrelated code. After finishing,
update NEXT.md to reflect the new state.

Build: cmake --build cmake-build-debug --target CnaTests
Test:  cmake-build-debug/CnaTests
```
