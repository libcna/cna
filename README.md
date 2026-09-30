# CNA

## 1. 🚀 Overview

CNA is a C++ reimplementation of the XNA 4.0 programming model, built on SDL3 and a pluggable graphics renderer layer.

It is a framework/runtime and abstraction layer—not a game—designed to preserve XNA-style APIs (`Microsoft::Xna::Framework`) while using modern C++ internals.

**CNA demonstrates engine-level C++ architecture, graphics abstraction design, and renderer-oriented systems engineering.**

### Quick Start

```bash
git submodule update --init --recursive
cmake -S . -B build -DCNA_GRAPHICS_RENDERER=OPENGLES3
cmake --build build --target CnaTests
ctest --test-dir build --output-on-failure
```

### Version

Current release: **0.1.0-alpha.1** (pre-release — the public API may still change; see
[`CHANGELOG.md`](CHANGELOG.md) for what the release contains and
[`docs/releasing.md`](docs/releasing.md) for how versions are managed). Compiled code reads its
own version from `CNA::getVersionString()` in `CNA/Version.hpp`.

> **Looking for a specific doc?** `docs/` has 93 Markdown documents — see [`docs/README.md`](docs/README.md)
> for an index of what's current vs. historical. Implementation roadmaps and retained task logs are
> indexed separately in [`plans/README.md`](plans/README.md).

### Project Status

- **`Microsoft::Xna::Framework::Graphics` milestone:** qualified **~90% XNA/FNA compatibility, test-execution-verified** (not estimated) — every one of the ~26 major Graphics classes is present, implemented, and tested. **As of 2026-07-11, all 5 confirmed bugs behind the original 2026-07-09 milestone declaration are fixed** (Vulkan `BlendState`, EasyGL anisotropic filtering, `IndexElementSize`'s numeric values, `Model`'s root-bone override, `SpriteBatch::Draw`'s optional source rectangle), and Vulkan `OcclusionQuery` (previously architecturally blocked) is fixed too. `docs/graphics-compatibility-report.md` is a dated snapshot from that declaration, kept for its methodology, not current status — see `NEXT.md` §5 for the actively-maintained bug list. What's left to 100% is a smaller set of individually-tracked issues plus a handful of project-owner architecture decisions (e.g. SDL_Renderer `TextureAddressMode::Wrap`/`Mirror`, `Texture3D`/`TextureCube` sampler-bind architecture) — none silent or undocumented.
- **Documented XNA 4.0 runtime API surface:** 331 of 331 public types (100.00%) and 3,467 of 3,627 documented non-type members (95.59%) represented in CNA in the Microsoft reference corpus. The [member audit](docs/xna-4-runtime-member-coverage.md) lists all 160 remaining gaps and their scope tiers; its [JSON report](docs/xna-4-runtime-member-coverage.json) records every reference member. Run `python3 tools/audit_xna_runtime_surface.py --write-reports`. API representation does not establish behavioral compatibility; the Content Pipeline is measured separately.
- **Compiled XNA effects:** `Effect(GraphicsDevice&, byte[])` and the canonical XNB `EffectReader`
  execute XNA/FNA Direct3D 9 Effect Framework bytecode on `FNA3D` unconditionally and on SDL_GPU,
  EasyGL/OpenGL, Vulkan, and DirectX 11 behind their renderer-specific build options. The shared
  public contract covers reflection, parameter mutation, techniques/passes, pass states, cloning,
  3D draws, and `SpriteBatch`. Unsupported renderers reject the constructor explicitly; MGFX and
  runtime `.fx` source compilation remain separate formats/projects. See
  [`docs/shader-effect-vs-fx-bytecode.md`](docs/shader-effect-vs-fx-bytecode.md).
- **`SDL_RENDERER` renderer:** Implemented path focused on practical 2D rendering workflows; 2D-only by design (3D calls throw).
- **`OPENGLES2`/`OPENGLES3`/`OPENGL33`/`WEBGL1`/`WEBGL2` renderers:** the most mature GL-family public renderers overall — one shared internal implementation (`EasyGL`, on top of `easy-gl`) driven by a GL profile choice, not five separate implementations. `OPENGLES3` (desktop/mobile GLES 3.0) and `WEBGL2` (Emscripten, GLES 3.0 → WebGL 2.0) have full 2D+3D pixel-verified coverage — this is what was previously the single `EASYGL` public renderer, split into its real public identities. `OPENGL33` (desktop GL 3.3 core) and `WEBGL1` (Emscripten, GLES 2.0 → WebGL 1.0) are newer and still landing — see `plans/plan_glbackends.md` for current per-profile status. `OPENGLES2` (native GLES 2.0, GLSL ES 1.00, Phase-2 expansion) carries a deliberately narrower ES 2.0 capability boundary — see [`docs/opengles2-renderer.md`](docs/opengles2-renderer.md).
- **`VULKAN` renderer:** Real, working 3D rendering (all 5 stock effects, render targets, depth/stencil state, `BlendState`, `OcclusionQuery`) — second-most mature renderer; the one remaining named gap is an isolated `RasterizerState.DepthBias` sub-case. See `docs/xna-4-api-coverage.md`'s per-renderer table for current detail.
- **`WEBGPU` renderer:** Experimental renderer using native `wgpu-native` (v29.0.1.1) and, since 2026-08-26, Emscripten's emdawnwebgpu port for a real in-browser path. Well past a 2D baseline: device/surface setup, clear/present, `Texture2D`/`TextureCube`/`Texture3D`, vertex/index uploads, a pixel-verified WGSL `SpriteBatch`, full 3D with every stock effect (`BasicEffect` incl. per-pixel/per-vertex lighting, `AlphaTestEffect`, `DualTextureEffect`, `SkinnedEffect` 72-bone palette, `EnvironmentMapEffect` — all with FNA fog parity, `WEBGPU-145`–`148`; plus `PbrEffect`/`SkinnedPbrEffect`), real instancing, `RenderTarget2D`/`RenderTargetCube`, MSAA, GPU occlusion queries, MRT (2-4 targets), custom WGSL `ShaderEffect`s (3D and `SpriteBatch`), full stencil state, and GPU-native block-compressed textures (DXT/BC7). `RenderTargetCube` mip regeneration/MSAA and a few narrow items remain — see the status summary in `plans/plan_webgpu.md` and [`docs/webgpu-renderer.md`](docs/webgpu-renderer.md).
- **`DIRECTX9` renderer:** Windows-only native Direct3D 9 renderer targeting real **XNA 4.0 pixel authenticity**, not just feature parity — it runs Microsoft's own vendored Stock Effects HLSL bytecode, cross-compiled via MinGW-w64 and verified through Wine+DXVK on a real GPU (14 CTest binaries). A checked-in 31-scene oracle corpus diffs CNA's render against the **real XNA 4.0 runtime's own render** of the same scene at `--tolerance 0`: **0/31 scenes currently diverge.** `GraphicsProfile.Reach`/`.HiDef` enforcement is real (the only CNA renderer where it is). Render targets sampled as textures, non-`Color` `SurfaceFormat`, and real-Windows hardware verification are still open. See [`docs/directx9-renderer.md`](docs/directx9-renderer.md), [`docs/d3d9-divergence-report.md`](docs/d3d9-divergence-report.md), and `plans/plan_dx9.md`.
- **`DIRECTX11` renderer:** Windows-only native Direct3D 11 renderer, cross-compiled via MinGW-w64 and verified through Wine+DXVK on a real GPU (6 CTest binaries, 96+ checks) — all 10 stock HLSL shader variants (`BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`), textures/render targets (MRT/MSAA/occlusion queries), state objects, SpriteBatch, and a runtime-`D3DCompile()` custom `ShaderEffect` path are real and pixel-verified. Real-Windows hardware verification (device-lost recovery, WARP fallback, driver-specific parity) is still open. See [`docs/directx11-renderer.md`](docs/directx11-renderer.md) and `plans/plan_dx.md`.
- **`DIRECTX12` renderer:** Windows-only native Direct3D 12 renderer, cross-compiled via MinGW-w64 and verified through Wine+vkd3d-proton on a real GPU, **off-screen only** (`DirectX12_Smoke` CTest, 80/80 checks) — device/queue/heaps/command-lists/fences/barriers/PSOs/root-signatures are real, all 10 stock HLSL shader variants (same DXBC as `DIRECTX11`) and a real `SpriteBatch` are pixel-verified off-screen, and device-removed recovery is real and functionally proven. Swap-chain presentation is a known, real, unresolved gap on this dev loop (genuine Wine/vkd3d-proton `dxgi.dll` architecture mismatch, not a CNA bug); runtime-settable blend/depth-stencil/rasterizer state, per-slot `SamplerState`, render targets, `Texture3D`, occlusion queries, and real-Windows hardware verification are all still open. See [`docs/directx12-renderer.md`](docs/directx12-renderer.md) and `plans/plan_dx.md`.
- **`CANVAS` renderer:** Emscripten-only HTML Canvas 2D renderer (`SpriteBatch`/`Texture2D`/`SpriteFont`/`RenderTarget2D` only, 2D-only by design like `SDL_RENDERER`) — `SpriteBatch` (incl. rotation/origin/flip/tint/transform), textures/render targets, `BlendState`/`SamplerState` mapping, and `SpriteFont` are all implemented and structurally reviewed, verified via a real `emcmake`/`emcc` 6.0.2 configure+build (`CnaTests` links and a renderer-agnostic GTest suite genuinely passes under `node`). **Not yet pixel-verified in a real browser** — this dev loop has no DOM/`CanvasRenderingContext2D` at all (`node` has none; `SDL_Init(SDL_INIT_VIDEO)` itself throws under Emscripten/`node`). See [`docs/canvas-renderer.md`](docs/canvas-renderer.md) (incl. a manual browser verification checklist) and `plans/plan_canvas.md`.
- **`METAL` renderer (macOS only, experimental):** Direct native `MTLDevice`/`CAMetalLayer` rendering with runtime-compiled MSL shaders. Its supported and evidence-backed boundary is documented in [`docs/metal-renderer.md`](docs/metal-renderer.md) and `plans/plan_metal.md`; iOS and tvOS remain unvalidated and are not claimed.
- **Verification methodology:** differential testing against a real, running `FNA.dll` reference implementation (`tools/fna-reference/`), disputed behavior settled against genuine XNA 4.0 on a Windows 7 VM, and a compile-time `CNAEXT` purity check (a dedicated CMake build option that turns every non-XNA-tagged declaration into a `[[deprecated]]` warning under `-Werror`) — see `CHECKLIST.md`'s "CNAEXT markers" section and `CMakeLists.txt`.
- **Automatic CI is partial** (see `.github/workflows/`): Linux workflows run the `Input` and `Devices`/`Sensors` gtest suites, not the full ~4,370-test unit suite or the complete GPU pixel matrix. A dedicated macOS 14 workflow builds the native Metal renderer and runs only its supported contract tests; a separate manual-dispatch Windows MSVC workflow covers D3D11/D3D12 renderer CTests. There is no automatic Windows or Android gate, and the macOS gate does not establish support for the Metal paths documented as unsupported.

## 2. 🎯 Goals

- Recreate the XNA developer experience in native C++.
- Provide a native C++ path for teams that like the XNA/MonoGame model but need non-managed runtime/toolchain control.
- Mirror core XNA namespaces and API patterns while implementing them incrementally.
- Decouple gameplay-facing API from rendering renderer implementation details.
- Enable one high-level API surface across different rendering technologies.
- Keep SDL/OpenGL/Vulkan-level concerns behind framework abstractions.

## 3. ✨ Features

### XNA API Compatibility (Incremental)

- Public API uses XNA-style namespaces, especially under `Microsoft::Xna::Framework`.
- Core game loop and framework primitives are available (`Game`, `GameTime`, graphics types, input/audio surfaces).
- Compatibility is partial and evolving; implementation status is tracked progressively in source.

### Diagnostics and Inspector

- `CNA_DIAGNOSTICS=OFF|STATS|FULL` provides the bounded renderer-independent profiler and resource
  metadata foundation; `OFF` remains the default and compiles instrumentation out.
- `CNA_BUILD_INSPECTOR=ON` optionally builds a separately linked, explicitly started application
  agent and the standalone `cna-inspector` local browser bridge. It is authenticated,
  demand-driven, localhost-only by default, and never embeds HTTP/JSON work in the game loop.
- See [`docs/diagnostics.md`](docs/diagnostics.md) and [`docs/inspector.md`](docs/inspector.md) for
  activation, security, protocol, supported views, performance measurements, and limitations.

### Input

- `Keyboard`, `Mouse` (incl. `MouseCursor`), `GamePad` (up to 4 players), `TouchPanel`/`TouchCollection`,
  and the `GestureDetector` gesture recognizer (Tap/DoubleTap/Hold/Drag/Flick/Pinch) — all under
  `Microsoft::Xna::Framework::Input`, matching FNA/XNA 4.0 behavior member-for-member.
  See `plans/plan_input.md` for the full FNA-parity audit record.
- CNAEXT extensions beyond stock XNA: `TextInputEXT` (IME composition), rumble/trigger-rumble/light-bar/
  gyro/accelerometer on `GamePad`, raw `CNA::Input::Joysticks` (distinct from `GamePad`'s mapped view),
  device-level `CNA::Input::Sensors`/`Power`, and `CNA::Input::Haptics` for standalone haptic devices.
- Single platform-event funnel (`IPlatform::PollEvents` → `PlatformInputBridge::ProcessEvent`),
  renderer-agnostic and independent of the selected native event source. SDL3 translation stays
  inside its platform implementation; the `CnaTests` input suite verifies the shared state path.

### Rendering

- `GraphicsDevice` abstraction with renderer delegation.
- `SpriteBatch` API with `Begin(...)` / `Draw(...)` / `End()` workflow.
- `Texture2D` abstraction with renderer-owned texture resources.

### Content pipeline — `.gltf` / `.glb` / `.cnj`

- **glTF 2.0 loads directly**: `Content.Load<Model>("character.glb")` — no offline step. An offline
  converter (`tools/gltf_to_cnj`) produces `.cnj` + binary sidecars for the same asset, and the two
  loaders are held to identical output by a per-fixture parity sweep.
- Geometry, PBR materials, skinning, animation (LINEAR/STEP/CUBICSPLINE), morph targets, cameras and
  punctual lights all import. What that costs is stated rather than implied: XNA's model is four
  joint influences and three directional lights, two sampled UV channels, and one colour channel — glTF data
  beyond those is **counted and reported**, never silently dropped.
- Correctness is held by a **generated 145-asset conformance corpus**: the exact L0–L6 numerical
  ladder covers container, accessor, semantic mesh, world geometry, packed GPU bytes and bound
  effect parameters per commit (including ASan + UBSan), then the production OPENGLES3 viewer
  supplies the final deterministic L7 image/disposition gate.
- **Read `docs/gltf-limitations.md` before choosing CNA for a glTF pipeline.** It lists every
  approximation and every unsupported feature next to the report field that names the loss at run
  time. The older `misc/CNAEXT.md` design is historical; use the current glTF limitations document.

### Standalone CNA graphics extensions (`CNA_CNAEXT`)

`CNA_CNAEXT=ON` enables the small `modules/graphics-ext` surface: `AsciiPostProcessEffect`,
`CRTEffect`, `DepthEffect` (colour-depth and palette reduction), and `DebugDraw`. CRT and Depth
are ordinary `ShaderEffect` descendants usable with render targets and SpriteBatch; ASCII has a
direct draw API. `DebugDraw` batches wireframe lines, bounds, spheres and frusta using `BasicEffect`.
The option is off by default. See [graphics extension guide](docs/cnaext-engine-layer.md).

The XNA-compatible graphics core, renderer backends, custom shaders, `PbrEffect` and
`SkinnedPbrEffect`, glTF loading, and CNB/CNJ content pipeline do not depend on this option.

### Cross-Platform Direction

- SDL3-based platform foundation for windowing/input/audio integration.
- Renderer abstraction supports targeting multiple rendering paths from one API layer.
- **Windows support** via the `SDL_RENDERER` renderer (MSVC, clang-cl, or MinGW-w64) — cross-compiled
  with MinGW-w64 and verified running under Wine.
- Linux support via `OPENGLES3`/`OPENGL33` (OpenGL) or `SDL_RENDERER`.
- **Web (Emscripten) and Android (NDK) targets are implemented and verified**, not just
  architecturally planned — see section 7 (Networking, Services & Avatar) below for what `Net`
  does on each (browsers have no multiplayer through the XNA API).
- **macOS** has a native CI build/test gate; **iOS/iPadOS** is experimental platform support.
  The Apple workflow final-links an actual `.app` for device and simulator and launches a
  one-frame `Game` smoke application in the simulator. There is still no physical-device,
  pixel, touch, audio, storage or performance evidence. The boundary is stated per claim in
  [`docs/apple-platforms.md`](docs/apple-platforms.md).

### Performance / C++ Advantages

- Native C++23 codebase and explicit control over memory/lifetime.
- Interface-driven renderer boundaries to keep hot rendering paths renderer-specific.
- Lightweight gameplay-facing API over renderer-specific implementations.

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
| include/CNA/Internal/Renderers + src/CNA/Internal/Renderers |
| - IGraphicsRenderer, ISpriteBatchRenderer, ITextureRenderer  |
+------------------------------+----------------------------+
                               |
                               v
+-----------------------------------------------------------+
|               Renderer Implementations                     |
| modules/renderers/{sdl-renderer,easygl,vulkan,...}/src        |
+-----------------------------------------------------------+
```

### Interface vs Implementation Separation

- **Public API** lives under `include/Microsoft/...` and stays framework-facing.
- **Renderer contracts** live under `CNA::Internal::Renderers` interfaces.
- **Renderer implementations** live under `src/CNA/Internal/Renderers/...`.
- `GraphicsDevice` constructs renderers via factory (`CreateGraphicsRenderer(...)`) based on build-time renderer selection.

## 5. 🎮 Rendering System

`SpriteBatch` is the primary 2D rendering abstraction.

- You create it against a `GraphicsDevice`.
- Call `Begin(...)` to start a draw pass.
- Issue `Draw(...)` calls for textures/sprites.
- Call `End()` to close the batch.

The API surface is renderer-agnostic, while rendering behavior is executed by renderer-specific `ISpriteBatchRenderer` implementations.

This keeps game code stable while allowing renderer-specific optimizations in SDL renderer,
EasyGL, Vulkan, and the other selected paths.

## 6. 🔌 Renderer System

CNA exposes **18 public renderer identities** through `CNA_GRAPHICS_RENDERER` (choose one per build
configuration). The set is curated: a renderer is added only when it provides meaningful platform
coverage, compatibility value, architectural value, or a capability the existing set does not
reasonably cover, and thirty-three identities have been retired
([`docs/removed-renderers.md`](docs/removed-renderers.md)). The canonical registration, implementation-sharing, capability, and platform-gate
inventory is [`docs/renderer-registry.md`](docs/renderer-registry.md).

Renderers are normally chosen at **compile time**, one per build. CNA can also be built with
several renderers and the concrete one chosen at **runtime**, before the game starts:

```cpp
#include "CNA/GraphicsRendererSelection.hpp"

CNA::GraphicsRendererSelection::SetPreferred(CNA::GraphicsRendererType::Vulkan);
```

A renderer that is unavailable or fails to start is an **error** by default — CNA never silently
substitutes another. An opt-in fallback chain is available when a game wants one. See
[docs/runtime-renderer-selection.md](docs/runtime-renderer-selection.md).

The former `ASCII` renderer identity was removed 2026-08 in favor of a renderer-neutral post-process
effect, `CNA::Graphics::AsciiPostProcessEffect` (`modules/graphics-ext/`), usable with any renderer's
`RenderTarget2D` output — see [`docs/ascii-post-process-effect.md`](docs/ascii-post-process-effect.md).

- `SDL_RENDERER`
- `SDL_GPU`
- `OPENGLES2` (native OpenGL ES 2.0, GLSL ES 1.00; internal implementation: EasyGL -- see [`docs/opengles2-renderer.md`](docs/opengles2-renderer.md))
- `OPENGLES3` (internal implementation: EasyGL)
- `OPENGL33` (internal implementation: EasyGL)
- `WEBGL1` (Emscripten only; internal implementation: EasyGL)
- `WEBGL2` (Emscripten only; internal implementation: EasyGL)
- `VULKAN`
- `WEBGPU`
- `HEADLESS`
- `SOFTWARE`
- `STUB`
- `DIRECTX9` (Windows-only; native Direct3D 9 running Microsoft's own vendored Stock Effects HLSL bytecode)
- `DIRECTX11` (Windows-only)
- `DIRECTX12` (Windows-only)
- `CANVAS` (Emscripten only)
- `METAL` (macOS only, experimental — see [`docs/metal-renderer.md`](docs/metal-renderer.md))
- `FNA3D` (FNA's own XNA-shaped graphics library; picks SDL_GPU/Direct3D 11/OpenGL at runtime, and executes XNA's actual stock effects — see [`docs/fna3d-renderer.md`](docs/fna3d-renderer.md))

### Tradeoffs

- **SDL_Renderer renderer**
    - Simpler integration and broad SDL portability.
    - Good for straightforward 2D workflows.

- **EasyGL renderer (OpenGL-based path through `easy-gl`)**
    - Custom shader-driven rendering path.
    - Better control over rendering behavior and extensibility than fixed SDL renderer usage.

- **Vulkan renderer**
    - Present as an architecture target/scaffold.
    - Current implementation is incomplete and contains TODO/stub areas.

## 7. 🌐 Networking, Services & Avatar

Beyond graphics, CNA ports the XNA 4.0 `GamerServices` and `Net` namespaces (and, within
`GamerServices`, the Avatar subsystem), backed by CNA's own account service
([`cna-gamer-services-server`](https://github.com/libcna/cna-gamer-services-server)).

### GamerServices

- The XNA gamer services API with real behaviour: with a configured CNA service, accounts and Guide
  sign-in, profiles, friends and presence, messages and player reviews, achievements, leaderboards
  (including Ranked arbitration), invitations, parties and social notifications; without one, local
  offline profiles, as on a console without Xbox LIVE. A console-style Guide (sign-in picker, gamer
  cards, friends, party, avatar editor) is drawn by CNA over the game.
- A **CNA-owned, XNA/Xbox-like** implementation with its own protocol, accounts and backend
  policies — **not** Xbox LIVE compatible. See [`docs/gamer-services-server.md`](docs/gamer-services-server.md)
  and, for everything that is not done or done differently,
  [`docs/gamer-services-known-limitations.md`](docs/gamer-services-known-limitations.md).

### Net (`Microsoft::Xna::Framework::Net`)

- Complete `NetworkSession` API surface (5 enums + 18 classes).
- **`SystemLink`** runs over [ENet](http://enet.bespin.org/) (reliable UDP, vendored under
  `third_party/enet`): hosting, joining, LAN discovery with measured QoS, data relay, host
  migration, disconnect handling and `StartGame`/`EndGame` state broadcast.
- **`PlayerMatch` and `Ranked`** run through the CNA service's session directory, with every datagram
  carried by its authenticated TLS/WSS relay: create, find, join, invitations, online host migration,
  `AddLocalGamer` and Ranked arbitration. `Local` and `LocalWithLeaderboards` need no transport.
- **Voice** is routed automatically in SystemLink and online sessions (microphone capture, Opus,
  realtime session packets, playback) where libopus is available (`CNA_ENABLE_VOICE`).
- **Networking by platform:**
  - **Linux** — native ENet/UDP, including a genuine two-OS-process loopback test.
  - **Windows** — native ENet/UDP via WinSock2; cross-compiled with MinGW-w64 and verified running
    under Wine.
  - **Web (Emscripten)** — ENet runs over Emscripten's WebSocket-emulated sockets as a client of a
    Node.js-run host, but a browser has no LAN discovery, no service transport and no relay, so a
    browser game cannot find or join sessions through the XNA API: browser multiplayer is outside
    the current scope ([`docs/browser-network-readiness.md`](docs/browser-network-readiness.md)).
  - **Android (NDK)** — native ENet/UDP via bionic libc's genuine POSIX sockets, verified on a real
    x86_64 emulator — no platform-specific transport workarounds needed at all, unlike Web.

### Avatar

- `AvatarAnimation`, `AvatarDescription` and `AvatarRenderer` behave as on the Xbox 360 (the
  Windows assembly only stubbed them): real rendering on XNA's 71-bone skeleton, the 31 animation
  presets, expressions and `AvatarDescription.Changed`, drawn from original CNA avatar catalogs
  compiled into the runtime. The service stores each account's description; a catalog a client
  lacks is installed as a verified pack. See [`docs/avatars.md`](docs/avatars.md).

## 8. 🧰 Technology Stack

- **Language:** C++23
- **Core platform/runtime library:** SDL3 (vendored via Git submodule at `third_party/SDL`)
- **Media integration:** `SDL3_image`, `SDL3_mixer` (vendored via Git submodules)
- **Graphics dependency:** `easy-gl` (for the `OPENGLES2`/`OPENGLES3`/`OPENGL33`/`WEBGL1`/`WEBGL2` renderers),
  resolved from the canonical `../easy-gl` sibling; EasyGL in turn resolves `../meta-gl`
- **Networking:** [ENet](http://enet.bespin.org/) (vendored directly at `third_party/enet`) —
  the realtime transport of every `Microsoft::Xna::Framework::Net` session (UDP on the LAN, carried
  through the service relay online); libcurl (TLS HTTPS and WebSockets to the CNA service);
  optional libopus for network voice
- **Utility/runtime layer:** `sharp-runtime`
- **Build system:** CMake
- **Tests:** GoogleTest (`CnaTests` target)

## 9. ⚡ Getting Started

### Prerequisites (Linux)

- CMake 3.20+
- C++23-capable compiler (GCC 12+ or Clang 15+)
- Dependency directories available to CMake:
    - `../sharp-runtime`
    - `../easy-gl` and `../meta-gl` (needed for the `OPENGLES2`/`OPENGLES3`/`OPENGL33`/
      `WEBGL1`/`WEBGL2` renderers)
- SDL3, SDL3_image, and SDL3_mixer are built from vendored submodules by default — no system SDL packages required.
- FFmpeg is optional. `CNA_ENABLE_VIDEO=AUTO` (the default) enables video decoding when
  `libavcodec`, `libavformat`, `libavutil` and `libswresample` development packages are present;
  use `OFF` for a game that does not need video, or `ON` to require them. The XNA video types remain
  available in all three modes; see [docs/video-backend.md](docs/video-backend.md).

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

### Build (Linux — OPENGLES3 renderer, default)

```bash
git submodule update --init --recursive
cmake -S . -B build -DCNA_GRAPHICS_RENDERER=OPENGLES3
cmake --build build --target CnaTests
```

### Build (Linux — SDL_RENDERER renderer)

```bash
git submodule update --init --recursive
cmake -S . -B build-sdlrenderer -DCNA_GRAPHICS_RENDERER=SDL_RENDERER
cmake --build build-sdlrenderer --target CnaTests
```

### Build (Windows — SDL_RENDERER renderer, vendored SDL)

On Windows the `SDL_RENDERER` renderer is selected automatically when no renderer is
explicitly specified. SDL is built from the vendored submodule — no pre-built SDL
binaries or `CMAKE_PREFIX_PATH` needed.

```bash
git submodule update --init --recursive
cmake -S . -B build-win -DCNA_GRAPHICS_RENDERER=SDL_RENDERER
cmake --build build-win --target CnaTests
```

### Build (Linux → Windows cross-compilation with MinGW-w64)

```bash
# Install cross toolchain
sudo apt install mingw-w64

git submodule update --init --recursive
cmake -S . -B build-windows \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_RENDERER=SDL_RENDERER
cmake --build build-windows --target CnaTests
```

### Build (macOS)

```bash
brew install ccache ffmpeg
git submodule update --init

cmake -S . -B cmake-build-macos -DCNA_GRAPHICS_RENDERER=SDL_RENDERER
cmake --build cmake-build-macos --target CnaTests --parallel
```

`METAL` is available here as well (`-DCNA_GRAPHICS_RENDERER=METAL`); its own supported contract is
narrower than "it builds" — see [`docs/metal-renderer.md`](docs/metal-renderer.md).

### Build (macOS → iOS / iPadOS cross-compilation)

Requires a macOS host with Xcode. This produces a final-linked `cna_ios_smoke.app` for a device
or simulator; the Apple workflow also launches its one-frame `Game` path in the simulator. This
is not evidence for a physical device or correct pixels/input/audio/storage — see
[`docs/apple-platforms.md`](docs/apple-platforms.md) for the exact boundary.

```bash
cmake -S . -B cmake-build-ios \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ios.cmake \
      -DCNA_GRAPHICS_RENDERER=SDL_RENDERER \
      -DCNA_BUILD_TESTS=OFF -DCNA_BUILD_EXAMPLES=OFF
cmake --build cmake-build-ios --parallel

# Simulator: add -DCNA_IOS_SIMULATOR=ON (use a separate build directory).
# Device deployment needs the Xcode generator and a team id:
#   -G Xcode -DCNA_APPLE_DEVELOPMENT_TEAM=<TEAMID>
```

### Optional: use system-installed SDL

If you prefer to link against system-installed SDL3 packages instead of the
vendored submodules, pass `-DCNA_USE_SYSTEM_SDL=ON`:

```bash
cmake -S . -B build -DCNA_USE_SYSTEM_SDL=ON -DCNA_GRAPHICS_RENDERER=SDL_RENDERER
cmake --build build --target CnaTests
```

This calls `find_package(SDL3 REQUIRED)`, `find_package(SDL3_image REQUIRED)`,
and `find_package(SDL3_mixer REQUIRED)` and requires those packages to be present
on the system (e.g. installed via your package manager).

### Other renderers

```bash
cmake -S . -B build -DCNA_GRAPHICS_RENDERER=OPENGL33
cmake -S . -B build -DCNA_GRAPHICS_RENDERER=VULKAN
```

### Build (Windows cross-compilation — D3D9 renderer)

A native Direct3D 9 renderer, Windows-only (hard-`FATAL_ERROR`-gated at configure time, same as
`D3D11`/`D3D12`), targeting real XNA 4.0 pixel authenticity rather than just feature parity —
see [`docs/directx9-renderer.md`](docs/directx9-renderer.md) for what that means and why. Developed and
verified on this repo's own Debian dev machine via the same MinGW-w64 cross toolchain the other
Windows renderers use, tested locally through Wine + DXVK (`scripts/run-wine-dxvk9.sh`). See
[`docs/directx9-renderer.md`](docs/directx9-renderer.md) and [`plans/plan_dx9.md`](plans/plan_dx9.md) for full detail.

```bash
# Install cross toolchain (same package D3D11/D3D12/SDL_RENDERER's own Windows cross-build uses)
sudo apt install mingw-w64

git submodule update --init --recursive
cmake -S . -B cmake-build-d3d9 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_RENDERER=D3D9 \
      -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-d3d9 --target CnaTests --parallel
```

Running the resulting `.exe`s needs a Wine + DXVK dev-loop, in a prefix separate from D3D11's own
(`docs/directx9-renderer.md` has full setup steps); CTest wires this in automatically:

```bash
ctest --test-dir cmake-build-d3d9 -L D3D9 --output-on-failure
```

### Build (Windows cross-compilation — D3D11 renderer)

A native Direct3D 11 renderer, Windows-only (hard-`FATAL_ERROR`-gated at configure time on any other
`CMAKE_SYSTEM_NAME`). Developed and verified on this repo's own Debian dev machine via the same
MinGW-w64 cross toolchain `SDL_RENDERER` uses, tested locally through Wine + DXVK
(`scripts/run-wine-dxvk.sh`) before any real-Windows verification pass. See
[`docs/directx11-renderer.md`](docs/directx11-renderer.md) and [`plans/plan_dx.md`](plans/plan_dx.md) for full detail.

```bash
# Install cross toolchain (same package SDL_RENDERER's own Windows cross-build uses)
sudo apt install mingw-w64

git submodule update --init --recursive
cmake -S . -B cmake-build-d3d11 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_RENDERER=D3D11 \
      -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-d3d11 --target CnaTests --parallel
```

Running the resulting `.exe`s needs a Wine + DXVK dev-loop (`docs/directx11-renderer.md` has full setup
steps); CTest wires this in automatically:

```bash
ctest --test-dir cmake-build-d3d11 -R D3D11 --output-on-failure
```

### Build (Windows cross-compilation — D3D12 renderer)

A native Direct3D 12 renderer, Windows-only (hard-`FATAL_ERROR`-gated at configure time, same as
`D3D11`). Also developed and verified on this repo's own Debian dev machine via the same MinGW-w64
cross toolchain, but tested locally through Wine + **vkd3d-proton** (`scripts/run-wine-vkd3d.sh`),
not DXVK — D3D12 needs a different Windows-D3D-to-Vulkan translation layer than D3D11, with its own
dedicated Wine prefix. Every check currently runs **off-screen only** — swap-chain presentation is
a known, real, unresolved gap on this dev loop (see `docs/directx12-renderer.md`). See
[`docs/directx12-renderer.md`](docs/directx12-renderer.md) and [`plans/plan_dx.md`](plans/plan_dx.md) for full detail.

```bash
# Install cross toolchain (same package D3D11/SDL_RENDERER's own Windows cross-build uses)
sudo apt install mingw-w64

git submodule update --init --recursive
cmake -S . -B cmake-build-d3d12 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_RENDERER=D3D12 \
      -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-d3d12 --target CnaTests --parallel
```

Running the resulting `.exe`s needs a Wine + vkd3d-proton dev-loop, in a prefix separate from
D3D11's own (`docs/directx12-renderer.md` has full setup steps); CTest wires this in automatically:

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

| Platform | Compiler | Renderer | Status |
|----------|----------|---------|--------|
| Linux x86_64 | GCC 12+ | OPENGLES3, SDL_RENDERER | ✅ |
| Linux x86_64 | Clang 15+ | OPENGLES3, SDL_RENDERER | ✅ |
| Windows x86_64 | MSVC 2022 | SDL_RENDERER | planned |
| Windows x86_64 (native) | MinGW-w64 | SDL_RENDERER | planned |
| Linux → Windows (cross) | MinGW-w64 | SDL_RENDERER | ✅ verified building + full test suite under Wine |
| Linux → Windows (cross) | MinGW-w64 | D3D9 | ✅ verified building + `D3D9` CTest suite (14 tests) under Wine+DXVK on a real GPU — 0/31 oracle scenes diverge from real XNA 4.0 at `--tolerance 0`; real Windows hardware verification still open, see `docs/directx9-renderer.md` |
| Linux → Windows (cross) | MinGW-w64 | D3D11 | ✅ verified building + `D3D11` CTest suite (6 tests, 96+ checks) under Wine+DXVK on a real GPU — real Windows hardware verification still open, see `docs/directx11-renderer.md` |
| Linux → Windows (cross) | MinGW-w64 | D3D12 | ✅ verified building + `D3D12` CTest suite (1 test, 80/80 checks, off-screen only) under Wine+vkd3d-proton on a real GPU — swap-chain presentation and real Windows hardware verification both still open, see `docs/directx12-renderer.md` |
| Web (Emscripten) | emcc/Clang (emsdk) | WEBGL2 | ✅ verified building + running under Node.js (as `EASYGL`, prior to the `plans/plan_glbackends.md` rename — not yet re-verified under its new `WEBGL2` name/build flags) |
| Android (NDK) | Clang (NDK 29/30) | OPENGLES3 | ✅ verified building + running on a real x86_64 emulator (as `EASYGL`, prior to the `plans/plan_glbackends.md` rename) |

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
- **Abstraction design:** Gameplay-facing rendering APIs (`GraphicsDevice`, `SpriteBatch`, `Texture2D`) delegate to renderer interfaces instead of exposing low-level renderer objects.
- **Separation of concerns:** Public framework API, internal contracts, and renderer implementations are physically separated in directory structure and ownership.
- **Renderer-oriented architecture:** Renderer can be swapped at build-time with a single CMake option while keeping high-level game code stable.
- **Performance-minded C++ implementation:** Native code path enables tighter control over memory, lifetime, and rendering behavior than managed runtime abstractions.

## 12. 🛣 Roadmap

- Continue expanding XNA API coverage and behavior parity (incremental, class-by-class).
- Extend compiled XNA Effect Framework bytecode beyond the completed `FNA3D` implementation. Each
  additional renderer remains gated off until it passes the shared reflection, state, lifecycle,
  3D, and SpriteBatch conformance contract; fixed-function/2D-only renderers stay explicitly
  unsupported.
- Continue testing the existing `.xnb` content readers against Microsoft reference assets.
- Close the remaining named architecture-decision gaps (SDL_Renderer `TextureAddressMode::Wrap`/`Mirror`, SDL_Renderer `Texture3D`/`TextureCube` construction, EasyGL non-`Color` `SurfaceFormat` GPU forwarding, `Texture3D`/`TextureCube` sampler-bind architecture) — see `NEXT.md` §5 and `docs/graphics-renderer-feature-matrix.md`.
- Strengthen cross-platform execution targets and validation coverage.

## 13. 📜 License

CNA is licensed under the Microsoft Public License (Ms-PL). See the [LICENSE](LICENSE) file for details.

Portions of CNA are derived from or based on FNA, which is also licensed under the Microsoft Public License (Ms-PL).
