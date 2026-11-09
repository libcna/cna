# Removed renderers

Renderer identities CNA no longer carries. CNA intentionally maintains a curated renderer set:
a renderer is added only when it provides meaningful platform coverage, compatibility value,
architectural value, or a capability not reasonably covered by the existing set, and one that
stops meeting that bar is retired. None of these was retired because the work was bad. The current
set and implementation mapping are in [`renderer-registry.md`](renderer-registry.md).

**The code is not gone.** Each entry names the commit that removed it; the parent of that commit
holds the whole implementation (`git show <commit>^:modules/renderers/<family>/...`, or
`git checkout <commit>^ -- modules/renderers/<family>`), together with its documents, plans and
probes. What Git does *not* preserve is the third-party project each renderer wrapped, so every
entry below records the exact dependency coordinate it used, or says explicitly when the checkout
was never pinned. That pairing — CNA's code in Git history, the dependency's coordinates here — is
the archive.

**A revert is a reference, not a patch.** `IGraphicsRenderer` changes often. Beyond a few months a
removal commit is a specification to port forward, not a patch to apply. Expect to read it, not
`git revert` it.

**No resurrection without a fresh owner instruction.** No surviving renderer is an alias for a
retired one.

## Retired identity values

A retired identity's numeric C ABI value was once published as a `CNA_GRAPHICS_RENDERER_*`
constant and is **permanently reserved**: it is never assigned to another renderer, and surviving
identities are never renumbered to close the gap. The retired constants themselves are no longer
published as selectable identities; `modules/c-api/include/CNA/C/graphics.h` lists the gaps in the
`CNA_GRAPHICS_RENDERER_MAXIMUM` contract. The next new identity takes value **52**.

The table is repeated in `cmake/RendererIdentities.cmake` for configure-time refusal and in
`RetiredGraphicsRenderers` (`modules/core/include/CNA/Internal/RetiredGraphicsRenderers.hpp`) for
run-time diagnostics. Every selection route — `CNA_GRAPHICS_RENDERER`, a member of
`CNA_GRAPHICS_RENDERERS`, `CNA_RENDERER_<X>=ON`, `GraphicsRendererSelection`, the environment route
and `cna_graphics_renderer_set_preferred_by_name_ext` — refuses a retired name by name and reserved
value; numeric C routes return `CNA_RESULT_INVALID_ARGUMENT` with the same retired-specific
diagnostic. The predicate `cna_graphics_renderer_try_parse_name_ext` deliberately treats a retired
name as unrecognized instead of selecting it. `scripts/check_renderer_identities.py` holds both code
tables and the table below to the pinned values and fails on reuse, disagreement or resurrection.

`SDL_GPU` is not a retired identity and therefore has no separate reserved value: it is a refused
former spelling of the live `SDLGPU` identity. `SDL_RENDERER` is the distinct retired 2D renderer
whose value is 1. The `Removed` column is the commit date.

| Value | Identity | Removed | Removal commit | Reason |
|---:|---|---|---|---|
| 1 | `SDL_RENDERER` | 2026-10-14 | `86b8152f2` | SDL's 2D API: 2D-only, it refused every 3D call. |
| 2 | `OPENGLES2` | 2026-10-07 | `1cac65958` | The GLSL ES 1.00 / ES 2.0 generation of EasyGL. |
| 5 | `WEBGL1` | 2026-10-07 | `1cac65958` | The browser form of the same ES 2.0 generation. |
| 7 | `BGFX` | 2026-09-17 | `fc1b6a537` | Portable-abstraction middleware over APIs CNA drives directly. |
| 8 | `VULKAN` | 2026-10-13 | `fe5e25994` | A second, hand-written native Vulkan renderer beside `SDLGPU`. |
| 10 | `MAGNUM` | 2026-09-17 | `fc1b6a537` | Portable-abstraction middleware. |
| 14 | `DIRECTX11` | 2026-10-13 | `def6f7747` | A native Direct3D 11 renderer with the shared `D3DCommon` core and offline HLSL. |
| 15 | `DIRECTX12` | 2026-10-07 | `1cac65958` | A second modern Direct3D renderer, never validated beyond WARP and vkd3d-proton. |
| 16 | `DIRECT2D` | 2026-09-27 | `0f96ea703` | A Windows 2D-only renderer. |
| 17 | `CANVAS` | 2026-10-07 | `1cac65958` | A GPU-free browser 2D path (`canvas.getContext('2d')`). |
| 18 | `HTML_DOM` | 2026-09-28 | `261cb5f7f` | A specialized browser DOM/CSS 2D path (pooled `<div>` sprites). |
| 19 | `SKIA` | 2026-08-30 | `b6b275cb0` | 2D-only by construction, so it can never satisfy `IGraphicsRenderer`; the heaviest, unpinned dependency. |
| 20 | `BLEND2D` | 2026-09-17 | `fc1b6a537` | A 2D vector rasterizer. |
| 21 | `FREEDIRECT` | 2026-09-27 | `0f96ea703` | A DirectDraw-shaped 2D renderer over an unpinned sibling checkout. |
| 23 | `DIRECTX1` | 2026-09-17 | `fc1b6a537` | A legacy/historical API. |
| 24 | `DIRECTX2` | 2026-09-17 | `fc1b6a537` | A legacy/historical API. |
| 25 | `DIRECTX3` | 2026-09-17 | `fc1b6a537` | A legacy/historical API. |
| 26 | `DIRECTX5` | 2026-09-17 | `fc1b6a537` | A legacy/historical API. |
| 27 | `DIRECTX6` | 2026-09-17 | `fc1b6a537` | A legacy/historical API. |
| 28 | `DIRECTX7` | 2026-09-17 | `fc1b6a537` | A legacy/historical API. |
| 29 | `DIRECTX8` | 2026-09-17 | `fc1b6a537` | A legacy/historical API. |
| 30 | `DIRECTX10` | 2026-09-17 | `fc1b6a537` | A legacy/historical API. |
| 32 | `OPENGLES1` | 2026-09-17 | `fc1b6a537` | A legacy fixed-function API. |
| 33 | `OPENGL4` | 2026-09-28 | `261cb5f7f` | A second desktop GL implementation overlapping EasyGL. |
| 34 | `OPENGL1` | 2026-09-17 | `fc1b6a537` | A legacy fixed-function API. |
| 35 | `OPENGL2` | 2026-09-17 | `fc1b6a537` | A legacy compatibility-profile API. |
| 36 | `WICKED` | 2026-09-17 | `fc1b6a537` | Portable-abstraction middleware (an engine's RHI). |
| 37 | `SOKOL` | 2026-09-17 | `fc1b6a537` | Portable-abstraction middleware. |
| 38 | `DILIGENT` | 2026-09-17 | `fc1b6a537` | Portable-abstraction middleware. |
| 39 | `GLIDE` | 2026-09-17 | `fc1b6a537` | A legacy/historical API. |
| 40 | `GDI` | 2026-09-28 | `261cb5f7f` | A Windows-specific duplicate 2D path (Software's CPU 2D presented through GDI). |
| 41 | `LLGL` | 2026-09-17 | `fc1b6a537` | Portable-abstraction middleware. |
| 42 | `METAL` | 2026-10-13 | `22842c107` | A native Metal renderer; `SDLGPU` reaches Metal through SDL. |
| 44 | `SVG_DOM` | 2026-09-28 | `261cb5f7f` | A specialized, experimental SVG DOM 2D path. |
| 45 | `OPENVG` | 2026-09-17 | `fc1b6a537` | A 2D vector rasterizer. |
| 46 | `PORTABLEGL` | 2026-09-27 | `0f96ea703` | A bounded CPU OpenGL 3.x pipeline, overlapping `SOFTWARE`. |
| 47 | `TINYGL` | 2026-09-17 | `fc1b6a537` | A duplicate CPU route. |
| 48 | `IGL` | 2026-09-17 | `fc1b6a537` | Portable-abstraction middleware. |
| 49 | `PIXIJS` | 2026-09-17 | `fc1b6a537` | A duplicate browser 2D route. |
| 50 | `NANOVG` | 2026-09-17 | `fc1b6a537` | A 2D vector rasterizer. |
| 51 | `RLGL` | 2026-09-17 | `fc1b6a537` | Portable-abstraction middleware (raylib's GL wrappers). |

The C ABI version history for these removals is in
[`c-api/ABI_VERSIONING.md`](c-api/ABI_VERSIONING.md).

## Retirement groups

- **2026-10-13** (owner decision, `RRC-030`..`RRC-054`): CNA stops maintaining its own native
  Vulkan, Direct3D 11 and Metal renderers and SDL's 2D renderer. `SDLGPU` is CNA's modern native
  path; SDL owns the Vulkan, Direct3D 12 and Metal backends underneath it. Per-host defaults moved:
  `SDLGPU` on Windows, macOS and iOS, `OPENGLES3` everywhere else outside the browser. XNA
  behaviour their suites covered became renderer-neutral conformance programs
  (`modules/graphics/examples/conformance/`, registered as `<Renderer>_Conformance_*`) or
  CnaTests cases. Kept on purpose: SDL's own Vulkan/D3D12 patches and the `run-wine-vkd3d*.sh`
  launchers (`SDLGPU`), MojoShader's D3D11 and Metal patches (`FNA3D`'s own drivers),
  SPIRV-Cross for SDL_shadercross, the SDL_Renderer-based surface presenter that shows `SOFTWARE`'s
  CPU frames, and the `ShaderLanguageEXT` HLSL/MSL identities.
- **2026-10-06** (owner decision, `RRC-018`): EasyGL lost its two ES 2.0 profiles and every path
  that existed only for them (GLSL ES 3.00→1.00 lowering, texture-object sampler emulation, no-VAO,
  unsized-format and single-sample fallbacks). The GLSL ES 1.00-era shader forms that are valid
  GLSL ES 3.00 stay so pixels do not move; the sibling `easy-gl` library was not modified.
- **2026-09-28** (`RRC-015`/`RRC-016`): `GDI`'s reduced Software build went with it (`590ac27c8`):
  `CNA_SOFTWARE_2D_ONLY`, `SoftwareRenderer2D.cpp` and the `GDI`+`SOFTWARE` combination rule, so
  `SOFTWARE` compiles one full implementation. There is deliberately no EasyGL OpenGL 4 profile;
  `OPENGL33` keeps its 3.3 contract.
- **2026-09-27** (`RRC-012`): no constant, enumerator, class or stub renderer remains; the dense C++
  `GraphicsRendererType` enumerators after the removed ones moved down, and no surviving C ABI value
  changed.
- **2026-09-17** (`RRC-002`/`RRC-003`): four overlapping groups — portable-abstraction middleware
  (`FNA3D` stays as the one abstraction renderer, because it is the behavioural reference for XNA
  compatibility), legacy APIs, 2D vector rasterizers, and duplicate CPU and browser routes. The
  renderer-agnostic 2D corpus `modules/graphics/examples/cross_renderer_2d_corpus.cpp` and the
  browser runner `scripts/run_browser_tests.mjs` (for the C ABI browser probe) stayed.
- **2026-08-30**: `SKIA`. Ten identities removed with it (`LLGL`, `SOKOL`, `DILIGENT`, `IGL`,
  `WICKED`, `MAGNUM`, `BLEND2D`, `NANOVG`, `OPENVG`, `TINYGL`) were restored on 2026-09-04 and
  retired again on 2026-09-17.

## Dependency coordinates

Directories are `modules/renderers/<family>` in the removal commit's parent. "Build was" is the
configure selection that built it.

| Identity | Dependency | Build was |
|---|---|---|
| `VULKAN` | system Vulkan loader and headers (`Vulkan::Vulkan`), Python for its offline SPIR-V build | `-DCNA_GRAPHICS_RENDERER=VULKAN` |
| `DIRECTX11` | Windows SDK `d3d11`, `dxgi`, `d3dcompiler`; no fetched source | MinGW-w64 toolchain or native MSVC |
| `METAL` | Apple's Metal, QuartzCore, Foundation and AppKit/UIKit frameworks | macOS/iOS |
| `SDL_RENDERER` | SDL3's own 2D API (SDL3 itself stays) | `-DCNA_GRAPHICS_RENDERER=SDL_RENDERER` |
| `DIRECTX12` | Windows SDK `d3d12`, `dxgi`, `d3dcompiler` plus the shared `D3DCommon`; no fetched source | MinGW-w64 toolchain |
| `CANVAS` | browser Canvas 2D through Emscripten; no external dependency | `emcmake` |
| `OPENGLES2`, `WEBGL1` | EasyGL profiles over the sibling `easy-gl`/`meta-gl` checkouts | `-DCNA_GRAPHICS_RENDERER=OPENGLES2` / `WEBGL1` |
| `GDI` | Win32 `gdi32` and Software's CPU 2D units | MinGW-w64 toolchain |
| `HTML_DOM`, `SVG_DOM` | browser DOM/CSS or SVG DOM through Emscripten `EM_JS`; no external dependency | `emcmake` |
| `OPENGL4` | the platform OpenGL library (`find_package(OpenGL)`) | `-DCNA_GRAPHICS_RENDERER=OPENGL4` |
| `DIRECT2D` | Windows SDK `d2d1`, `d3d11`, `dxgi`; no fetched source (Linux runs through Wine's `d2d1`, or Proton `9.0 (Beta)`/`8.0` with DXVK for presentation) | MinGW-w64 toolchain (`scripts/run-wine-direct2d.sh`) or native MSVC |
| `FREEDIRECT` | sibling checkout `../free-direct` (`https://github.com/openeggbert/free-direct.git`, which adds `../free-api`), `add_subdirectory`, **never pinned** — the removal commit does not identify the revision it was built against | `-DCNA_GRAPHICS_RENDERER=FREEDIRECT` (named `DIRECTX3` until 2026-08-04) |
| `PORTABLEGL` | `https://github.com/rswinkle/PortableGL.git` @ tag `0.100.0`, commit `63a55db75ab07619797a93ff9bf3909355d27950` (MIT, single header) | `-DCNA_GRAPHICS_RENDERER=PORTABLEGL`; cannot share a binary with a real GL renderer (global `gl*` symbols) |
| `BGFX` | `https://github.com/bkaradzic/bgfx.cmake.git` @ `572868c0cb952add48019d267223453958e958b8` (submodules) plus CNA's `bgfx-max-render-target-msaa.patch` | `-DCNA_GRAPHICS_RENDERER=BGFX` (`CNA_BGFX_BUILD_SHADERC`, `CNA_BGFX_RENDERER`) |
| `MAGNUM` | `https://github.com/mosra/corrade.git` @ `783e4e4807536ec52c352986fc9317db986ace96`, `https://github.com/mosra/magnum.git` @ `5a7424643bfd4621fbcff8c361d37795502cf890` (or `CNA_MAGNUM_ROOT`) | `-DCNA_GRAPHICS_RENDERER=MAGNUM` (`CNA_MAGNUM_USE_EGL`) |
| `BLEND2D` | `https://github.com/blend2d/blend2d.git` @ `def0d1238c3e5d0983bb848e5676049d829e435b`, `https://github.com/asmjit/asmjit.git` @ `b56f4176cb9b0c0501da659ac54d4c5877862c7b` | `-DCNA_GRAPHICS_RENDERER=BLEND2D` |
| `DIRECTX1`…`DIRECTX7` | MinGW-w64 `ddraw` + `dxguid` import libraries, run under Wine; no fetched source | MinGW-w64 toolchain, `scripts/run-wine-directx<n>.sh` |
| `DIRECTX8` | DXVK's `d3d8.dll.a` from `/usr/lib/dxvk/wine64`, run through DXVK/D8VK | MinGW-w64 toolchain |
| `DIRECTX10` | MinGW-w64 `d3d10`, `dxgi`, `d3dcompiler`; Wine's `d3d10.dll` forwarding to DXVK's `d3d10core.dll` | MinGW-w64 toolchain |
| `OPENGLES1` | system `GLESv1_CM` and `GLES/gl.h` (Debian `libgles1 libgles-dev`); Debian's Mesa disables ES 1.1, so tests needed a side-by-side Mesa (`scripts/opengles1-test-env.sh`) | `-DCNA_GRAPHICS_RENDERER=OPENGLES1` |
| `OPENGL1`, `OPENGL2` | system OpenGL (`OpenGL::GL`) only | `-DCNA_GRAPHICS_RENDERER=OPENGL1` / `OPENGL2` |
| `WICKED` | `https://github.com/turanszkij/WickedEngine.git` @ `27c0df160d738925474a2181d3f88bfd59edaefe` (`CNA_WICKED_ROOT`) plus patches `wicked-cna-platform`, `wicked-device-teardown`, `wicked-sdl3-platform-legacy`, `wicked-staging-footprint` | `-DCNA_GRAPHICS_RENDERER=WICKED -DCNA_WICKED_ROOT=<checkout>` |
| `SOKOL` | `https://github.com/floooh/sokol.git` @ `27b49604b19be8cee0dcc6b2bbfe803dd9517585`; shaders compiled offline by `sokol-shdc` | `-DCNA_GRAPHICS_RENDERER=SOKOL` (`CNA_SOKOL_API=GLCORE`) |
| `DILIGENT` | `https://github.com/DiligentGraphics/DiligentCore.git` @ `v2.5.6` | `-DCNA_GRAPHICS_RENDERER=DILIGENT` (`CNA_DILIGENT_DEVICE`) |
| `GLIDE` | none linked: a caller-supplied 32-bit `glide3x.dll` at run time (dgVoodoo2 on Windows, OpenGlide on Wine, runtime pass with OpenGlide `b8ac1a32c98f9f8e8616aeffcf4b0af163b59b8f`) | `cmake/toolchains/mingw-w64-i686.cmake` (removed with it) |
| `LLGL` | `https://github.com/LukasBanana/LLGL.git` @ `Release-v0.04b`, commit `1e78d8fa497f5cab76b231ba13f4d6249dac0e7e` (or `CNA_LLGL_ROOT`) | `-DCNA_GRAPHICS_RENDERER=LLGL` (`CNA_LLGL_RENDERER`) |
| `OPENVG` | `https://github.com/ileben/ShivaVG.git` @ `6122ccb3c4b86f69a326f1a65b0f86bc79f69c50` (LGPL-2.1) plus `shivavg-context-dtor-leak.patch` | `-DCNA_GRAPHICS_RENDERER=OPENVG` |
| `TINYGL` | `https://github.com/C-Chads/tinygl.git` @ `36a7987e7bebfda19615ea33341b1cc0ff9c3b13` | `-DCNA_GRAPHICS_RENDERER=TINYGL` |
| `IGL` | `https://github.com/facebook/igl.git` @ `v1.1.1` (or `CNA_IGL_ROOT`), with its glm, fmt, glslang, SPIRV-Headers, volk and vma dependencies | `-DCNA_GRAPHICS_RENDERER=IGL` (`CNA_IGL_BACKEND`) |
| `PIXIJS` | `pixi.js` 7.4.2 UMD build, SHA-256 `9ddba9cd78bc8610a1d445ec939393888be83925c78e40d66d9a17e98450228d` (prepended with `--extern-pre-js`) | `emcmake cmake -DCNA_GRAPHICS_RENDERER=PIXIJS` |
| `NANOVG` | `https://github.com/memononen/nanovg.git` @ `ce3bf745eb2d2dbc14a50bf2446783f691ac4353` (zlib) | `-DCNA_GRAPHICS_RENDERER=NANOVG` |
| `RLGL` | `https://github.com/raysan5/raylib` @ `dbc56a87da87d973a9c5baa4e7438a9d20121d28` (archive SHA-256 `81b06ce7c19cf3b634b0271c23c361ba6ad8bf45fb8b036abbfeb4260ec1e126`), standalone `src/rlgl.h` only | `-DCNA_GRAPHICS_RENDERER=RLGL` (`CNA_RLGL_COMPILED_EFFECTS`) |
| `SKIA` | `https://skia.googlesource.com/skia.git`, **never pinned** (the developer build cloned HEAD; CMake required `-DCNA_SKIA_ROOT=<checkout>`). Reconstructing the revision from `docs/skia-ganesh-artifact.md` in the parent commit and the commit dates is the first restoration task | `-DCNA_GRAPHICS_RENDERER=SKIA -DCNA_SKIA_ROOT=<checkout>` |

Findings these renderers left in the surviving code: `SpriteBatch` refuses a foreign-device texture
at `Draw()` (from `NANOVG`'s per-context image handles), and a renderer that cannot read a vertex
record at its declared offsets must refuse it rather than read the wrong bytes (`OPENGLES1`,
`GLTF-473`).
