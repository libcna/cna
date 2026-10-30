# SDLGPU renderer

`-DCNA_GRAPHICS_RENDERER=SDLGPU` maps XNA 4.0 onto SDL3's GPU API (`SDL_gpu.h`). SDL owns the
drivers underneath it: Vulkan on Linux and Android, Direct3D 12 on Windows, Metal on macOS and iOS.
Those are SDL's backends, not CNA renderer identities (`docs/renderer-registry.md`). `SDLGPU` is
the default renderer on Windows, macOS and iOS (`cmake/RendererIdentityDefault.cmake`). The source
is `modules/renderers/sdl-gpu/`.

The campaign that built it (`SDLGPU-1`…`SDLGPU-135`, 2026-09) is in Git history; comments cite its
rows as `SDLGPU-nnn`.

## Build options

| Option | Default | Effect |
|---|---|---|
| `CNA_SDLGPU_SHADERCROSS` | ON on Windows and Apple, OFF elsewhere | Links the pinned SDL_shadercross statically and translates the stock SPIR-V shaders to the DXBC or MSL that the D3D12 and Metal drivers consume (`SDLGPU-92`). Vulkan takes the SPIR-V directly. SDL is linked after every static archive that calls it, because GNU/MinGW resolves archives in one pass (`SDLGPU-98`). |
| `CNA_SDLGPU_COMPILED_EFFECTS` | OFF | Runs compiled XNA `Effect` bytecode through MojoShader's SDL_GPU adapter (`FX-061`); with ShaderCross on, the same route reaches D3D12 and Metal (`SDLGPU-93`). |

The stock shaders are GLSL under `src/shaders/`, compiled by `compile_shaders.py` (libshaderc) into
the committed `spirv_shaders.hpp`; regenerate it after editing a `.glsl` file. The CNAEXT
`ShaderEffect` takes raw SPIR-V and is available only where a target-native GLSL→SPIR-V compiler
and a SPIR-V driver exist, so it is off on Windows, Apple and Emscripten (`SDLGPU-97`). Its binding
layout is [`sdlgpu-shader-effect-layout.md`](sdlgpu-shader-effect-layout.md).

## Parity with EasyGL

The final sweep (2026-09-11) found every audited ordinary XNA 4.0 graphics family observably
equivalent to EasyGL on Linux/Vulkan and on Direct3D 12, except three things SDL_gpu cannot
express:

| Feature | SDLGPU behaviour |
|---|---|
| `BlendState.MultiSampleMask` | A mask keeping every sample or none is applied exactly; any other mask is refused, and `RendererFeature::MultiSampleMask` reports Restricted (CNA-DEMOS DC-R2, 2026-10-24). |
| `PresentInterval::Two` | SDL_gpu has no half-rate present mode; it presents with plain VSYNC, as `One` does. |
| `OcclusionQuery` | SDL_gpu has no query API. The renderer reports the limitation and refuses to construct a query rather than fabricate a result (`SDLGPU-80`). |

Parity covers the ordinary XNA surface only, not the CNAEXT graphics extensions. Where the audit
found EasyGL itself wrong, SDLGPU follows XNA/FNA instead of copying it; the one such EasyGL
defect still open is in [`../handoff.md`](../handoff.md) §5.1.

## Platform evidence

| Platform and SDL driver | Evidence |
|---|---|
| Linux, Vulkan | The full `SdlGpu_*` registration set, with Vulkan validation errors and warnings fatal (194/194 at the 2026-09-11 sweep). |
| Windows, Direct3D 12 | MinGW cross-builds under Wine with vkd3d-proton: headless devices and pipelines (`SDLGPU-99`…`SDLGPU-114`) and a real swapchain through a private compositor (`SDLGPU-94`), with D3D12 debug-layer errors and warnings fatal. Not run on Windows hardware. |
| macOS, Metal | The Mac mini M4 qualification (`AM4-*`, 2026-10-07/08): full tree 10,962/10,962 ([`apple-platforms.md`](apple-platforms.md)). |
| iOS, Metal | Never built ([`apple-platforms.md`](apple-platforms.md)). |
| Android, Vulkan | Not qualified (`SDLGPU-96`, [`android-graphics-limitations.md`](android-graphics-limitations.md)). |

## Design decisions

- **Vertex input comes from the `VertexDeclaration`**, not from the stride (`SDLGPU-59`). Rigid
  `PbrEffect` accepts records without a tangent and feeds the neutral tangent input (the
  2026-10-22 museum follow-up).
- **`SkinnedEffect`'s bone palette is an RGBA32F texture**, not a vertex storage buffer, because
  D3D12 rejected the storage-buffer pipeline (`SDLGPU-103`).
- **Sampler LOD bias is applied in the shaders.** SDL documents the native `mip_lod_bias` as a
  no-op on Metal, so stock shaders apply each slot's captured bias, and compiled effects have
  their combined-sampler SPIR-V rewritten (`SpirvSamplerLodBias`, `SDLGPU-121`/`122`). A zero bias
  keeps the implicit lookup, which Vulkan rounds differently (`SDLGPU-130`).
- **Compiled-effect pipelines live as long as their last user.** They are keyed by a monotonic
  program identity rather than a recyclable shader address (`SDLGPU-124`); every live `Effect` and
  every queued draw holds a lease, and releasing the last lease retires that program's pipelines
  (`SDLGPU-125`). Deferred draws keep their shader records until replay (`SDLGPU-118`), and live
  compiled runtimes are detached before the device is torn down (`SDLGPU-131`).
- **Immutable sampler and pipeline caches are bounded** to the 256 most recently used entries
  per family; an evicted handle goes through SDL's command-buffer-aware destruction
  (`SDLGPU-126`/`127`).
- **The backbuffer is what was asked for.** The requested `DepthFormat` becomes a real depth
  attachment, and the colour format reports `SurfaceFormat::Color` because SDL's swapchain is
  RGBA8/BGRA8 (`SDLGPU-132`); backbuffer MSAA honours `MultiSampleCount` (`SDLGPU-85`).

## Tests

The integration programs are in `modules/renderers/sdl-gpu/examples/` and register as `SdlGpu_*`
only when `SDLGPU` is the default renderer; the shared renderer-neutral parity fixtures are in
`modules/graphics/examples/parity/`. Every registration fails on a Vulkan validation or D3D12
debug-layer error or warning (`SDLGPU-115`).

`CNA_SDLGPU_TEST_VIDEO_DRIVER` selects the SDL video driver: `x11` on Linux, which receives
`CNA_TEST_DISPLAY`, and the native driver elsewhere. `offscreen` and `dummy` clear both `DISPLAY`
and `WAYLAND_DISPLAY` so no host display leaks in (`SDLGPU-116`). Run them through the private
runner, never on the live desktop:

```bash
tools/platform/run_gpu_tests_private.sh cmake-build-sdlgpu -R '^SdlGpu_' -j6
```
