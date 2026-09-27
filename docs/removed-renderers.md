# Removed renderers

Renderer identities CNA no longer carries. CNA intentionally maintains a curated renderer set:
a renderer is added only when it provides meaningful platform coverage, compatibility value,
architectural value, or a capability not reasonably covered by the existing set, and one that
stops meeting that bar is retired. None of these was retired because the work was bad. Several
were excellent reconnaissance; what is kept here is the finding, not the code.

**The code is not gone.** Each entry names the commit that removed it; the parent of that commit
holds the whole implementation (`git show <commit>^:modules/renderers/<family>/...`, or
`git checkout <commit>^ -- modules/renderers/<family>`). The per-renderer plans stay in `plans/`
as history, marked retired. What git does *not* preserve is the third-party project each renderer
wrapped, so every entry records the exact dependency it was pinned to. That pairing — CNA's code in
git history, the dependency's coordinates here — is the archive.

Standalone probes for retired renderers were also removed from the current `spikes/` tree. Their
measured conclusions remain in the historical plans and entries below; Git history retains their
source if a past result needs to be inspected.

**A revert is a reference, not a patch.** `IGraphicsRenderer` changes often. Beyond a few months a
removal commit is a specification to port forward, not a patch to apply. Expect to read it, not
`git revert` it.

## Retired identity values

A retired identity's C ABI value (`CNA_GRAPHICS_RENDERER_*`, `modules/c-api/include/CNA/C/graphics.h`)
is **permanently reserved**: it is never assigned to another renderer, and surviving identities are
never renumbered to close the gap. The next new identity takes value **52**. The same table lives in
`cmake/RendererIdentities.cmake` (which refuses these names at configure time, naming the value)
and in `scripts/check_renderer_identities.py` (which fails if a value or a name is reused).

| Value | Identity | Removed |
|---:|---|---|
| 7 | `BGFX` | 2026-09-17 |
| 10 | `MAGNUM` | 2026-09-17 |
| 16 | `DIRECT2D` | 2026-09-27 |
| 19 | `SKIA` | 2026-08-30 |
| 20 | `BLEND2D` | 2026-09-17 |
| 21 | `FREEDIRECT` | 2026-09-27 |
| 23 | `DIRECTX1` | 2026-09-17 |
| 24 | `DIRECTX2` | 2026-09-17 |
| 25 | `DIRECTX3` | 2026-09-17 |
| 26 | `DIRECTX5` | 2026-09-17 |
| 27 | `DIRECTX6` | 2026-09-17 |
| 28 | `DIRECTX7` | 2026-09-17 |
| 29 | `DIRECTX8` | 2026-09-17 |
| 30 | `DIRECTX10` | 2026-09-17 |
| 32 | `OPENGLES1` | 2026-09-17 |
| 34 | `OPENGL1` | 2026-09-17 |
| 35 | `OPENGL2` | 2026-09-17 |
| 36 | `WICKED` | 2026-09-17 |
| 37 | `SOKOL` | 2026-09-17 |
| 38 | `DILIGENT` | 2026-09-17 |
| 39 | `GLIDE` | 2026-09-17 |
| 41 | `LLGL` | 2026-09-17 |
| 45 | `OPENVG` | 2026-09-17 |
| 46 | `PORTABLEGL` | 2026-09-27 |
| 47 | `TINYGL` | 2026-09-17 |
| 48 | `IGL` | 2026-09-17 |
| 49 | `PIXIJS` | 2026-09-17 |
| 50 | `NANOVG` | 2026-09-17 |
| 51 | `RLGL` | 2026-09-17 |

The C ABI went to `0.28.0` for the 2026-09-17 retirement and to `0.31.0` for the 2026-09-27 one
(`docs/c-api/ABI_VERSIONING.md`).

## The 2026-09-27 retirement

An intentional, permanent scope reduction (`plans/plan_renderer_cleanup.md` `RRC-012`): `DIRECT2D`,
`FREEDIRECT` and `PORTABLEGL` were removed in one commit, found with `git log --grep=RRC-012`, leaving
22 public renderer identities over 18 implementation families. None of the three was replaced, and
each was already covered by a renderer that stays:

- `DIRECT2D`, a Windows 2D-only renderer, by `GDI` (Windows 2D), `DIRECTX9`/`DIRECTX11`/`DIRECTX12`
  (Windows GPU) and `SDL_RENDERER`.
- `FREEDIRECT`, a DirectDraw-shaped 2D renderer over the unpinned `../free-direct` sibling, by
  `SDL_RENDERER` and `GDI`. It was the last renderer of the legacy DirectX lineage.
- `PORTABLEGL`, a bounded CPU OpenGL 3.x pipeline, by `SOFTWARE`, which is CNA's CPU renderer for
  the classic XNA surface.

**What was removed** besides the three `modules/renderers/<family>` directories: the three
`option(CNA_RENDERER_*)` switches and dispatch arms in `cmake/RendererSelection.cmake` (with the
`../free-direct` sibling `add_subdirectory` and the `PORTABLEGL` member of the `TERMINAL` renderer
list); `cmake/ThirdPartyPortableGL.cmake`; the registry map rows; the `PORTABLEGL`-versus-real-GL
combination rule and `DIRECT2D` in the Windows-only partition (`cmake/RendererCombinations.cmake`);
`FREEDIRECT` from the SDL3-dependent renderer lists (`cmake/SdlAvailability.cmake`,
`cmake/Sdl2OnlyConfiguration.cmake`, `tools/platform/renderer_sdl_audit.py`,
`tools/platform/sdl_ratchet.py`), so the SDL allowlist is now `sdl-renderer`, `sdl-gpu` and
`fna3d`; the `CNA_DIRECT2D_TEST_RUNTIME` cache option, the Direct2D Wine emulator arm and the
`Direct2D_Unit` test in `cmake/UnitTests.cmake`; the `GraphicsRendererType` enumerators and their
name, category and maturity arms; the three C ABI constants and their C/C++ mappings; the
`DIRECT2D` leg of `.github/workflows/d3d-windows-ci.yml`; nine Direct2D-only scripts under
`scripts/` and the `tests/fixtures/direct2d` log fixtures; seven renderer documents under `docs/`;
and the per-renderer arms in shared tests and examples.

**Compatibility residue, kept on purpose.** The three names stay in
`CNA_RENDERER_RETIRED_IDENTITIES` so that every selection route -- `CNA_GRAPHICS_RENDERER`, a member
of `CNA_GRAPHICS_RENDERERS`, and `CNA_RENDERER_<X>=ON` -- is refused by name instead of silently
configuring the host default. Their C ABI values 16, 21 and 46 stay reserved: `graphics.h` names them
in the `CNA_GRAPHICS_RENDERER_MAXIMUM` comment, and every C route refuses them with
`CNA_RESULT_INVALID_ARGUMENT`. No constant, enumerator, class or stub renderer remains. The C++
enumerators after the removed ones moved down, as the dense `GraphicsRendererType` has always
allowed; no surviving C ABI value changed.

**Deliberately preserved.** `GDI` and `SOFTWARE` were not modified. None of the removed families
shared source with them: `DIRECT2D` used only the common `PlatformRendererSurfaceState.hpp` (still
included by `GDI`, `DIRECTX9`/`11`/`12`, `METAL` and `WEBGPU`), `FREEDIRECT` the common
`NoOp3DResources.hpp` and `Sdl3RendererInterop.hpp` (still used by `SDL_RENDERER`, `CANVAS`,
`SDL_GPU` and `FNA3D`), and `PORTABLEGL` `VertexDeclarationFidelity.hpp` (used throughout).
`RequirePbrShadingSupportEXT` stays because `FNA3D` still calls it. The Win32 platform, the
`IPlatformSurfacePresenter` CPU-frame path and the `TERMINAL` platform are unchanged; `TERMINAL`
keeps `SOFTWARE`, `HEADLESS` and `STUB`.

**Validation performed** (2026-09-27, Linux x86-64). `cmake-build-multi` was configured from an
empty directory with `SDL_RENDERER;OPENGLES3;VULKAN;SOFTWARE;HEADLESS;STUB` in one binary and
`CnaTests` built: all six retained families compile against the changed headers. On the private
Weston/Xwayland display (`tools/platform/run_gpu_tests_private.sh`) the renderer identity, selection,
fallback, registry, descriptor, capability, cube-storage, glTF policy and cross-renderer contract
suites ran 331 cases: 317 passed, 14 skipped by configuration, none failed. In the same tree the 29
retired-selector refusals, the route and live-accept cases, the identity/combination/discipline
gates and the C ABI header baseline all pass. (The discipline gate's identity parser was matching
nothing at the time; `RRC-013` repaired it, and it passes over all 22 identities.) The default `cmake-build-debug` (`OPENGLES3`, SDL3)
was rebuilt incrementally and its full CTest suite run before and after the change on the same
display: 134 of 10,140 failed before, 120 of 10,144 after, and no test fails only after the change
except `CnaInputTests`, an X11 `BadWindow` under parallel load that passes on its own. The C ABI
baseline was regenerated against the rebuilt `libcna_c_api.so`: only the three constants, the
maximum and the version changed, and the 3,213 exports did not. `cmake-build-gdi` was configured
from an empty directory with the MinGW-w64 toolchain and `CNA_GRAPHICS_RENDERER=GDI`, and every GDI
renderer translation unit compiles.

**Not validated.** No Windows, macOS or browser run was possible here. The GDI executables do not
link on this tree for a reason that predates this change: `SoftwareRenderer2D.cpp`, compiled into
`GDI` with `CNA_SOFTWARE_2D_ONLY`, calls helpers that are only defined outside that mode (since
`8465377b1`/`0f213ebaf`, 2026-09-08), so the GDI tests were neither linked nor run. `DIRECTX9`,
`DIRECTX11`, `DIRECTX12`, `METAL`, `WEBGPU`, `SDL_GPU`, `OPENGL4`, `FNA3D` and the Emscripten
renderers were not built; none of their sources changed.

### DIRECT2D

| | |
|---|---|
| Identity | `DIRECT2D` (enum `Direct2D`, C ABI 16) |
| Family | `modules/renderers/direct2d` — 11 files, 8,467 lines |
| Dependency | the Windows SDK `d2d1`, `d3d11` and `dxgi` import libraries, no fetched source; Linux cross-builds ran through Wine's `d2d1` in a dedicated prefix, or Proton `9.0 (Beta)`/`8.0` through DXVK for the presentation device |
| Build was | `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake -DCNA_GRAPHICS_RENDERER=DIRECT2D` (tests through `scripts/run-wine-direct2d.sh`, `CNA_DIRECT2D_TEST_RUNTIME=WINE|PROTON`), or native MSVC |

**What it proved.** A SpriteBatch-oriented 2D renderer on `ID2D1DeviceContext`, with a D3D11 device
and flip-model swap chain only as its host (`plans/plan_direct2d.md`): byte-exact
`SurfaceFormat::Color` ↔ BGRA conversion, authored `Texture2D` mips, exact Porter-Duff tuples mapped
to Direct2D composites with every inexact blend refused, device-loss recovery that raised
`ContentLost`, and a native debug-layer/live-object gate in CI. The renderer-agnostic 2D corpus it
motivated, `modules/graphics/examples/cross_renderer_2d_corpus.cpp`, stays and is still built by
EasyGL and Vulkan.

### FREEDIRECT

| | |
|---|---|
| Identity | `FREEDIRECT` (enum `FreeDirect`, C ABI 21) |
| Family | `modules/renderers/freedirect` — 15 files, 4,203 lines |
| Dependency | the sibling checkout `../free-direct` (`https://github.com/openeggbert/free-direct.git`, which itself adds `../free-api`), added with `add_subdirectory` and **never pinned** -- the build used whatever that checkout held; its own SDL3 came from CNA's vendored targets |
| Build was | `-DCNA_GRAPHICS_RENDERER=FREEDIRECT` with `../free-direct` present (native Linux, no Wine) |

**What it proved.** A 2D-only renderer over a COM-shaped DirectDraw subset: a CPU shadow backbuffer
that worked around `free-direct`'s unwritable primary surface, a `BltFast` fast path plus an
edge-function compositor, four distinct `BlendState` formulas, bilinear filtering and
`Wrap`/`Mirror` addressing (`plans/plan_freedirect.md`). It was named `DIRECTX3` until 2026-08-04.
**Note for anyone restoring it:** like Skia, the dependency was never pinned, so the removal commit
does not identify the `free-direct` revision it was built against.

### PORTABLEGL

| | |
|---|---|
| Identity | `PORTABLEGL` (enum `PortableGL`, C ABI 46) |
| Family | `modules/renderers/portablegl` — 19 files, 5,853 lines |
| Dependency | `https://github.com/rswinkle/PortableGL.git` @ tag `0.100.0`, commit `63a55db75ab07619797a93ff9bf3909355d27950` (MIT, single header, FetchContent; `cmake/ThirdPartyPortableGL.cmake`, removed with it) |
| Build was | `-DCNA_GRAPHICS_RENDERER=PORTABLEGL` (no GPU, no window) |

**What it proved.** A bounded CPU 3D path in which every pipeline stage is a real PortableGL call --
buffers, vertex attributes, C-function-pointer shaders, `glDrawArrays`/`glDrawElements`, blend,
stencil, cull and polygon mode -- with everything outside that boundary refused by name. It could
not share a binary with a real OpenGL renderer, because the header defines the global `gl*`
symbols; that combination rule left with it.

## The 2026-09-17 retirement

`plans/plan_renderer_cleanup.md` retired twenty-five identities in one workstream, removed in commit
`fc1b6a537` (`refactor(Task RRC-002/RRC-003)`), leaving 25 public renderer identities over 21
implementation families. The set that stays covers every platform CNA targets with at least one
native renderer, keeps a CPU renderer and a no-pixel renderer for tests, and keeps FNA3D as the
compatibility reference. What was retired fell into four groups, each overlapping something that
stays:

- **Portable-abstraction middleware** — `BGFX`, `MAGNUM`, `WICKED`, `SOKOL`, `DILIGENT`, `LLGL`,
  `IGL`, `RLGL`. Each put a second abstraction layer between CNA and a native API that CNA already
  drives directly (`VULKAN`, `OPENGL33`/`OPENGL4`, `DIRECTX11`/`DIRECTX12`, `METAL`), and each carried
  a large pinned dependency. `FNA3D` stays as the one abstraction renderer, because it is the
  behavioural reference for XNA compatibility.
- **Legacy and historical APIs** — `DIRECTX1`…`DIRECTX8`, `DIRECTX10`, `GLIDE`, `OPENGL1`, `OPENGL2`,
  `OPENGLES1`. Research into old APIs rather than platform coverage: every platform they reached is
  reached by `DIRECTX9`/`DIRECTX11`/`DIRECTX12`, `GDI`, `OPENGLES2`/`OPENGL33`/`OPENGL4` or `SOFTWARE`.
- **2D vector rasterizers** — `BLEND2D`, `OPENVG`, `NANOVG`. 2D-only by construction, so they could
  never satisfy the 3D half of `IGraphicsRenderer`; CNA's 2D already renders on the GPU through EasyGL
  and SDL_GPU, and on the CPU through `SOFTWARE`.
- **Duplicate CPU and browser routes** — `TINYGL` (covered by `SOFTWARE` and `PORTABLEGL`, the
  latter itself retired on 2026-09-27) and
  `PIXIJS` (covered by `WEBGL2`, `CANVAS`, `HTML_DOM` and `SVG_DOM`).

The sizes below count the family directory only; each renderer also had documentation, CI and
scripts, removed or marked retired in the same workstream.

---

### BGFX

| | |
|---|---|
| Identity | `BGFX` (enum `Bgfx`, C ABI 7) |
| Family | `modules/renderers/bgfx` — 152 files, 51,260 lines |
| Dependency | `https://github.com/bkaradzic/bgfx.cmake.git` @ `572868c0cb952add48019d267223453958e958b8` (FetchContent, submodules), plus CNA's `bgfx-max-render-target-msaa.patch` exposing the renderer-selected render-target MSAA ceiling in `bgfx::Caps` |
| Build was | `-DCNA_GRAPHICS_RENDERER=BGFX` (optional `-DCNA_BGFX_BUILD_SHADERC=ON` to regenerate `bgfx_shaders.hpp`; runtime API via `CNA_BGFX_RENDERER`) |

**What it proved.** One of CNA's earliest renderers (2026-04) and for months the reference for the
stock-effect, render-target and readback contracts; a large part of today's renderer-agnostic example
corpus was first written against it. Two of those sources were still registered by the Vulkan
renderer and moved to `modules/renderers/vulkan/examples/` rather than being lost.

### MAGNUM

| | |
|---|---|
| Identity | `MAGNUM` (enum `Magnum`, C ABI 10) |
| Family | `modules/renderers/magnum` — 30 files, 9,598 lines |
| Dependency | `https://github.com/mosra/corrade.git` @ `783e4e4807536ec52c352986fc9317db986ace96`, `https://github.com/mosra/magnum.git` @ `5a7424643bfd4621fbcff8c361d37795502cf890` (FetchContent, or `CNA_MAGNUM_ROOT`) |
| Build was | `-DCNA_GRAPHICS_RENDERER=MAGNUM` (optional `-DCNA_MAGNUM_USE_EGL=ON`) |

**What it proved.** That a typed C++ GL wrapper can host the whole XNA surface on an externally owned
GL 3.3 core context handed to it through `IPlatformGlContext`, with every resource and draw going
through `Magnum::GL`.

### BLEND2D

| | |
|---|---|
| Identity | `BLEND2D` (enum `Blend2D`, C ABI 20) |
| Family | `modules/renderers/blend2d` — 16 files, 4,063 lines |
| Dependency | `https://github.com/blend2d/blend2d.git` @ `def0d1238c3e5d0983bb848e5676049d829e435b` and `https://github.com/asmjit/asmjit.git` @ `b56f4176cb9b0c0501da659ac54d4c5877862c7b` (FetchContent) |
| Build was | `-DCNA_GRAPHICS_RENDERER=BLEND2D` |

**What it proved.** The CPU-raster-plus-platform-presentation shape: a premultiplied Blend2D
backbuffer handed to `IPlatformSurfacePresenter`, including on the `TERMINAL` platform, where it was
the renderer the terminal integration test ran. **Consequence to know about:** no surviving renderer
requests a surface presenter, so the `TERMINAL` platform's CI leg now builds `SOFTWARE`
(`plans/plan_renderer_cleanup.md`, findings).

### DIRECTX1, DIRECTX2, DIRECTX3, DIRECTX5, DIRECTX6, DIRECTX7, DIRECTX8

| | |
|---|---|
| Identities | `DIRECTX1` (23), `DIRECTX2` (24), `DIRECTX3` (25), `DIRECTX5` (26), `DIRECTX6` (27), `DIRECTX7` (28), `DIRECTX8` (29) |
| Families | `modules/renderers/directx1` (14 files, 3,130 lines), `directx2` (23 / 5,549), `directx3` (23 / 5,570), `directx5` (23 / 5,569), `directx6` (24 / 5,829), `directx7` (24 / 5,813), `directx8` (24 / 5,212) |
| Dependency | MinGW-w64 `ddraw` + `dxguid` import libraries, run under Wine (`DIRECTX1`…`DIRECTX7`); DXVK's own `d3d8.dll.a` from `/usr/lib/dxvk/wine64`, run through DXVK/D8VK (`DIRECTX8`). No fetched source |
| Build was | `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake -DCNA_GRAPHICS_RENDERER=DIRECTX<n>`, tests through `scripts/run-wine-directx<n>.sh` |

**What they proved.** The real COM interface of each DirectX generation, one step at a time
(`plans/plan_dxold.md` and `plans/plan_dx1.md`…`plans/plan_dx8.md`): DirectDraw v1 2D; Direct3D
`DrawPrimitive` once the execute-buffer model was shown non-functional under Wine (recorded in
`plans/plan_dx2.md`); DirectDraw v2 and v4; FVF submission; real stencil in DirectX 6; the
flattened DirectX 7 device; and Direct3D 8's merged device with a DXVK delivery route. `DIRECTX3`
here is the *real* DirectX 3 renderer;
the `../free-direct`-backed renderer that once held that name is `FREEDIRECT`, which survived this
retirement and was retired on 2026-09-27.

### DIRECTX10

| | |
|---|---|
| Identity | `DIRECTX10` (enum `DirectX10`, C ABI 30) |
| Family | `modules/renderers/directx10` — 11 files, 2,740 lines |
| Dependency | MinGW-w64 `d3d10`, `dxgi`, `d3dcompiler`; Wine's builtin `d3d10.dll` forwarding to DXVK's `d3d10core.dll` |
| Build was | `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake -DCNA_GRAPHICS_RENDERER=DIRECTX10` |

**What it proved.** The first programmable-only Direct3D generation under the DXVK route: real
`vs_4_0`/`ps_4_0` HLSL, state objects and MRT (`plans/plan_d3d10.md`). `DIRECTX11` covers the same
hardware class.

### OPENGLES1

| | |
|---|---|
| Identity | `OPENGLES1` (enum `OpenGLES1`, C ABI 32) |
| Family | `modules/renderers/opengles1` — 12 files, 6,034 lines |
| Dependency | system `GLESv1_CM` and `GLES/gl.h` (Debian `libgles1 libgles-dev`); Debian's Mesa ships with ES 1.1 disabled, so runtime tests needed a side-by-side Mesa build (`scripts/opengles1-test-env.sh`) |
| Build was | `-DCNA_GRAPHICS_RENDERER=OPENGLES1` |

**What it proved.** A fixed-function ES 1.1 implementation of the XNA surface, and the `GLTF-473`
finding that a fixed-function renderer must *refuse* a record it would otherwise read through the wrong
byte offsets. That shared guard left with it; `CanonicalOffsetOfSemanticEXT`, which only it used, was
removed as dead code.

### OPENGL1, OPENGL2

| | |
|---|---|
| Identities | `OPENGL1` (34), `OPENGL2` (35) |
| Families | `modules/renderers/opengl1` (33 files, 5,598 lines), `modules/renderers/opengl2` (53 / 12,838) |
| Dependency | system OpenGL (`OpenGL::GL`) only |
| Build was | `-DCNA_GRAPHICS_RENDERER=OPENGL1` / `OPENGL2` |

**What they proved.** Desktop fixed-function GL 1.x (including real `ARB_occlusion_query`) and a
GLSL 1.10 compatibility-profile renderer with runtime-resolved post-1.1 entry points
(`plans/plan_opengl1.md`, `plans/plan_opengl2.md`). `OPENGL33` and `OPENGL4` cover desktop GL.

### WICKED

| | |
|---|---|
| Identity | `WICKED` (enum `Wicked`, C ABI 36) |
| Family | `modules/renderers/wicked` — 9 files, 6,991 lines |
| Dependency | `https://github.com/turanszkij/WickedEngine.git` @ `27c0df160d738925474a2181d3f88bfd59edaefe` (hand-supplied `CNA_WICKED_ROOT`, optional auto-fetch), plus four carried patches: `wicked-cna-platform`, `wicked-device-teardown`, `wicked-sdl3-platform-legacy`, `wicked-staging-footprint` |
| Build was | `-DCNA_GRAPHICS_RENDERER=WICKED -DCNA_WICKED_ROOT=<checkout>` |

**What it proved.** That only the RHI layer of a full engine (`wi::graphics::GraphicsDevice`) can back
CNA, and three upstream defects found on the way (device teardown leaks, narrow staging-buffer
under-allocation, the platform bridge) — each with a patch (`plans/plan_wicked.md`).

### SOKOL

| | |
|---|---|
| Identity | `SOKOL` (enum `Sokol`, C ABI 37) |
| Family | `modules/renderers/sokol` — 29 files, 25,822 lines (including generated `sokol_shaders.hpp`) |
| Dependency | `https://github.com/floooh/sokol.git` @ `27b49604b19be8cee0dcc6b2bbfe803dd9517585` (FetchContent); shaders compiled offline by `sokol-shdc` |
| Build was | `-DCNA_GRAPHICS_RENDERER=SOKOL` (`CNA_SOKOL_API=GLCORE`, the only verified API) |

**What it proved.** Near-EasyGL parity on `sokol_gfx`'s GL core route, including MRT, MSAA resolve and
readback, and the permanent API-shaped gaps (`FillMode`, cube MSAA, `MultiSampleMask`)
(`plans/plan_sokol.md`).

### DILIGENT

| | |
|---|---|
| Identity | `DILIGENT` (enum `Diligent`, C ABI 38) |
| Family | `modules/renderers/diligent` — 40 files, 15,209 lines |
| Dependency | `https://github.com/DiligentGraphics/DiligentCore.git` @ `v2.5.6` (FetchContent) |
| Build was | `-DCNA_GRAPHICS_RENDERER=DILIGENT` (native API chosen at runtime, `CNA_DILIGENT_DEVICE`) |

**What it proved.** A renderer whose native API is chosen at run time over two stacked abstraction
layers, with render targets, MSAA, occlusion queries and PBR (`plans/plan_diligent.md`).

### GLIDE

| | |
|---|---|
| Identity | `GLIDE` (enum `Glide`, C ABI 39) |
| Family | `modules/renderers/glide` — 34 files, 6,786 lines |
| Dependency | none linked: a caller-supplied 32-bit `glide3x.dll` at run time (dgVoodoo2 on Windows, OpenGlide on Wine; runtime pass with OpenGlide `b8ac1a32c98f9f8e8616aeffcf4b0af163b59b8f`); built with `cmake/toolchains/mingw-w64-i686.cmake`, removed with it |
| Build was | `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-i686.cmake -DCNA_GRAPHICS_RENDERER=GLIDE` |

**What it proved.** A hand-declared 32-bit Glide 3.x ABI verified against an independent fake DLL for
all 39 exports, and 2D SpriteBatch batching through `grDrawVertexArrayContiguous` (`plans/plan_glide.md`).

### LLGL

| | |
|---|---|
| Identity | `LLGL` (enum `Llgl`, C ABI 41) |
| Family | `modules/renderers/llgl` — 91 files, 27,011 lines |
| Dependency | `https://github.com/LukasBanana/LLGL.git` @ `Release-v0.04b` (commit `1e78d8fa497f5cab76b231ba13f4d6249dac0e7e`, FetchContent or `CNA_LLGL_ROOT`) |
| Build was | `-DCNA_GRAPHICS_RENDERER=LLGL` (`CNA_LLGL_RENDERER=auto|opengl`) |

**What it proved.** A supported `LLGL → OpenGL → GLX` route on Linux/X11 with a written post-audit
contract, and that its Vulkan module was not a safe fallback at that pin (`plans/plan_llgl.md`).

### OPENVG

| | |
|---|---|
| Identity | `OPENVG` (enum `OpenVg`, C ABI 45) |
| Family | `modules/renderers/openvg` — 17 files, 3,251 lines |
| Dependency | `https://github.com/ileben/ShivaVG.git` @ `6122ccb3c4b86f69a326f1a65b0f86bc79f69c50` (LGPL-2.1, FetchContent), plus `shivavg-context-dtor-leak.patch` |
| Build was | `-DCNA_GRAPHICS_RENDERER=OPENVG` |

**What it proved.** Real Khronos OpenVG 1.1 calls on a CNA-owned GL context, and an end-to-end audit
of 2D presentation, scissor and blend math against a vector API (recorded in Git history).

### TINYGL

| | |
|---|---|
| Identity | `TINYGL` (enum `TinyGL`, C ABI 47) |
| Family | `modules/renderers/tinygl` — 14 files, 5,154 lines |
| Dependency | `https://github.com/C-Chads/tinygl.git` @ `36a7987e7bebfda19615ea33341b1cc0ff9c3b13` (FetchContent) |
| Build was | `-DCNA_GRAPHICS_RENDERER=TINYGL` |

**What it proved.** The fixed-function CPU counterpart to `PORTABLEGL`, with a 1-bit colour-key
transparency model and cross-platform CI (`plans/plan_tinygl.md`).

### IGL

| | |
|---|---|
| Identity | `IGL` (enum `Igl`, C ABI 48) |
| Family | `modules/renderers/igl` — 48 files, 14,406 lines |
| Dependency | `https://github.com/facebook/igl.git` @ `v1.1.1` (FetchContent or `CNA_IGL_ROOT`), with its glm, fmt, glslang, SPIRV-Headers, volk and vma dependencies |
| Build was | `-DCNA_GRAPHICS_RENDERER=IGL` (`CNA_IGL_BACKEND=auto|opengl|vulkan`) |

**What it proved.** 27/27 example tests on both its OpenGL and Vulkan backends, a shader library
generated from the vertex declaration, and the upstream limits of IGL `v1.1.1` — no occlusion
queries, no 3D-texture readback (`plans/plan_igl.md`).

### PIXIJS

| | |
|---|---|
| Identity | `PIXIJS` (enum `PixiJs`, C ABI 49) |
| Family | `modules/renderers/pixijs` — 13 files, 3,465 lines |
| Dependency | `pixi.js` 7.4.2 UMD build, SHA-256 `9ddba9cd78bc8610a1d445ec939393888be83925c78e40d66d9a17e98450228d` (downloaded, prepended with `--extern-pre-js`) |
| Build was | `emcmake cmake -DCNA_GRAPHICS_RENDERER=PIXIJS`; native host contracts with `-DCNA_BUILD_PIXIJS_HOST_TESTS=ON` |

**What it proved.** A retained-mode scene graph can back an immediate-mode `SpriteBatch` if it commits
at every submission point, with a browser pixel suite (70/70 in headless Chromium). Its browser test
runner was not renderer-specific and stays as `scripts/run_browser_tests.mjs` for the C ABI browser
probe.

### NANOVG

| | |
|---|---|
| Identity | `NANOVG` (enum `NanoVg`, C ABI 50) |
| Family | `modules/renderers/nanovg` — 21 files, 5,571 lines |
| Dependency | `https://github.com/memononen/nanovg.git` @ `ce3bf745eb2d2dbc14a50bf2446783f691ac4353` (zlib, FetchContent) |
| Build was | `-DCNA_GRAPHICS_RENDERER=NANOVG` |

**What it proved.** `SpriteBatch` through NanoVG's compiled GLSL 1.10 vector pipeline on a CNA-owned
GL 2.1 context, and that its per-context image handles make cross-device texture use a wrong-picture
defect rather than a lost draw — which is why `SpriteBatch` now refuses a foreign texture at `Draw()`.

### RLGL

| | |
|---|---|
| Identity | `RLGL` (enum `Rlgl`, C ABI 51) |
| Family | `modules/renderers/rlgl` — 48 files, 26,690 lines |
| Dependency | `https://github.com/raysan5/raylib` @ `dbc56a87da87d973a9c5baa4e7438a9d20121d28` (archive SHA-256 `81b06ce7c19cf3b634b0271c23c361ba6ad8bf45fb8b036abbfeb4260ec1e126`), standalone `src/rlgl.h` only |
| Build was | `-DCNA_GRAPHICS_RENDERER=RLGL` (optional `-DCNA_RLGL_COMPILED_EFFECTS=ON`) |

**What it proved.** EasyGL-equivalent classic XNA 4.0 renderer parity on raylib's low-level GL 3.3
wrappers without the raylib framework, including the compiled-effect contracts
(`plans/plan_rlgl.md`; `docs/rlgl-renderer.md` and `docs/rlgl-parity-campaign.md` in the removal
commit's parent).

---

## SKIA

| | |
|---|---|
| Identity | `SKIA` (enum `Skia`, C ABI `CNA_GRAPHICS_RENDERER_SKIA` = 19) |
| Family | `modules/renderers/skia` |
| Removed | 2026-08-30, commit `b6b275cb0` |
| Size | 32,563 lines (9,684 production, **0 tests**, 22,879 examples) — plus 34 `docs/skia-*.md`, `plans/plan_skia.md`, `NEXT_skia.md` and 6 `scripts/validate_skia_*.py` |
| Dependency | `https://skia.googlesource.com/skia.git` — **not pinned**; the developer build cloned it at whatever HEAD was, and CMake required `-DCNA_SKIA_ROOT=<checkout>` rather than fetching it |
| Build was | `-DCNA_GRAPHICS_RENDERER=SKIA -DCNA_SKIA_ROOT=<skia checkout>` |

**What it proved.** That an external CPU rasterizer can be driven as a CNA renderer, and —
more usefully — exactly where that stops. The 3D refusal was reasoned out rather than
assumed (`docs/skia-3d-refusal.md`, `docs/skia-3d-emulation-adr.md`), and the GLSL→SkSL
translator contract, the CPU depth/stencil/geometry spikes and the surface-format matrix
are all real findings about the cost of emulating a GPU pipeline on a 2D canvas API.

**Why removed.** It is 2D-only by construction, so it can never satisfy `IGraphicsRenderer`:
CNA's contract has 102 pure-virtual methods covering depth, render targets, MSAA, MRT and
stock 3D effects, and Skia never advertised any of them. That is a category error rather
than unfinished work — no amount of further effort finishes it. Meanwhile EasyGL already
renders CNA's 2D on every platform CNA targets, on the GPU. Skia was also the heaviest
dependency in the tree, and the only one with no pinned revision — the build was not
reproducible across machines or across time.

**Note for anyone restoring it.** Because the dependency was never pinned, this removal
commit does not identify the Skia revision it was written against. Reconstructing that
from `docs/skia-ganesh-artifact.md` and the commit dates is the first task.

Ten identities removed alongside `SKIA` on 2026-08-30 (`LLGL`, `SOKOL`, `DILIGENT`, `IGL`, `WICKED`,
`MAGNUM`, `BLEND2D`, `NANOVG`, `OPENVG`, `TINYGL`) were restored on 2026-09-04 and then retired again,
with fifteen others, on 2026-09-17 (entries above).
