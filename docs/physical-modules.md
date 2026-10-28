# CNA physical module layout

Since the Phase-3 physical modularization (2026-08-10, implemented on `feature/physical-modules`
and promoted to `develop` the same day as `3ecbbce72`), the
repository is a module-oriented monorepo: every subsystem and every renderer implementation
family physically owns `modules/<name>/{CMakeLists.txt,include/,src/,tests/,examples/}` (a
subdirectory is omitted only when genuinely empty; `examples/` joined the owned areas when
the tracked examples moved into their owning modules, 2026-08-10). Consumer include spelling
is unchanged — each module's
`include/` root reproduces the public `Microsoft/...` / `CNA/...` paths beneath it, so
`#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"` still works everywhere; only the
physical location of the header changed. The root `CMakeLists.txt` composes the modules via
`add_subdirectory(modules)`; `modules/CMakeLists.txt` owns the shared build-flag surface, the
umbrella targets and the physical source-partition validator.

## Framework modules

| Module root | Kind | CMake target (alias) | Direct CNA deps (PUBLIC unless noted) | SharpRuntime components |
|---|---|---|---|---|
| modules/core | base | `cna_core` (`CNA::Core`); `cna_core_headers` (`CNA::CoreHeaders`, header-only surface) | — (SDL3 private) | Core.Base |
| modules/diagnostics | optional observation foundation (`CNA_DIAGNOSTICS`) | `cna_diagnostics` (`CNA::Diagnostics`) | — | — |
| modules/inspector | optional development tool (`CNA_BUILD_INSPECTOR`) | `cna_inspector` (`CNA::Inspector`), `cna-inspector` executable; deliberately outside `CNA` umbrella | diagnostics, core-headers; private: OS sockets/threads | — |
| modules/math | base | `cna_math` (`CNA::Math`) | core-headers (headers-only: CNAEXT marker) | Core.Base |
| modules/design | opt-in tooling | `cna_design` (`CNA::Design`) | math | ComponentModel (and its public closure) |
| modules/runtime | base | `cna_runtime` (`CNA::Runtime`) | graphics, input, content, audio, media, core, math | Core.Base, IO |
| modules/graphics | base | `cna_graphics_core` (`CNA::GraphicsCore`) | math, core; private: input (cycle), selected renderer (factory edge) | Core.Base, IO, Collections.Core, Text |
| modules/input | base | `cna_input` (`CNA::Input`) | graphics, math, core | Core.Base |
| modules/audio | base | `cna_audio` (`CNA::Audio`) | core, math; private: media (cycle), input (dispatcher pump) | Core.Base, IO, Runtime |
| modules/media | base | `cna_media` (`CNA::Media`) | audio, graphics; private: input (dispatcher surface) | Core.Base, IO |
| modules/video-ffmpeg | optional backend (`CNA_ENABLE_VIDEO`) | `cna_video_ffmpeg` (`CNA::VideoFfmpeg`) | build flags; private: media contract headers and system FFmpeg, linked into media only when enabled | Core.Base |
| modules/content | base | `cna_content` (`CNA::Content`) | graphics, audio, media, math, core | Core.Base, IO |
| modules/storage | base | `cna_storage` (`CNA::Storage`) | core-headers (headers-only: PlayerIndex/CNAEXT/PathContainment) | Core.Base, IO, Runtime, Threading |
| modules/devices | base | `cna_devices` (`CNA::Devices`) | runtime, graphics, core, math | Core.Base |
| modules/devices-ext | extension | `cna_devices_ext` (`CNA::DevicesExt`) | runtime, graphics, core, math (never the devices base) | Core.Base |
| modules/phone | base | `cna_phone` (`CNA::Phone`) | runtime, core-headers | Core.Base, Collections.Core |
| modules/graphics-ext | extension | `cna_graphics_ext` (`CNA::GraphicsExt`) | graphics | Core.Base |
| modules/gamer-services | base (optional, `CNA_ENABLE_NET`) | `CNA_GamerServices` (`CNA::GamerServices`) | runtime, storage | Core.Base, IO, Collections.Core, Globalization, Runtime, Threading |
| modules/net | base (optional, `CNA_ENABLE_NET`) | `CNA_Net` (`CNA::Net`) | gamer-services, enet | Core.Base, IO, Collections.Core, Runtime, Threading |

The `CNA::Runtime` target above remains the runtime module. The unrelated C++ class
`CNA::Runtime` and its `RuntimeOptions` in `CNA/Misc.hpp` were retired for
CNA-DEMOS-ISSUE-150: their five declared methods never had definitions, so no consumer could
link them. Remove that obsolete include and use `Microsoft::Xna::Framework::Game` with
`GraphicsDeviceManager` for the game lifecycle. This does not introduce a replacement
five-method initialization API or change the module target.

Umbrellas (defined in `modules/CMakeLists.txt`):

- **`CNA`** — the historical full-runtime INTERFACE umbrella: runtime modules + the selected
  renderer + the shared build flags. Existing `target_link_libraries(game CNA)` consumers keep
  working unchanged and receive the aggregate runtime include surface through target composition.
  The design/tooling-only `CNA::Design` module is deliberately opt-in so ordinary games do not
  link converter registration or ComponentModel implementation code.
- **`cna_cnaext` / `CNA::CnaExt`** — compatibility umbrella; the former STATIC library's
  implementation is now the graphics-ext module, and the umbrella composes
  `CNA::GraphicsExt` + `CNA::DevicesExt` as an INTERFACE.
- **`cna_build_config` / `CNA::BuildConfig`** — the shared PUBLIC compile definitions and
  language requirements. It carries no include directory; the former global `include/` root does
  not exist. Private compiler policy lives in `CNA::ProjectOptions`,
  `CNA::Instrumentation`, and `CNA::LinkerOptions`.

Every module test tree preserves its former `tests/`-relative mirror path (e.g.
`modules/audio/tests/Microsoft/Xna/Framework/Audio/...`), and `CnaTests` remains the single
corpus executable, globbing `modules/*/tests` + `modules/renderers/*/tests` + `tests/`.
Top-level `tests/` retains only the shared fixture assets (`tests/assets`, addressed by
runtime-literal paths) and the cross-module minimal-link probes (`tests/modules`).

## Renderer modules — modules/renderers/

8 implementation families carry the 10 public renderer identities (the easygl family
implements the three GL-profile identities OPENGLES3/OPENGL33/WEBGL2).
Identities are
pinned by `scripts/check_renderer_identities.py` over `CNA/GraphicsRendererType.hpp` (core
module) + `cmake/RendererSelection.cmake`; family directories are implementation structure,
not identities. Exactly one family's `${RENDERER_TARGET}` is configured per build
(`RENDERER_DIR = modules/renderers/<family>`); each family's `CMakeLists.txt` owns its
specific SDK links, carried over branch-for-branch from the former central manifest.

directx9, easygl, fna3d, headless, sdl-gpu, software, stub, webgpu.

Common helper targets (deliberate sharing, not public identities):

- **modules/renderers/common/mojoshader** — `cna_renderer_mojoshader_effect`, the
  MojoShader-to-CNA translation shared by every family that executes compiled XNA effects.
- **d3d9 effect sub-target** — `cna_renderer_d3d9_effect` (the isolated
  d3dcompiler-carrying custom-ShaderEffect path) lives inside modules/renderers/directx9.

## Module examples — modules/<name>/examples/

Examples follow the same ownership model as sources and tests (module-examples campaign,
implemented on `feature/module-examples` and promoted to `develop` on 2026-08-10 as
`675e04c7a`):

- **Subsystem examples** live with the framework module whose API they primarily
  demonstrate: `modules/graphics/examples/` (demo_2d, house3d_demo, the renderer-agnostic
  conformance fixtures, the cross-renderer diagnostic scene/comparator + Emscripten
  benchmark, and the shared support headers under `common/` — PixelTestGame, ScreenshotEXT,
  SimpleFontEXT, ViewSpaceFogRef), `modules/audio/examples/` (demo_sound, demo_xact),
  `modules/input/examples/` (demo_input, input_smoke), `modules/devices/examples/`
  (demo_devices incl. its Android Gradle/CMake subproject),
  `modules/gamer-services/examples/` (demo_avatar + the avatar variant demos, the
  GamerServices demo fleet, the avatar integration fixtures) and `modules/net/examples/`
  (the Microsoft.Xna.Framework.Net demo fleet incl. demo_net_avatar_sync).
- **Renderer examples** live with their implementation family under
  `modules/renderers/<family>/examples/`, ownership following the implementation, not the
  public profile (the easygl family's suite serves OPENGLES3/OPENGL33/WEBGL2; the
  profile stays visible in target/ctest names).
- **Extension examples** live with their extension module:
  `modules/graphics-ext/examples/` (ASCII oracles and depth/CRT effect demos — all
  CNAEXT).
- **Registration is module-local**: each owning module's `CMakeLists.txt` enters
  `examples/CMakeLists.txt` via `add_subdirectory(examples)`; the historical configure
  conditions moved into those files unchanged, and example/test executables still land at
  the build root (`CMAKE_RUNTIME_OUTPUT_DIRECTORY`), where they have always been.
  `CNA_GRAPHICS_EXAMPLES_DIR` (set in `modules/CMakeLists.txt`) is how family registrations
  compile the graphics-owned shared fixtures per renderer and how consumers reach the
  `common/` support headers. Cross-family reuse (e.g. a family building
  EasyGL-authored golden fixtures) is spelled as an explicit
  `${CMAKE_SOURCE_DIR}/modules/renderers/<owner>/examples/` path. Module-registered tests
  get the root's headless-safe skip convention through `cna_apply_skip_convention()`
  (cmake/TestHelpers.cmake).
- **Top-level `examples/` holds exactly one thing**: `examples/golden/`, the cross-renderer
  golden oracle corpus (SHARED_EXAMPLE_ASSET). The easygl
  suites load those PNGs at run time via CWD-relative `"examples/golden/*.png"` literals
  from the repo-root test working directory, so the corpus deliberately stays at that
  documented shared location.

**Adding a new example**: a module-specific example belongs to its owning module's
`examples/` (register it in that module's `examples/CMakeLists.txt`); a renderer-specific
example belongs to its renderer family's `examples/`; only a genuinely cross-module
integration example with no single owner may sit at repository level, with its
justification documented. The top-level `examples/` directory must not become a
miscellaneous dumping ground again.

## Intentional cycles (unchanged from the accepted target graph)

- **graphics ↔ input** — XNA semantics require `GraphicsDevice` to update the TouchPanel and Mouse
  bindings, while the public `MouseCursor::FromTexture2D` surface points back to graphics. Moving
  propagation into `Game` would break standalone `GraphicsDevice` use, and a registration hook
  could disappear when its object file is not pulled from a static archive.
- **audio ↔ media** — `FrameworkDispatcher` owns and pumps the `MediaPlayer` event state, while
  `MediaPlayer` plays through the audio mixer. A lazy bridge would move upstream-owned state and
  add global registration and initialization-order hazards without reducing a consumer-visible
  dependency.
- **media → video-ffmpeg (optional)** — `cna_media` always owns the XNA `Video`/`VideoPlayer`
  surface and a link-complete unavailable implementation. `CNA_ENABLE_VIDEO=AUTO/ON` may replace
  that implementation with `cna_video_ffmpeg`; `OFF` leaves the entire Game/audio link graph free
  of FFmpeg. See [video-backend.md](video-backend.md).
- **graphics ↔ selected renderer** — the explicit renderer-factory call is the undefined-symbol
  anchor that makes a static linker pull the selected renderer archive. Self-registration alone
  cannot pull its registrar object; repairing that would require whole-archive flags or another
  anchor. The reverse edges (`${RENDERER_TARGET}` → graphics/core/math) are shared implementation
  dependencies declared by `cna_renderer_common_setup()` in `modules/renderers/CMakeLists.txt`.

## Validators

- `modules/CMakeLists.txt` — physical source-partition validator: every production TU must
  live inside a declared module's `src`/`tests`/`examples` tree, and the legacy global
  `src/`/`include/` roots must not reappear (configure-time FATAL_ERROR).
- `cmake/Tests/ModuleProbes.cmake` — minimal-link probes for math, design, core, graphics, content,
  runtime, input, audio, media, storage, devices, devices-ext, graphics-ext, the CnaExt
  composition umbrella and net, with per-probe link-closure gates
  (`scripts/check_module_link_closure.py`) and the HEADLESS native-SDK-free closure
  configuration gate.
- `scripts/check_renderer_identities.py` — pins the 10 public renderer identities.
