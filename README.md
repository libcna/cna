# CNA

## 1. Overview

CNA is a C++ reimplementation of the XNA 4.0 programming model, built on SDL3 and a pluggable graphics renderer layer.

It is a framework/runtime and abstraction layer—not a game—designed to preserve XNA-style APIs (`Microsoft::Xna::Framework`) while using modern C++ internals.

Goals:

- Recreate the XNA developer experience in native C++.
- Provide a native C++ path for teams that like the XNA/MonoGame model but need non-managed runtime/toolchain control.
- Mirror core XNA namespaces and API patterns while implementing them incrementally.
- Decouple gameplay-facing API from rendering renderer implementation details.
- Enable one high-level API surface across different rendering technologies.
- Keep SDL- and graphics-API-level concerns behind framework abstractions.

## 2. Status and where to look

- Current state, known bugs and limitations: [KNOWN_ISSUES.md](KNOWN_ISSUES.md), the one authoritative bug list.
- Index of the documentation: [`docs/README.md`](docs/README.md). What a task ID in a comment belonged to:
  [`docs/task-id-index.md`](docs/task-id-index.md).
- Graphics: what each renderer supports is [`docs/graphics-renderer-feature-matrix.md`](docs/graphics-renderer-feature-matrix.md).
- API surface: every public type and documented member of the XNA 4.0 reference corpus is represented; counts and
  classifications are in the [member audit](docs/xna-4-runtime-member-coverage.md)
  (`python3 tools/audit_xna_runtime_surface.py --write-reports`). Representation does not establish behavioural
  compatibility; the Content Pipeline is measured separately.
- Compiled XNA effects (Direct3D 9 Effect Framework bytecode through `Effect(GraphicsDevice&, byte[])` and the XNB
  `EffectReader`): [`docs/shader-effect-vs-fx-bytecode.md`](docs/shader-effect-vs-fx-bytecode.md).
- Verification method: differential tests against a running `FNA.dll` with disputed behaviour settled against genuine
  XNA 4.0 ([`docs/fna-reference-harness.md`](docs/fna-reference-harness.md)), and the `CNAEXT` purity build
  (`CHECKLIST.md`, "CNAEXT markers").
- CI (`.github/workflows/`) runs the full suite on Linux for `OPENGLES3` plus narrower subsystem, Apple and MSVC jobs;
  there is no automatic GPU pixel matrix across renderers and no automatic Android gate.

## 3. Version and dependencies

Current release: **0.2.0** (pre-1.0 — a minor release may still change the public API; see
[`CHANGELOG.md`](CHANGELOG.md) and [`docs/releasing.md`](docs/releasing.md)). Compiled code reads its own version from
`CNA::getVersionString()` in `CNA/Version.hpp`.

CNA 0.2.0 requires the sibling checkouts **sharp-runtime 0.2.0**, **easy-gl 0.1.1** and **meta-gl 0.4.1** (or a later
patch release of each). They are not submodules: check out those tags next to this repository; configuration stops with
instructions when a checkout declares an incompatible version (`cmake/DependencyVersions.cmake`).

Binary C++ consumers opt in to `CNA_INSTALL_CXX=ON` and install the SDK, which exports `CNA::` and `SharpRuntime::`
for `find_package(CNA CONFIG)` ([`docs/installed-cpp-sdk.md`](docs/installed-cpp-sdk.md)).

## 4. Quick start

```bash
git submodule update --init
cmake -S . -B build -DCNA_GRAPHICS_RENDERER=OPENGLES3
cmake --build build
tools/platform/run_gpu_tests_private.sh build --output-on-failure   # private display, never the desktop
```

Tests that open a window run on a private headless compositor, never on the live desktop.

## 5. Renderers

One renderer is chosen per build with `CNA_GRAPHICS_RENDERER`; the registry and capability inventory is
[`docs/renderer-registry.md`](docs/renderer-registry.md).

| Identity | Role | Default on | Page |
|---|---|---|---|
| `SDLGPU` | Modern native path; SDL's `SDL_GPU` owns its Vulkan, Direct3D 12 and Metal backends | Windows, macOS, iOS | [sdl-gpu-renderer.md](docs/sdl-gpu-renderer.md) |
| `OPENGLES3` | EasyGL, GL ES 3.0 profile | Linux, Android, other hosts | [renderer-registry.md](docs/renderer-registry.md) |
| `OPENGL33` | EasyGL, desktop GL 3.3 core profile | — | [opengl33-renderer.md](docs/opengl33-renderer.md) |
| `WEBGL2` | EasyGL, WebGL 2 (Emscripten only) | Emscripten | [webgl2-renderer.md](docs/webgl2-renderer.md) |
| `WEBGPU` | wgpu-native, and emdawnwebgpu in the browser (supported) | — | [webgpu-renderer.md](docs/webgpu-renderer.md) |
| `DIRECTX9` | Windows-only Direct3D 9 running Microsoft's Stock Effects bytecode; diffed against real XNA 4.0 renders | — | [directx9-renderer.md](docs/directx9-renderer.md) |
| `FNA3D` | FNA's graphics library (SDL_GPU/Direct3D 11/OpenGL chosen at run time) | — | [fna3d-renderer.md](docs/fna3d-renderer.md) |
| `SOFTWARE` | Deterministic CPU rasterizer, no GPU or window (experimental) | — | [software-renderer.md](docs/software-renderer.md) |
| `HEADLESS` | CI/testing renderer: validates and traces graphics calls without rasterizing | — | [headless-renderer.md](docs/headless-renderer.md) |
| `STUB` | Minimal no-op renderer | — | [stub-renderer.md](docs/stub-renderer.md) |

Several renderers can be compiled into one binary and one chosen at run time, before the game starts; an unavailable
renderer is an error unless the game opts in to a fallback chain
([`docs/runtime-renderer-selection.md`](docs/runtime-renderer-selection.md)):

```cpp
#include "CNA/GraphicsRendererSelection.hpp"

CNA::GraphicsRendererSelection::SetPreferred(CNA::GraphicsRendererType::SdlGpu);
```

The set is curated; retired identities and why they went are in [`docs/removed-renderers.md`](docs/removed-renderers.md).

## 6. Platforms

`CNA_PLATFORM` selects `SDL3` (default; Windows, X11, Wayland, macOS, iOS, Android and the browser through SDL3's own
drivers), `HEADLESS`, or POSIX-only `TERMINAL` ([`docs/platform-abstraction.md`](docs/platform-abstraction.md)).

| Platform | Toolchain | Renderers | Limits |
|---|---|---|---|
| Linux x86_64 | GCC 12+, Clang 15+ | `OPENGLES3`, `OPENGL33`, `SDLGPU` (Vulkan), `WEBGPU`, `FNA3D`, `SOFTWARE`, `HEADLESS`, `STUB` | [feature matrix](docs/graphics-renderer-feature-matrix.md) |
| Windows x86_64 | MSVC 2022 (presets) | `HEADLESS`, `SDLGPU` (automatic workflow); `DIRECTX9` (manual workflow) | [windows-visual-studio.md](docs/windows-visual-studio.md) |
| Linux → Windows | MinGW-w64, run under Wine | `SDLGPU` (Direct3D 12 through vkd3d-proton), `SOFTWARE`, `DIRECTX9` (through DXVK) | [directx9-renderer.md](docs/directx9-renderer.md) |
| Web | Emscripten (emsdk) | `WEBGL2`, `WEBGPU`, in Chrome | [web-emscripten-graphics-limitations.md](docs/web-emscripten-graphics-limitations.md) |
| Android | NDK 29/30 Clang | `OPENGLES3`, x86_64 emulator | [android-graphics-limitations.md](docs/android-graphics-limitations.md) |
| macOS | Xcode | `SOFTWARE` (CI); `SDLGPU`'s Metal backend is not re-qualified | [apple-platforms.md](docs/apple-platforms.md) |
| iOS / iPadOS | Xcode | `SOFTWARE` (experimental; simulator smoke app only) | [apple-platforms.md](docs/apple-platforms.md) |

Validation on real Windows hardware and on physical Apple devices is open.

## 7. Subsystems

- **Input:** `Keyboard`, `Mouse` (incl. `MouseCursor`), `GamePad` (4 players), `TouchPanel` and `GestureDetector` under
  `Microsoft::Xna::Framework::Input`, matching XNA 4.0 member for member ([`docs/input-fna-fidelity.md`](docs/input-fna-fidelity.md)).
  CNAEXT adds `TextInputEXT`, rumble/light-bar/gyro on `GamePad`, raw `CNA::Input::Joysticks`, `Sensors`, `Power` and
  `Haptics`. All events flow through one funnel (`IPlatform::PollEvents` → `PlatformInputBridge::ProcessEvent`).
- **Content and glTF:** `Content.Load<Model>("character.glb")` loads glTF 2.0 directly; `tools/gltf_to_cnj` produces
  `.cnj` + binary sidecars with identical output. glTF data beyond XNA's model (four joint influences, three directional
  lights, two UV channels, one colour channel) is counted and reported, never silently dropped; a generated conformance
  corpus holds it. Read [`docs/gltf-limitations.md`](docs/gltf-limitations.md) before choosing CNA for a glTF pipeline.
- **CNAEXT graphics extensions:** `CNA_CNAEXT=ON` (off by default) builds `AsciiPostProcessEffect`, `CRTEffect`,
  `DepthEffect` and `DebugDraw` ([`docs/cnaext-engine-layer.md`](docs/cnaext-engine-layer.md)). The graphics core, custom
  shaders, `PbrEffect`/`SkinnedPbrEffect`, glTF loading and the CNB/CNJ pipeline do not depend on it.
- **Diagnostics and inspector:** `CNA_DIAGNOSTICS=OFF|STATS|FULL` (default `OFF`, compiled out) is a bounded profiler and
  resource-metadata layer; `CNA_BUILD_INSPECTOR=ON` builds an explicitly started agent and the localhost `cna-inspector`
  browser bridge ([`docs/diagnostics.md`](docs/diagnostics.md), [`docs/inspector.md`](docs/inspector.md)).
- **GamerServices, Net and Avatar:** the XNA 4.0 `GamerServices` and `Net` namespaces backed by CNA's own account service
  ([`cna-gamer-services-server`](https://github.com/libcna/cna-gamer-services-server)): accounts and Guide sign-in,
  profiles, friends, presence, messages, achievements, leaderboards, invitations and parties, or local offline profiles
  without a service. The public API is XNA-compatible and the workflow Xbox-like; backend policy is CNA's own and is
  not Xbox LIVE compatible ([`docs/gamer-services-server.md`](docs/gamer-services-server.md),
  [`docs/gamer-services-known-limitations.md`](docs/gamer-services-known-limitations.md)). `SystemLink` runs over ENet
  (`third_party/enet`); `PlayerMatch` and `Ranked` run through the service's authenticated relay; voice uses libopus
  (`CNA_ENABLE_VOICE`). Browsers have no multiplayer through the XNA API
  ([`docs/browser-network-readiness.md`](docs/browser-network-readiness.md)). Avatars render on XNA's 71-bone skeleton
  with the 31 animation presets from original CNA catalogs ([`docs/avatars.md`](docs/avatars.md)).

## 8. Architecture

- **Game code** uses only the `Microsoft::Xna::Framework` API.
- **XNA API layer:** `modules/<module>/include/Microsoft/Xna/Framework/...` (`Game`, `GraphicsDevice`, `SpriteBatch`, ...).
- **Renderer contracts:** `modules/graphics/include/CNA/Internal/Renderers/Common/` (`IGraphicsRenderer`,
  `ISpriteBatchRenderer`, `ITextureRenderer`).
- **Renderer implementations:** `modules/renderers/<family>/`, created by `CreateGraphicsRenderer(...)` from the
  selected renderer.
- **Platform services:** `CNA::Platform::IPlatform`; SDL3 calls stay in its platform, audio and renderer edges.
- **Runtime support:** `sharp-runtime` (`System::*` and the .NET primitive aliases).

Module ownership and the physical layout are in [`docs/physical-modules.md`](docs/physical-modules.md).

## 9. Building and running tests

Prerequisites: CMake 3.20+ (3.21+ for the presets), a C++23 compiler (GCC 12+, Clang 15+ or MSVC 2022 17.8+), the
`../sharp-runtime`, `../easy-gl` and `../meta-gl` sibling checkouts, and libcurl and nlohmann-json for GamerServices and
Net (both on by default). SDL3 and SDL3_mixer are built from vendored submodules. FFmpeg is optional
(`CNA_ENABLE_VIDEO=AUTO`, [`docs/video-backend.md`](docs/video-backend.md)). The Debian package list, sibling setup,
system SDL and source-archive notes are in [`docs/development-environment.md`](docs/development-environment.md).

```bash
git submodule update --init                       # SDL, SDL_mixer, Draco, googletest, avatar catalogs

# Linux (OPENGLES3 is the default); other renderers: -DCNA_GRAPHICS_RENDERER=OPENGL33|SDLGPU|...
cmake -S . -B build -DCNA_GRAPHICS_RENDERER=OPENGLES3
cmake --build build --target CnaTests

# Linux -> Windows with MinGW-w64 (sudo apt install mingw-w64); SDLGPU or DIRECTX9
cmake -S . -B cmake-build-mingw -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake -DCNA_GRAPHICS_RENDERER=DIRECTX9
cmake --build cmake-build-mingw --target CnaTests --parallel 8
ctest --test-dir cmake-build-mingw -L DIRECTX9 --output-on-failure   # Wine + DXVK through scripts/run-wine-dxvk9.sh

# macOS
cmake -S . -B cmake-build-macos -DCNA_GRAPHICS_RENDERER=SDLGPU
cmake --build cmake-build-macos --target CnaTests --parallel 8
```

On Windows `SDLGPU` is the default and the presets serve Visual Studio, VS Code and CI
([`docs/windows-visual-studio.md`](docs/windows-visual-studio.md)):

```powershell
cmake --preset windows-msvc-sdlgpu-debug
cmake --build --preset windows-msvc-sdlgpu-debug
ctest --preset windows-msvc-sdlgpu-debug
```

The MinGW build needs a Windows libcurl (`CURL_INCLUDE_DIR`, `CURL_LIBRARY`) or
`-DCNA_ENABLE_NET=OFF -DCNA_ENABLE_GAMER_SERVICES=OFF`; `SDLGPU`'s Direct3D 12 backend runs under Wine through
`scripts/run-wine-vkd3d.sh`. `DIRECTX9` configures only for Windows targets
([`docs/directx9-renderer.md`](docs/directx9-renderer.md)). iOS/iPadOS cross-compilation (`cmake/toolchains/ios.cmake`)
is in [`docs/apple-platforms.md`](docs/apple-platforms.md).

Run the tests on the private display; the second command also proves the run left the checkout clean:

```bash
tools/platform/run_gpu_tests_private.sh build --output-on-failure
scripts/check_clean_checkout.sh -- tools/platform/run_gpu_tests_private.sh build
```

## 10. Usage example

Minimal XNA-style game skeleton in CNA (`Game` presents the frame itself, exactly as in XNA):

```cpp
#include <memory>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class MyGame final : public Game {
public:
    MyGame() : graphics_(this) { getContentProperty().setRootDirectoryProperty("Content"); }

protected:
    void LoadContent() override
    {
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        logo_ = std::make_unique<Texture2D>(getContentProperty().Load<Texture2D>("logo"));
    }

    void Draw(const GameTime& gameTime) override
    {
        getGraphicsDeviceProperty().Clear(Color::CornflowerBlue);

        spriteBatch_->Begin();
        spriteBatch_->Draw(*logo_, Vector2(100.0f, 80.0f), Color::White);
        spriteBatch_->End();

        Game::Draw(gameTime);
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

## 11. Direction and license

CNA is in long-term maintenance by a human C++ developer: fixing the defects in [KNOWN_ISSUES.md](KNOWN_ISSUES.md),
keeping behaviour faithful to XNA 4.0 and keeping validation deterministic. The renderer set, the platform layer and the
content formats are kept as they are; new renderers, asset formats or subsystems are not planned.

CNA is licensed under the Microsoft Public License (Ms-PL); see [LICENSE](LICENSE). Portions of CNA are derived from or
based on FNA, which is also licensed under the Ms-PL.
