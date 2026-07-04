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

### Project Status

- **Overall maturity:** Active, incremental framework development; XNA API coverage is partial and expanding.
- **`SDL_RENDERER` backend:** Implemented path focused on practical 2D rendering workflows.
- **`EASYGL` backend:** Implemented OpenGL-based path through `easy-gl`, used for backend-level rendering control.
- **`VULKAN` backend:** Architecture scaffold present, but implementation is currently incomplete.

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
- Improve backend parity and complete missing/stubbed backend functionality.
- Advance Vulkan backend from scaffold to practical rendering path.
- Extend rendering capabilities beyond current 2D-focused workflows.
- Strengthen cross-platform execution targets and validation coverage.

## 13. 📜 License

CNA is licensed under the Microsoft Public License (Ms-PL). See the [LICENSE](LICENSE) file for details.

Portions of CNA are derived from or based on FNA, which is also licensed under the Microsoft Public License (Ms-PL).
