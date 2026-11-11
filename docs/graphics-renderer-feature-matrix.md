# Graphics renderer feature matrix

Status: reconciled with the active renderer registry, runtime capability queries and renderer
documents on **2026-10-31**.

This is the project-wide entry point for comparing graphics renderers. CNA exposes ten public
renderer identities over eight implementation families. The three EasyGL identities
(`OPENGLES3`, `OPENGL33` and `WEBGL2`) share one implementation and therefore occupy one column
below. The six columns in the raster tables are the six implementations that produce pixels;
`HEADLESS` and `STUB` have a separate table because calling either one a rasterizer would be
misleading. The canonical identity list is [`renderer-registry.md`](renderer-registry.md).

The matrix is a maintained summary, not a replacement for a live device query. For a particular
build, graphics profile, adapter and driver, use
`GraphicsDevice::GetRendererCapabilityProfileEXT()`, `SupportsCapability()` and the per-format
queries documented in [`renderer-capability-profiles.md`](renderer-capability-profiles.md). A broad
feature cell does not promise every `SurfaceFormat`, every feature combination or every platform.
The renderer's own document supplies the exact boundary, and current defects live only in
[KNOWN_ISSUES.md](../KNOWN_ISSUES.md).

Legend:

- ✅ — implemented, with current executable evidence on at least one documented environment;
- ◇ — implemented through an emulation or with a stable behavioral restriction named below;
- ⚙ — depends on the build option, graphics profile, live device or selected internal driver;
- ❌ — explicitly unsupported and refused rather than silently approximated;
- — — not meaningful for that renderer.

## Rasterizing families: common XNA and CNA surface

| Feature family | EasyGL | WebGPU | SDLGPU | Software | DirectX9 | FNA3D |
|---|---:|---:|---:|---:|---:|---:|
| `SpriteBatch`, `SpriteFont` and 2D state | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Classic 3D and stock effects | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `PbrEffect` and `SkinnedPbrEffect` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `Texture2D` and `TextureCube` storage, mips and sampling | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `Texture3D` storage and transfer under HiDef | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `RenderTarget2D` and `RenderTargetCube` | ✅ | ✅ | ✅ | ✅ | ⚙ per format/device | ✅ |
| MRT, ordered 2–4 attachments under HiDef | ⚙ | ⚙ | ⚙ | ✅ | ⚙ | ⚙ |
| Backbuffer and ordinary render-target readback | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Blend, depth/stencil, rasterizer and sampler state | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 16-bit and 32-bit indexed drawing | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

The public MRT ceiling is four attachments even when a native API exposes more. Reach permits one
attachment and therefore reports no MRT capability. Texture and target support is also
format-specific: use the live per-format profile before selecting a non-`Color`, compressed,
floating-point or filterable resource. FNA3D's compressed readback additionally depends on its
selected internal driver.

Stock-effect support does not establish every combined state. The fog-alpha contract
(CNA-FIX-D01), its tested combinations and its platform boundary are in
[`sdl-gpu-renderer.md`](sdl-gpu-renderer.md#stock-effect-fog-alpha-cna-fix-d01).

## Rasterizing families: conditional and differentiating features

| Feature | EasyGL | WebGPU | SDLGPU | Software | DirectX9 | FNA3D |
|---|---:|---:|---:|---:|---:|---:|
| MSAA | ⚙ live context | ⚙ live adapter | ⚙ live device | ✅ 4× CPU | ⚙ 2D targets; no cube MSAA | ⚙ selected driver |
| Wireframe | ⚙ native polygon mode | ◇ triangle-edge expansion | ✅ | ✅ | ✅ | ✅ |
| Multiple vertex streams | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ |
| Instanced drawing | ✅ | ✅ | ✅ | ✅ | ✅ | ⚙ compiled-effect shader + driver |
| Occlusion queries | ◇ exact or boolean by GL profile | ⚙ exact or visibility-only by adapter | ❌ | ✅ exact CPU count | ✅ | ⚙ selected driver |
| Float and half-float render targets | ⚙ per format/context | ⚙ per format/adapter feature | ⚙ per format/device | ✅ | ⚙ per format/device | ⚙ per format/driver |
| CNAEXT `ShaderEffect` execution | ✅ GLSL / GLSL ES | ✅ WGSL | ✅ SPIR-V package; ⚙ GLSL text | ❌ | ✅ HLSL | ❌ |
| Compiled XNA Effect bytecode | ⚙ opt-in | ⚙ opt-in | ⚙ opt-in | ⚙ opt-in | ⚙ opt-in | ✅ always on |

Compiled-effect support is controlled independently for each family and is off by default, except
`CNA_EASYGL_COMPILED_EFFECTS`, which defaults ON when OPENGL33 is selected without OPENGLES3 or
WEBGL2 (`cmake/RendererSelection.cmake`): `CNA_EASYGL_COMPILED_EFFECTS`, `CNA_WEBGPU_COMPILED_EFFECTS`,
`CNA_SDLGPU_COMPILED_EFFECTS`, `CNA_SOFTWARE_COMPILED_EFFECTS` and
`CNA_DIRECTX9_COMPILED_EFFECTS`. [`fx-compiled-effects.md`](fx-compiled-effects.md) defines the
shared bytecode, reflection, pass, state and draw contract.

`ShaderEffect` is a separate CNA extension. With `CNA_CNAEXT=ON`, SDLGPU's explicit SPIR-V package
intake exists on a live device without runtime `shaderc` and routes through ShaderCross when the
selected SDL driver does not consume SPIR-V directly; only its legacy GLSL-text compilation is
conditional on the build-derived `CNA_SDLGPU_SHADER_EFFECTS` definition. Software deliberately
does not execute the supplied payload, while FNA3D accepts compiled Effect bytecode instead.
DirectX9's verified runtime path reports HLSL, accepts its vertex and fragment stages, and marks
both source execution and `ShaderDialectHlsl` supported in the detailed profile. The full
eight-family distinction between acceptance, exact language intake and execution is
[`shader-effect-vs-fx-bytecode.md`](shader-effect-vs-fx-bytecode.md).

## Non-raster identities

| Contract | Headless | Stub |
|---|---|---|
| Intended use | Game-logic CI, validation, trace and counters | Smallest dependency-free no-op implementation |
| Draw result | No pixels; validates and records according to its selected mode | No pixels; draw calls are no-ops |
| Observable diagnostics | Resource lifecycle, state changes, structured draw trace and counters | None beyond successful API plumbing |
| Texture CPU storage | Exact advertised `Texture2D`/`TextureCube` transfer shadows; no `Texture3D` | No retained texels; `Texture2D` transfer is accepted and ignored, with no cube and normally no volume resource |
| Backbuffer/render-target pixels and readback | ❌ | ❌ |
| Pixel-fidelity or performance claim | — | — |

HEADLESS may report that it accepts a state or draw shape so validation code can proceed. That is
not evidence that the feature rendered. STUB reports no graphics capability and must not be used as
a permissive fallback. See [`headless-renderer.md`](headless-renderer.md) and
[`stub-renderer.md`](stub-renderer.md).

## Three-argument `GraphicsDevice::Present`

With no rectangles and a zero override handle, the classic overload takes the ordinary
`Present()` path on every renderer. Shared code clips source requests to the live logical buffer,
validates destination intersection against the selected client's physical pixels, and retains the
original destination mapping. An unsupported request throws `NotSupportedException` rather
than presenting a different region.

| Renderer | Source rectangle | Destination rectangle | Foreign window |
|---|---:|---:|---:|
| `HEADLESS` | ◇ accepted and traced | ◇ accepted and traced | ◇ accepted and traced |
| `SOFTWARE` | ✅ clipped CPU source region | ⚙ native dirty-rectangle presenter | ⚙ native target adoption + presenter |
| `FNA3D` | ✅ forwarded to `FNA3D_SwapBuffers` (OpenGL, ES 3 and SDL GPU drivers; back-buffer filter, below) | ⚙ whole client only; a partial destination is an FNA3D-8 refusal | ❌ FNA3D-8 refusal |
| `SDLGPU`, `WEBGPU` | ✅ source sampled from the logical buffer | ✅ composed into retained GPU history | ⚙ GPU readback + native dirty-rectangle presenter |
| `OPENGL33` | ✅ crop, scale and composite into retained history | ✅ composed into history with the original mapping | ⚙ readback + native dirty-rectangle presenter |
| `OPENGLES3`, `WEBGL2` | ◇ implemented through the shared EasyGL path; not qualified on hardware or a browser | ◇ same | `WEBGL2` refuses a nonzero native override |
| `DIRECTX9` | ◇ native swap-chain `Present` with the original rectangles | ◇ same | ◇ native `HWND` forwarded to `Present`; pixel witnesses cannot run under Wine/DXVK (a presented client is not capturable there), so Windows hardware is required |
| `STUB` | ❌ | ❌ | ❌ |

SDLGPU and WEBGPU (GAP-08c): an own window keeps the previously presented frame in an owned GPU texture; an explicit
region composites into that history with the original destination mapping and the whole frame is presented. A borrowed
native window reads the selected source back and uses the platform's native dirty-rectangle presenter, so its previous
pixels survive; ordinary `Present()` keeps the swapchain path. Browser WebGPU keeps `#canvas` and refuses a nonzero native
override. Verified through the private runner on the real GPU (19 native pixel cases per renderer through three capture
transports, with source, destination, window, history and readback mutants each failing their witness).

HEADLESS's acceptance remains non-rasterizing and therefore makes no presentation-result claim.
SOFTWARE remains windowless by default; nonzero `PresentationParameters.DeviceWindowHandle`
explicitly attaches its own platform window/presenter. Destination/override presentation requires
that attachment. Native X11/Win32 dirty rectangles preserve previously presented pixels outside
the destination; full texture-backed swaps and other unqualified partial-surface paths refuse.
SDL dummy-surface coordinates, bytes and resize are tested; real native-client captures are built
and await private owner execution. Existing platform-known native windows can be reused; external
X11/Win32/Cocoa hosts are imported without transferring ownership. An unregistered Wayland
`wl_surface` cannot supply dimensions through this API and refuses; platform-known Wayland windows
retain their actual size information.

FNA3D forwards a source rectangle, and a destination equal to the whole client, to
`FNA3D_SwapBuffers`; native captures pass on the pinned fork's OpenGL driver, on it with
`FNA3D_OPENGL_FORCE_ES3`, and on its SDL GPU driver (`Fna3dPresentPixels.*`, CTest
`Gap08_Fna3dPresentedWindowPixels_<driver>`). FNA3D scales with its back-buffer filter, linear
unless `FNA3D_BACKBUFFER_SCALE_NEAREST` is set, so at a source edge inside the back buffer the two
outer client pixels of a 4x scale blend the neighbouring texel, where SDLGPU and WEBGPU sample only
the source. Everything else throws `NotSupportedException` naming dependency defect
[FNA3D-8](fna3d-dependency-defects.md#fna3d-8-region-and-override-presentation-produces-wrong-native-client-pixels):
a destination leaving part of the client uncovered (with the driver named; neither driver keeps the
pixels outside it), a foreign window (before it is adopted), and any rectangle on the D3D11 driver.
SOFTWARE native captures confirmed the region/foreign pixels; two own-window checks
needed separated clients to avoid X11 exposure artefacts, with the revised fixture awaiting rerun.


The native override identity is distinct from a legacy platform window token (an SDL pointer in
SDL3). XNA Refresh's public `Present` wrapper converts rectangle endpoints and passes the native
window unchanged (`GraphicsDevice.Present`, IL_0014–0082); clipping, scaling, untouched client
pixels and native error outcomes are delegated to Direct3D. A successful call, a HEADLESS trace or
backbuffer readback alone does not qualify the actual presented destination or selected window.

## Current renderer-specific boundaries

- **EasyGL (`OPENGLES3`, `OPENGL33`, `WEBGL2`)** — one implementation over distinct GL
  profiles. MSAA, anisotropy, native wireframe, numeric limits and several formats are live-context
  facts. Desktop GL supplies exact occlusion counts; GLES/WebGL may supply only a visibility flag.
  See [`opengl33-renderer.md`](opengl33-renderer.md),
  [`webgl2-renderer.md`](webgl2-renderer.md) and
  [`surface-format-support.md`](surface-format-support.md).
- **WebGPU** — supported on native Linux x86_64 and in headless Chrome. Windows, macOS and
  linux-aarch64 native packages remain unverified (`WEBGPU-1`). Wireframe is edge expansion;
  occlusion-query runs spanning render-pass segments are aggregated, with exact counts or only
  zero/non-zero visibility according to the adapter. See
  [`webgpu-renderer.md`](webgpu-renderer.md).
- **SDLGPU** — CNA's modern native route, verified on Linux/Vulkan and under Wine with SDL's D3D12
  path. Native Windows, macOS and iOS qualification remains open. SDL_gpu exposes no occlusion-query
  API; float/half-float 2D and cube targets use device-qualified native storage (implemented
  2026-09-10 in `8df965818` and `e236a8d33`); partial multisample masks are refused, and
  `PresentInterval::Two` maps to `One`. See [`sdl-gpu-renderer.md`](sdl-gpu-renderer.md).
- **Software** — deterministic CPU rasterization with optional explicit platform-window
  presentation, without a GPU context or guaranteed swap timing. It implements the classic rendering surface, exact CPU queries and optional compiled
  effects, but not source `ShaderEffect` execution. See
  [`software-renderer.md`](software-renderer.md).
- **DirectX9** — its stricter goal is exact XNA 4.0 behavior. The active 40-scene corpus matches
  under Wine+DXVK, not verified real Windows hardware. Float/half-float targets require native
  support for the specific 2D or cube format; `HdrBlendable` also requires post-pixel-shader
  blending support. The float targets are qualified under Wine/DXVK 3.1.1 through the private runner
  (GAP-10); no real Windows hardware run exists. Cube MSAA and multiple vertex streams are unsupported, and depth bias lacks a discriminating differential
  result. See [`directx9-renderer.md`](directx9-renderer.md) and
  [`d3d9-divergence-report.md`](d3d9-divergence-report.md).
- **FNA3D** — compiled effects are the native shader route. MSAA, stencil, queries and instancing
  depend on the selected FNA3D driver; stock-effect instancing is refused even when compatible
  compiled effects can instance. OpenGL and SDLGPU drivers are verified, while native D3D11
  execution remains open. See [`fna3d-renderer.md`](fna3d-renderer.md).

## Focused sources of truth

- resource and format boundaries:
  [`rendertarget-support.md`](rendertarget-support.md),
  [`texture3d-texturecube-support.md`](texture3d-texturecube-support.md) and
  [`surface-format-support.md`](surface-format-support.md);
- vertex input and state:
  [`vertex-format-support.md`](vertex-format-support.md),
  [`sampler-state-support.md`](sampler-state-support.md),
  [`rasterizerstate-support.md`](rasterizerstate-support.md) and
  [`depthstencilstate-support.md`](depthstencilstate-support.md);
- queries and effects:
  [`occlusionquery-support.md`](occlusionquery-support.md),
  [`fx-compiled-effects.md`](fx-compiled-effects.md) and the five stock-effect support pages listed
  in [`README.md`](README.md#graphics-apis-and-effects).

Historical campaign matrices, fixed follow-up lists and dated test-count snapshots were removed
from this page during the 2026-10-31 reconciliation. They remain available in Git history; current
limitations belong in renderer documents or `KNOWN_ISSUES.md` rather than being copied here.
