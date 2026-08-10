# CNA physical module layout

Since the Phase-3 physical modularization (2026-08-10, implemented on `feature/physical-modules`
and promoted to `develop` the same day as `3ecbbce72`), the
repository is a module-oriented monorepo: every subsystem and every renderer implementation
family physically owns `modules/<name>/{CMakeLists.txt,include/,src/,tests/}` (a subdirectory
is omitted only when genuinely empty). Consumer include spelling is unchanged — each module's
`include/` root reproduces the public `Microsoft/...` / `CNA/...` paths beneath it, so
`#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"` still works everywhere; only the
physical location of the header changed. The root `CMakeLists.txt` composes the modules via
`add_subdirectory(modules)`; `modules/CMakeLists.txt` owns the shared build-flag surface, the
umbrella targets and the physical source-partition validator.

## Framework modules

| Module root | Kind | CMake target (alias) | Direct CNA deps (PUBLIC unless noted) | SharpRuntime components |
|---|---|---|---|---|
| modules/core | base | `cna_core` (`CNA::Core`); `cna_core_headers` (`CNA::CoreHeaders`, header-only surface) | — (SDL3 private) | Core.Base |
| modules/math | base | `cna_math` (`CNA::Math`) | core-headers (headers-only: NOXNA marker) | Core.Base |
| modules/runtime | base | `cna_runtime` (`CNA::Runtime`) | graphics, input, content, audio, media, core, math | Core.Base, IO |
| modules/graphics | base | `cna_graphics_core` (`CNA::GraphicsCore`) | math, core; private: input (cycle), selected renderer (factory edge) | Core.Base, IO, Collections.Core, Text |
| modules/input | base | `cna_input` (`CNA::Input`) | graphics, math, core | Core.Base |
| modules/audio | base | `cna_audio` (`CNA::Audio`) | core, math; private: media (cycle), input (dispatcher pump) | Core.Base, IO, Runtime |
| modules/media | base | `cna_media` (`CNA::Media`) | audio, graphics; private: input (dispatcher surface) | Core.Base, IO |
| modules/content | base | `cna_content` (`CNA::Content`) | graphics, audio, media, math, core | Core.Base, IO |
| modules/storage | base | `cna_storage` (`CNA::Storage`) | core-headers (headers-only: PlayerIndex/NOXNA/PathContainment) | Core.Base, IO, Runtime, Threading |
| modules/devices | base | `cna_devices` (`CNA::Devices`) | runtime, graphics, core, math | Core.Base |
| modules/devices-ext | extension | `cna_devices_ext` (`CNA::DevicesExt`) | runtime, graphics, core, math (never the devices base) | Core.Base |
| modules/graphics-ext | extension | `cna_graphics_ext` (`CNA::GraphicsExt`) | graphics | Core.Base |
| modules/gamer-services | base (optional, `CNA_ENABLE_NET`) | `CNA_GamerServices` (`CNA::GamerServices`) | runtime, storage | Core.Base, IO, Collections.Core, Globalization, Runtime, Threading |
| modules/net | base (optional, `CNA_ENABLE_NET`) | `CNA_Net` (`CNA::Net`) | gamer-services, enet | Core.Base, IO, Collections.Core, Runtime, Threading |

Umbrellas (defined in `modules/CMakeLists.txt`):

- **`CNA`** — the historical full-framework INTERFACE umbrella: all modules + the selected
  renderer + the shared build flags. Existing `target_link_libraries(game CNA)` consumers keep
  working unchanged and receive the aggregate include surface through target composition.
- **`cna_noxna` / `CNA::NoXna`** — compatibility umbrella; the former STATIC library's
  implementation is now the graphics-ext module, and the umbrella composes
  `CNA::GraphicsExt` + `CNA::DevicesExt` as an INTERFACE.
- **`cna_build_flags` / `CNA::BuildFlags`** — the shared PUBLIC compile definitions. It no
  longer carries any include directory; the former global `include/` root does not exist.

Every module test tree preserves its former `tests/`-relative mirror path (e.g.
`modules/audio/tests/Microsoft/Xna/Framework/Audio/...`), and `CnaTests` remains the single
corpus executable, globbing `modules/*/tests` + `modules/renderers/*/tests` + `tests/`.
Top-level `tests/` retains only the shared fixture assets (`tests/assets`, addressed by
runtime-literal paths) and the cross-module minimal-link probes (`tests/modules`).

## Renderer modules — modules/renderers/

38 implementation families carry the 41 public renderer identities (the easygl family
implements the four GL-profile identities OPENGLES/OPENGL33/WEBGL1/WEBGL2). Identities are
pinned by `scripts/check_renderer_identities.py` over `CNA/GraphicsRendererType.hpp` (core
module) + `cmake/RendererSelection.cmake`; family directories are implementation structure,
not identities. Exactly one family's `${RENDERER_TARGET}` is configured per build
(`RENDERER_DIR = modules/renderers/<family>`); each family's `CMakeLists.txt` owns its
specific SDK links, carried over branch-for-branch from the former central manifest.

ascii, bgfx, canvas, d3d10, d3d11, d3d12, d3d9, diligent, direct2d, dx1, dx2, dx3, dx5, dx6,
dx7, dx8, easygl, freedirect, gdi, glide, headless, html-dom, llgl, magnum, metal, opengl1,
opengl2, opengl4, opengles1, sdl-gpu, sdl-renderer, skia, software, sokol, stub, vulkan,
webgpu, wicked.

Common helper targets (deliberate sharing, not public identities):

- **modules/renderers/common/d3d** — `cna_renderer_d3dcommon`, consumed by the d3d11
  and d3d12 families only. D3D9 and D3D10 are independent (their own format/state mapping;
  verified mechanically by include and link audit during the physical move).
- **sdl-renderer as ASCII's core** — under `CNA_GRAPHICS_RENDERER=ASCII` the sdl-renderer
  module builds its sources as `cna_renderer_sdl_renderer_core` (the accepted
  decorator design); under SDL_RENDERER it builds the renderer itself.
- **software 2D units in GDI** — under GDI the software module publishes its eight shared
  CPU-2D translation units (`CNA_GDI_SOFTWARE_SOURCES`) plus `cna_renderer_software_headers`;
  they compile into the GDI archive. Physical ownership stays with the software module — this
  is the one documented target-membership exception to location==ownership.
- **d3d9 effect sub-target** — `cna_renderer_d3d9_effect` (the isolated
  d3dcompiler-carrying custom-ShaderEffect path) lives inside modules/renderers/d3d9.
- **metal + glide header interfaces** — `cna_renderer_metal_headers` /
  `cna_renderer_glide_headers` are defined unconditionally: those families' policy/ABI test
  suites deliberately compile into the CnaTests corpus on every renderer, so their
  `CNA/Internal/Renderers/<X>` policy headers stay reachable in every configuration.

## Intentional cycles (unchanged from the accepted target graph)

- **graphics ↔ input** — XNA semantics (GraphicsDevice updates TouchPanel/Mouse binding;
  MouseCursor builds on Texture2D).
- **audio ↔ media** — FrameworkDispatcher pumps MediaPlayer; MediaPlayer plays through the
  mixer.
- **graphics ↔ selected renderer** — the factory edge plus the unconditional reverse edges
  (`${RENDERER_TARGET}` → graphics/core/math), declared by
  `cna_renderer_common_setup()` in `modules/renderers/CMakeLists.txt`.

## Validators

- `modules/CMakeLists.txt` — physical source-partition validator: every production TU must
  live inside a declared module's `src`/`tests` tree, and the legacy global `src/`/`include/`
  roots must not reappear (configure-time FATAL_ERROR).
- `modularization/tools/check_include_reachability.py` — every `#include "CNA/..."`
  / `"Microsoft/..."` in every module TU (transitively through headers) must resolve through
  the declared module graph; config-gated renderer includes are attributed to their renderer.
- `cmake/Tests/ModuleProbes.cmake` — minimal-link probes for math, core, graphics, content,
  runtime, input, audio, media, storage, devices, devices-ext, graphics-ext, the NoXna
  composition umbrella and net, with per-probe link-closure gates
  (`scripts/check_module_link_closure.py`) and the HEADLESS native-SDK-free /
  VULKAN closure configuration gates.
- `scripts/check_renderer_identities.py` — pins the 41 public renderer identities.
