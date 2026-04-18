# CNA

## 1. 🚀 Overview

CNA is a C++ reimplementation of the XNA 4.0 programming model, built on SDL3 and a pluggable graphics backend layer.

It is a framework/runtime and abstraction layer—not a game—designed to preserve XNA-style APIs (`Microsoft::Xna::Framework`) while using modern C++ internals.

**CNA demonstrates engine-level C++ architecture, graphics abstraction design, and backend-oriented systems engineering.**

## 2. 🎯 Goals

- Recreate the XNA developer experience in native C++.
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

CNA currently supports build-time backend selection via `CNA_GRAPHICS_BACKEND`:

- `SDL_RENDERER`
- `EASYGL`
- `VULKAN`

### Tradeoffs

- **SDL_Renderer backend**
    - Simpler integration and broad SDL portability.
    - Good for straightforward 2D workflows.

- **EasyGL backend (OpenGL-based path through `easy-gl`)**
    - Custom shader-driven rendering path.
    - Better control over rendering behavior and extensibility than fixed SDL renderer usage.

- **Vulkan backend**
    - Present as an architecture target/scaffold.
    - Current implementation is incomplete and contains TODO/stub areas.

## 7. 🧰 Technology Stack

- **Language:** C++23
- **Core platform/runtime library:** SDL3
- **Media integration:** `SDL3_image`, `SDL3_mixer`, `SDL3_ttf`
- **Graphics dependency:** `easy-gl` (for `EASYGL` backend)
- **Utility/runtime layer:** `cpp-dotnet`
- **Build system:** CMake
- **Tests:** GoogleTest (`CnaTests` target)

## 8. ⚡ Getting Started

### Prerequisites

- CMake 3.20+
- C++23-capable compiler
- SDL3 + SDL3_image + SDL3_mixer + SDL3_ttf development packages
- Dependency directories available to CMake:
    - `../cpp-dotnet`
    - `../easy-gl`

### Build

```bash
cmake -S . -B build -DCNA_GRAPHICS_BACKEND=EASYGL
cmake --build build --target CNA CnaTests
```

Change backend as needed:

```bash
cmake -S . -B build -DCNA_GRAPHICS_BACKEND=SDL_RENDERER
# or
cmake -S . -B build -DCNA_GRAPHICS_BACKEND=VULKAN
```

### Run Demo / Verification

Current repository state does not expose a dedicated CNA game demo executable by default.

Use these commands to validate the setup:

```bash
ctest --test-dir build --output-on-failure
cmake --build build --target hello-triangle-sdl
```

## 9. 📖 Usage Example

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

## 10. 🧠 Design & Engineering Highlights

- **API mirroring strategy:** Public classes follow XNA naming and namespace conventions to reduce conceptual migration cost from XNA/MonoGame-style code.
- **Abstraction design:** Gameplay-facing rendering APIs (`GraphicsDevice`, `SpriteBatch`, `Texture2D`) delegate to backend interfaces instead of exposing low-level renderer objects.
- **Separation of concerns:** Public framework API, internal contracts, and backend implementations are physically separated in directory structure and ownership.
- **Backend-oriented architecture:** Backend can be swapped at build-time with a single CMake option while keeping high-level game code stable.
- **Performance-minded C++ implementation:** Native code path enables tighter control over memory, lifetime, and rendering behavior than managed runtime abstractions.

## 11. 🛣 Roadmap

- Continue expanding XNA API coverage and behavior parity (incremental, class-by-class).
- Improve backend parity and complete missing/stubbed backend functionality.
- Advance Vulkan backend from scaffold to practical rendering path.
- Extend rendering capabilities beyond current 2D-focused workflows.
- Strengthen cross-platform execution targets and validation coverage.

## 12. 📜 License

See repository license metadata/files for current license terms.
