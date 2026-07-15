# CNA

## 1. 🚀 Overview

CNA is a C++ reimplementation of the XNA 4.0 programming model, built on SDL3 and a pluggable graphics backend layer.

It is a framework/runtime and abstraction layer—not a game—designed to preserve XNA-style APIs (`Microsoft::Xna::Framework`) while using modern C++ internals.

**CNA demonstrates engine-level C++ architecture, graphics abstraction design, and backend-oriented systems engineering.**

### Quick Start

```bash
git submodule update --init --recursive
cmake -S . -B build -DCNA_GRAPHICS_BACKEND=EASYGL
cmake --build build --target CNA CnaTests
ctest --test-dir build --output-on-failure
```

> **Looking for a specific doc?** `docs/` has 57 files — see [`docs/README.md`](docs/README.md)
> for an index of what's current vs. historical.

### Project Status

- **`Microsoft::Xna::Framework::Graphics` milestone:** qualified **~90% XNA/FNA compatibility, test-execution-verified** (not estimated) — every one of the ~26 major Graphics classes is present, implemented, and tested. **As of 2026-07-11, all 5 confirmed bugs behind the original 2026-07-09 milestone declaration are fixed** (Vulkan `BlendState`, EasyGL anisotropic filtering, `IndexElementSize`'s numeric values, `Model`'s root-bone override, `SpriteBatch::Draw`'s optional source rectangle), and Vulkan `OcclusionQuery` (previously architecturally blocked) is fixed too. `docs/graphics-compatibility-report.md` is a dated snapshot from that declaration, kept for its methodology, not current status — see `NEXT.md` §5 for the actively-maintained bug list. What's left to 100% is a smaller set of individually-tracked issues plus a handful of project-owner architecture decisions (e.g. SDL_Renderer `TextureAddressMode::Wrap`/`Mirror`, `Texture3D`/`TextureCube` sampler-bind architecture) — none silent or undocumented.
- **Overall XNA 4.0 API surface:** 227 of 245 public FNA types are present in CNA (**92.7%**, computed 2026-07-11 by diffing FNA's public type list against CNA's headers) — 100% for `Graphics`/`Audio`/`Input`(+`Touch`)/`Storage`; the real gap is `.Content` (4/12 — no `.xnb` reader, by design) and `.Media` (25/25 present, but 14 are shells). See `docs/xna-4-api-coverage.md`. **Note this is a different metric from the Graphics bullet above** — this one counts whether a type/class exists at all across every XNA namespace (a raw presence count), while the Graphics "~90%" figure is a narrower, bug-weighted quality gate scoped to just the ~26 major Graphics classes (it also counts behavioral correctness, not just presence — Graphics itself is 91/91 = 100% present). The two numbers measuring different things is expected, not a typo or a contradiction.
- **The two gaps that matter most to a real port:** no `.xnb` content pipeline (CNA loads raw assets + JSON descriptors instead) and no compiled `.fx` shader bytecode support (`Effect(GraphicsDevice&, byte[])`) — the latter is the single biggest real gap, blocking 23 of the 86 official XNA samples in `../cna-samples`. See `docs/migration-guide.md`.
- **`SDL_RENDERER` backend:** Implemented path focused on practical 2D rendering workflows; 2D-only by design (3D calls throw).
- **`EASYGL` backend:** Most mature backend overall — implemented OpenGL-based path through `easy-gl`, full 2D+3D pixel-verified coverage.
- **`VULKAN` backend:** Real, working 3D rendering (all 5 stock effects, render targets, depth/stencil state, `BlendState`, `OcclusionQuery`) — second-most mature backend; the one remaining named gap is an isolated `RasterizerState.DepthBias` sub-case. See `docs/xna-4-api-coverage.md`'s per-backend table for current detail.
- **`BGFX` backend:** Broad 2D+3D functionality, largely pixel-verified parity with EasyGL/Vulkan as of this project's Phase 72 — but not unqualified full parity: known real limitations remain, including a `Depth24Stencil8`-attached `RenderTargetCube` face producing no colour output (Task 952, deferred, root cause not yet found), `DrawIndexedPrimitivesEx` silently discarding `startIndex`/`baseVertex` on a sub-range indexed draw (Task 954), and occlusion-query pixel-count correctness that can't be verified under this project's own sandbox's software GL driver. See `NEXT.md` §5 for the current, complete list.
- **`WEBGPU` backend:** Experimental fifth backend using native `wgpu-native`. The current baseline covers device/surface setup, clear/present, RGBA8 `Texture2D`, vertex/index uploads and WGSL SpriteBatch rendering. It is not yet a 3D-parity replacement for EasyGL/Vulkan/Bgfx; see [`docs/webgpu-backend.md`](docs/webgpu-backend.md) and `plan_webgpu.md`.
- **`D3D11` backend:** Windows-only native Direct3D 11 backend, cross-compiled via MinGW-w64 and verified through Wine+DXVK on a real GPU (6 CTest binaries, 96+ checks) — all 10 stock HLSL shader variants (`BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`), textures/render targets (MRT/MSAA/occlusion queries), state objects, SpriteBatch, and a runtime-`D3DCompile()` custom `ShaderEffect` path are real and pixel-verified. Real-Windows hardware verification (device-lost recovery, WARP fallback, driver-specific parity) is still open. See [`docs/d3d11-backend.md`](docs/d3d11-backend.md) and `plan_dx.md`.
- **`D3D12` backend:** Windows-only native Direct3D 12 backend, cross-compiled via MinGW-w64 and verified through Wine+vkd3d-proton on a real GPU, **off-screen only** (`D3D12_Smoke` CTest, 80/80 checks) — device/queue/heaps/command-lists/fences/barriers/PSOs/root-signatures are real, all 10 stock HLSL shader variants (same DXBC as `D3D11`) and a real `SpriteBatch` are pixel-verified off-screen, and device-removed recovery is real and functionally proven. Swap-chain presentation is a known, real, unresolved gap on this dev loop (genuine Wine/vkd3d-proton `dxgi.dll` architecture mismatch, not a CNA bug); runtime-settable blend/depth-stencil/rasterizer state, per-slot `SamplerState`, render targets, `Texture3D`, occlusion queries, and real-Windows hardware verification are all still open. See [`docs/d3d12-backend.md`](docs/d3d12-backend.md) and `plan_dx.md`.
- **`DX3` backend:** Cross-platform (genuinely — builds and runs via ordinary `/usr/bin/c++`, no MinGW/Wine needed) DirectDraw-shaped 2D backend fronting `../free-direct`, a sibling project's own DirectDraw reimplementation. 2D-only by design, same spirit as `SDL_RENDERER`. All 8 plan phases complete: real device/window bring-up with a CPU-owned "shadow backbuffer" (working around a real `Lock()`-on-primary gap in `free-direct` itself), texture/render-target backends, a CPU `SpriteBatch` compositor (`BltFast` fast path + a from-scratch edge-function rasterizer for everything else), all 4 real `BlendState` presets with genuinely distinct formulas (not one collapsed baseline), bilinear filtering, and real `Wrap`/`Mirror` texture addressing — the latter two are a real capability win over `SDL_RENDERER`, which has `Wrap`/`Mirror` ⛔ BLOCKED. See [`docs/dx3-backend.md`](docs/dx3-backend.md) and `plan_dx3.md`.
- **Verification methodology:** differential testing against a real, running `FNA.dll` reference implementation (`tools/fna-reference/`), disputed behavior settled against genuine XNA 4.0 on a Windows 7 VM, and a compile-time `NOXNA` purity check (a dedicated CMake build option that turns every non-XNA-tagged declaration into a `[[deprecated]]` warning under `-Werror`) — see `CHECKLIST.md`'s "NOXNA markers" section and `CMakeLists.txt`.
- **CI is Linux-only and partial** (see `.github/workflows/`): it currently runs only the `Input` and `Devices`/`Sensors` gtest suites — not the full ~4,370-test unit suite and not the ~490-test GPU pixel-test suite across the 4 established graphics backends; the experimental WebGPU backend does not yet have pixel-test coverage. Everything Graphics-related in this README is verified by running the suites locally and by hand, not by an automated Graphics CI gate; there is currently no Windows, macOS, or Android CI at all (Windows/Android are verified manually, per §7/§9 below).

## 2. 🎯 Goals

- Recreate the XNA developer experience in native C++.
- Provide a native C++ path for teams that like the XNA/MonoGame model but need non-managed runtime/toolchain control.
- Mirror core XNA namespaces and API patterns while implementing them incrementally.
- Decouple gameplay-facing API from rendering backend implementation details.
- Enable one high-level API surface across different rendering technologies.
- Keep SDL/OpenGL/Vulkan-level concerns behind framework abstractions.

## 3. ✨ Features

### XNA API Compatibility (Incremental)

- Public API uses XNA-style namespaces, especially under `Microsoft::Xna::Framework`.
- Core game loop and framework primitives are available (`Game`, `GameTime`, graphics types, input/audio surfaces).
- Compatibility is partial and evolving; implementation status is tracked progressively in source.

### Rendering

- `GraphicsDevice` abstraction with backend delegation.
- `SpriteBatch` API with `Begin(...)` / `Draw(...)` / `End()` workflow.
- `Texture2D` abstraction with backend-owned texture resources.

### Cross-Platform Direction

- SDL3-based platform foundation for windowing/input/audio integration.
- Backend abstraction supports targeting multiple rendering paths from one API layer.
- **Windows support** via the `SDL_RENDERER` backend (MSVC, clang-cl, or MinGW-w64) — cross-compiled
  with MinGW-w64 and verified running under Wine.
- Linux support via `EASYGL` (OpenGL) or `SDL_RENDERER`.
- **Web (Emscripten) and Android (NDK) targets are implemented and verified**, not just
  architecturally planned — see section 7 (Networking, Services & Avatar) below for real
  cross-platform `Net` verification on both.

### Performance / C++ Advantages

- Native C++23 codebase and explicit control over memory/lifetime.
- Interface-driven backend boundaries to keep hot rendering paths backend-specific.
- Lightweight gameplay-facing API over backend-specific implementations.

## 4. 🏗 Architecture

CNA is organized into clear layers with strict responsibility boundaries:

```text
+-----------------------------------------------------------+
|                 Game / Application Code                  |
|        (uses Microsoft::Xna::Framework API)             |
+------------------------------+----------------------------+
                               |
                               v
+-----------------------------------------------------------+
|            API Layer (XNA-style public surface)           |
| include/Microsoft/Xna/Framework/...                       |
| - Game, GraphicsDevice, SpriteBatch, Texture2D, ...       |
+------------------------------+----------------------------+
                               |
                               v
+-----------------------------------------------------------+
|         CNA Internal Layer (abstractions/factories)       |
| include/CNA/Internal/Backends + src/CNA/Internal/Backends |
| - IGraphicsBackend, ISpriteBatchBackend, ITextureBackend  |
+------------------------------+----------------------------+
                               |
                               v
+-----------------------------------------------------------+
|               Backend Implementations                     |
| src/CNA/Internal/Backends/{SdlRenderer,EasyGL,Vulkan}    |
+-----------------------------------------------------------+
```

### Interface vs Implementation Separation

- **Public API** lives under `include/Microsoft/...` and stays framework-facing.
- **Backend contracts** live under `CNA::Internal::Backends` interfaces.
- **Backend implementations** live under `src/CNA/Internal/Backends/...`.
- `GraphicsDevice` constructs backends via factory (`CreateGraphicsBackend(...)`) based on build-time backend selection.

## 5. 🎮 Rendering System

`SpriteBatch` is the primary 2D rendering abstraction.

- You create it against a `GraphicsDevice`.
- Call `Begin(...)` to start a draw pass.
- Issue `Draw(...)` calls for textures/sprites.
- Call `End()` to close the batch.

The API surface is backend-agnostic, while rendering behavior is executed by backend-specific `ISpriteBatchBackend` implementations.

This keeps game code stable while allowing backend-specific optimizations in SDL renderer, EasyGL, or future Vulkan paths.

## 6. 🔌 Backend System

CNA supports backend selection at build-time via `CNA_GRAPHICS_BACKEND` (choose one backend per build configuration):

- `SDL_RENDERER`
- `EASYGL`
- `BGFX`
- `VULKAN`
- `WEBGPU`
- `HEADLESS`
- `SOFTWARE`
- `D3D11` (Windows-only)
- `D3D12` (Windows-only)
- `DX3`

### Tradeoffs

- **SDL_Renderer backend**
    - Simpler integration and broad SDL portability.
    - Good for straightforward 2D workflows.

- **EasyGL backend (OpenGL-based path through `easy-gl`)**
    - Custom shader-driven rendering path.
    - Better control over rendering behavior and extensibility than fixed SDL renderer usage.

- **BGFX backend**
    - Dedicated backend option with the same public rendering API coverage as other backends.
    - Integrates through CNA backend abstraction and can be selected via `CNA_GRAPHICS_BACKEND=BGFX`.
    - Uses native `bgfx` API (window/platform init, texture creation, sprite draws, frame submission), not `SDL_Renderer` rendering.
    - `bgfx` is integrated in CMake for this backend via `FetchContent` (`bgfx.cmake`).

- **Vulkan backend**
    - Present as an architecture target/scaffold.
    - Current implementation is incomplete and contains TODO/stub areas.

## 7. 🌐 Networking, Services & Avatar

Beyond graphics, CNA ports the XNA 4.0 `GamerServices` and `Net` namespaces (and, within
`GamerServices`, the Avatar subsystem), with real cross-platform networking behind them.

### GamerServices

- Complete XNA-shaped port of the Xbox LIVE-era gamer services API surface: `Gamer`,
  `SignedInGamer`, `GamerProfile`, `FriendGamer`/`FriendCollection`, leaderboards
  (`LeaderboardReader`/`LeaderboardWriter`/`LeaderboardEntry`), `Guide`, achievements, and more.
- **Not** binary-compatible with real Xbox Live — reimplements the public API shape with
  local/synthetic semantics, matching how FNA itself already handles this namespace.

### Net (`Microsoft::Xna::Framework::Net`)

- Complete `NetworkSession` API surface (5 enums + 18 classes).
- **Real networking** for `NetworkSessionType::SystemLink`, backed by
  [ENet](http://enet.bespin.org/) (reliable UDP, vendored directly under `third_party/enet`) —
  hosting, joining, LAN discovery, `AppData` relay, disconnect handling, and `StartGame`/`EndGame`
  state broadcast all run over a genuine transport, not a stub. Every other `NetworkSessionType`
  remains a synthetic (non-networked) stub, matching upstream XNA/FNA behavior.
- **Verified real networking across four platforms:**
  - **Linux** — native ENet/UDP, including a genuine two-OS-process loopback test.
  - **Windows** — native ENet/UDP via WinSock2; cross-compiled with MinGW-w64 and verified running
    under Wine.
  - **Web (Emscripten)** — real ENet traffic carried over actual WebSocket connections. A browser
    tab can only ever be a network *client* (browsers cannot open listening sockets at all); real
    hosting requires a Node.js-run process.
  - **Android (NDK)** — native ENet/UDP via bionic libc's genuine POSIX sockets, verified on a real
    x86_64 emulator — no platform-specific transport workarounds needed at all, unlike Web.

### Avatar

- `AvatarAnimation`, `AvatarDescription`, `AvatarRenderer`, and their supporting enums/types (all
  within `Microsoft::Xna::Framework::GamerServices`) are ported from a decompiled real Microsoft
  XNA 4.0 reference assembly — FNA itself never implemented Avatar, since real avatar rendering
  required Xbox Live's cloud avatar-editor service. The API shape is complete, with the real
  (occasionally surprising, always-inert) stubbed behavior of the original assembly preserved
  faithfully rather than "improved."

## 8. 🧰 Technology Stack

- **Language:** C++23
- **Core platform/runtime library:** SDL3 (vendored via Git submodule at `third_party/SDL`)
- **Media integration:** `SDL3_image`, `SDL3_mixer` (vendored via Git submodules)
- **Graphics dependency:** `easy-gl` (for `EASYGL` backend)
- **Networking:** [ENet](http://enet.bespin.org/) (vendored directly at `third_party/enet`) —
  reliable-UDP transport backing `Microsoft::Xna::Framework::Net`'s `SystemLink` sessions
- **Utility/runtime layer:** `sharp-runtime`
- **Build system:** CMake
- **Tests:** GoogleTest (`CnaTests` target)

## 9. ⚡ Getting Started

### Prerequisites (Linux)

- CMake 3.20+
- C++23-capable compiler (GCC 12+ or Clang 15+)
- Dependency directories available to CMake:
    - `../sharp-runtime`
    - `../easy-gl` (only needed for `EASYGL` backend)
- SDL3, SDL3_image, and SDL3_mixer are built from vendored submodules by default — no system SDL packages required.

### Prerequisites (Windows)

- CMake 3.20+
- One of:
    - **MSVC 2022** (Visual Studio 2022, v17.8+, with C++20/23 support)
    - **clang-cl** (LLVM for Windows, targeting MSVC ABI)
    - **MinGW-w64** (either natively on Windows or cross-compiled from Linux)
- Dependency directories:
    - `../sharp-runtime` (no external dependencies — builds cleanly on Windows)
- SDL3, SDL3_image, and SDL3_mixer are built from vendored submodules by default — no pre-built SDL binaries or `CMAKE_PREFIX_PATH` configuration required.

### Initialise Submodules

Before the first build, initialise the vendored SDL submodules:

```bash
git submodule update --init --recursive
```

This populates `third_party/SDL`, `third_party/SDL_image`, and `third_party/SDL_mixer`.
After that, no system SDL packages are required.

> **Building from a source zip/tarball instead of a Git clone?** GitHub's "Download ZIP"
> and release archives do **not** include submodule contents, so `third_party/SDL` will be
> empty and CMake aborts with a clear error (`Missing vendored 'SDL' … Run: git submodule
> update --init --recursive`, from `cmake/ThirdPartySDL.cmake`). Either clone with Git and run
> the command above, or set `-DCNA_USE_SYSTEM_SDL=ON` to use system-installed SDL3 packages.

### Build (Linux — EASYGL backend, default)

```bash
git submodule update --init --recursive
cmake -S . -B build -DCNA_GRAPHICS_BACKEND=EASYGL
cmake --build build --target CNA CnaTests
```

### Build (Linux — SDL_RENDERER backend)

```bash
git submodule update --init --recursive
cmake -S . -B build-sdlrenderer -DCNA_GRAPHICS_BACKEND=SDL_RENDERER
cmake --build build-sdlrenderer --target CNA CnaTests
```

### Build (Windows — SDL_RENDERER backend, vendored SDL)

On Windows the `SDL_RENDERER` backend is selected automatically when no backend is
explicitly specified. SDL is built from the vendored submodule — no pre-built SDL
binaries or `CMAKE_PREFIX_PATH` needed.

```bash
git submodule update --init --recursive
cmake -S . -B build-win -DCNA_GRAPHICS_BACKEND=SDL_RENDERER
cmake --build build-win --target CNA CnaTests
```

### Build (Linux → Windows cross-compilation with MinGW-w64)

```bash
# Install cross toolchain
sudo apt install mingw-w64

git submodule update --init --recursive
cmake -S . -B build-windows \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_BACKEND=SDL_RENDERER
cmake --build build-windows --target CNA CnaTests
```

### Optional: use system-installed SDL

If you prefer to link against system-installed SDL3 packages instead of the
vendored submodules, pass `-DCNA_USE_SYSTEM_SDL=ON`:

```bash
cmake -S . -B build -DCNA_USE_SYSTEM_SDL=ON -DCNA_GRAPHICS_BACKEND=SDL_RENDERER
cmake --build build --target CNA CnaTests
```

This calls `find_package(SDL3 REQUIRED)`, `find_package(SDL3_image REQUIRED)`,
and `find_package(SDL3_mixer REQUIRED)` and requires those packages to be present
on the system (e.g. installed via your package manager).

### Other backends

```bash
cmake -S . -B build -DCNA_GRAPHICS_BACKEND=BGFX
cmake -S . -B build -DCNA_GRAPHICS_BACKEND=VULKAN
```

### Build (Windows cross-compilation — D3D11 backend)

A native Direct3D 11 backend, Windows-only (hard-`FATAL_ERROR`-gated at configure time on any other
`CMAKE_SYSTEM_NAME`). Developed and verified on this repo's own Debian dev machine via the same
MinGW-w64 cross toolchain `SDL_RENDERER` uses, tested locally through Wine + DXVK
(`scripts/run-wine-dxvk.sh`) before any real-Windows verification pass. See
[`docs/d3d11-backend.md`](docs/d3d11-backend.md) and [`plan_dx.md`](plan_dx.md) for full detail.

```bash
# Install cross toolchain (same package SDL_RENDERER's own Windows cross-build uses)
sudo apt install mingw-w64

git submodule update --init --recursive
cmake -S . -B cmake-build-d3d11 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_BACKEND=D3D11 \
      -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-d3d11 --target CNA
```

Running the resulting `.exe`s needs a Wine + DXVK dev-loop (`docs/d3d11-backend.md` has full setup
steps); CTest wires this in automatically:

```bash
ctest --test-dir cmake-build-d3d11 -R D3D11 --output-on-failure
```

### Build (Windows cross-compilation — D3D12 backend)

A native Direct3D 12 backend, Windows-only (hard-`FATAL_ERROR`-gated at configure time, same as
`D3D11`). Also developed and verified on this repo's own Debian dev machine via the same MinGW-w64
cross toolchain, but tested locally through Wine + **vkd3d-proton** (`scripts/run-wine-vkd3d.sh`),
not DXVK — D3D12 needs a different Windows-D3D-to-Vulkan translation layer than D3D11, with its own
dedicated Wine prefix. Every check currently runs **off-screen only** — swap-chain presentation is
a known, real, unresolved gap on this dev loop (see `docs/d3d12-backend.md`). See
[`docs/d3d12-backend.md`](docs/d3d12-backend.md) and [`plan_dx.md`](plan_dx.md) for full detail.

```bash
# Install cross toolchain (same package D3D11/SDL_RENDERER's own Windows cross-build uses)
sudo apt install mingw-w64

git submodule update --init --recursive
cmake -S . -B cmake-build-d3d12 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_BACKEND=D3D12 \
      -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-d3d12 --target CNA
```

Running the resulting `.exe`s needs a Wine + vkd3d-proton dev-loop, in a prefix separate from
D3D11's own (`docs/d3d12-backend.md` has full setup steps); CTest wires this in automatically:

```bash
ctest --test-dir cmake-build-d3d12 -R D3D12 --output-on-failure
```

### Run Demo / Verification

This repository intentionally prioritizes framework/runtime development over shipping a bundled game demo executable.

Use these commands for quick environment and rendering-path verification:

```bash
ctest --test-dir build --output-on-failure
cmake --build build --target hello-triangle-sdl
```

### Tested Compilers

| Platform | Compiler | Backend | Status |
|----------|----------|---------|--------|
| Linux x86_64 | GCC 12+ | EASYGL, SDL_RENDERER | ✅ |
| Linux x86_64 | Clang 15+ | EASYGL, SDL_RENDERER | ✅ |
| Windows x86_64 | MSVC 2022 | SDL_RENDERER | planned |
| Windows x86_64 (native) | MinGW-w64 | SDL_RENDERER | planned |
| Linux → Windows (cross) | MinGW-w64 | SDL_RENDERER | ✅ verified building + full test suite under Wine |
| Linux → Windows (cross) | MinGW-w64 | D3D11 | ✅ verified building + `D3D11` CTest suite (6 tests, 96+ checks) under Wine+DXVK on a real GPU — real Windows hardware verification still open, see `docs/d3d11-backend.md` |
| Linux → Windows (cross) | MinGW-w64 | D3D12 | ✅ verified building + `D3D12` CTest suite (1 test, 80/80 checks, off-screen only) under Wine+vkd3d-proton on a real GPU — swap-chain presentation and real Windows hardware verification both still open, see `docs/d3d12-backend.md` |
| Web (Emscripten) | emcc/Clang (emsdk) | EASYGL | ✅ verified building + running under Node.js |
| Android (NDK) | Clang (NDK 29/30) | EASYGL | ✅ verified building + running on a real x86_64 emulator |

## 10. 📖 Usage Example

Minimal XNA-style game skeleton in CNA:

```cpp
#include <memory>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class MyGame final : public Game {
public:
    MyGame()
        : graphics_(this)
    {
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        logo_ = std::make_unique<Texture2D>("assets/logo.png", getGraphicsDeviceProperty());
    }

    void Update(GameTime& gameTime) override
    {
        (void)gameTime;
        // Update game state here.
    }

    void Draw(const GameTime& gameTime) override
    {
        (void)gameTime;

        auto& device = getGraphicsDeviceProperty();
        device.Clear(CornflowerBlue);

        spriteBatch_->Begin();
        spriteBatch_->Draw(*logo_, 100.0f, 80.0f);
        spriteBatch_->End();

        device.Present();
    }

private:
    GraphicsDeviceManager graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> logo_;
};

int main()
{
    MyGame game;
    game.Run();
    return 0;
}
```

## 11. 🧠 Design & Engineering Highlights

- **API mirroring strategy:** Public classes follow XNA naming and namespace conventions to reduce conceptual migration cost from XNA/MonoGame-style code.
- **Abstraction design:** Gameplay-facing rendering APIs (`GraphicsDevice`, `SpriteBatch`, `Texture2D`) delegate to backend interfaces instead of exposing low-level renderer objects.
- **Separation of concerns:** Public framework API, internal contracts, and backend implementations are physically separated in directory structure and ownership.
- **Backend-oriented architecture:** Backend can be swapped at build-time with a single CMake option while keeping high-level game code stable.
- **Performance-minded C++ implementation:** Native code path enables tighter control over memory, lifetime, and rendering behavior than managed runtime abstractions.

## 12. 🛣 Roadmap

- Continue expanding XNA API coverage and behavior parity (incremental, class-by-class).
- Implement compiled `.fx` shader bytecode support (`Effect(GraphicsDevice&, byte[])`) — the single biggest real gap; it blocks 23 of the 86 official XNA samples in `../cna-samples`.
- Consider a real `.xnb` content-pipeline reader (currently a deliberate design choice, not a bug — CNA loads raw assets + JSON descriptors instead).
- Close the remaining named architecture-decision gaps (SDL_Renderer `TextureAddressMode::Wrap`/`Mirror`, SDL_Renderer `Texture3D`/`TextureCube` construction, EasyGL non-`Color` `SurfaceFormat` GPU forwarding, `Texture3D`/`TextureCube` sampler-bind architecture) — see `NEXT.md` §5 and `docs/graphics-backend-feature-matrix.md`.
- Strengthen cross-platform execution targets and validation coverage.

## 13. 📜 License

CNA is licensed under the Microsoft Public License (Ms-PL). See the [LICENSE](LICENSE) file for details.

Portions of CNA are derived from or based on FNA, which is also licensed under the Microsoft Public License (Ms-PL).
