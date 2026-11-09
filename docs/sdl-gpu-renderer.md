# SDLGPU renderer

`-DCNA_GRAPHICS_RENDERER=SDLGPU` maps XNA 4.0 onto SDL3's GPU API (`SDL_gpu.h`). SDL owns the
drivers underneath it: the usual native routes are Vulkan on Linux/Android, Direct3D 12 on
Windows and Metal on macOS/iOS. Actual driver selection depends on SDL, shader formats and the
host; a platform name alone does not establish a usable device or qualification.
Those are SDL's backends, not CNA renderer identities ([renderer registry](renderer-registry.md)). `SDLGPU` is
the default renderer on Windows, macOS and iOS (`cmake/RendererIdentityDefault.cmake`). The source
is `modules/renderers/sdl-gpu/`.

The campaign that built it (`SDLGPU-1`…`SDLGPU-135`, 2026-09) is in Git history; comments cite its
rows as `SDLGPU-nnn`.

## Build options

| Option | Default | Effect |
|---|---|---|
| `CNA_SDLGPU_SHADERCROSS` | ON on Windows and Apple, OFF elsewhere | Links the pinned SDL_shadercross statically and translates SPIR-V to formats accepted by the selected SDL device, including the configured D3D12/Metal routes (`SDLGPU-92`). Vulkan can take SPIR-V directly. SDL is linked after the renderer/ShaderCross archives that call it (`SDLGPU-98`). |
| `CNA_SDLGPU_COMPILED_EFFECTS` | OFF | Runs compiled XNA `Effect` bytecode through MojoShader's SDL_GPU adapter (`FX-061`); with ShaderCross on, the same route reaches D3D12 and Metal (`SDLGPU-93`). |

The stock shaders are GLSL under `modules/renderers/sdl-gpu/src/shaders/`, compiled by
`compile_shaders.py` (libshaderc) into
the committed `spirv_shaders.hpp`; regenerate it after editing a `.glsl` file. CNAEXT
`ShaderEffect` has two distinct routes. With `CNA_CNAEXT=ON`, an explicit `ShaderCodeEXT` or
`ShaderPackageEXT` supplies SPIR-V directly in every live SDLGPU build; the renderer remaps its
descriptors and uses ShaderCross when the selected SDL driver needs DXBC or MSL. The legacy string
constructor accepts GLSL text only in builds with the derived `CNA_SDLGPU_SHADER_EFFECTS`
definition and runtime `libshaderc`; that route is off on Windows, Apple and Emscripten
(`SDLGPU-97`). Its binding layout is
[`sdlgpu-shader-effect-layout.md`](sdlgpu-shader-effect-layout.md).

## Parity with EasyGL

The 2026-09-11 campaign compared its named ordinary-XNA fixtures with EasyGL on Linux/Vulkan
and Direct3D 12. That finite result is not a guarantee for every asset/state combination or every
platform. The following XNA limitations remain explicit:

| Feature | SDLGPU behaviour |
|---|---|
| `BlendState.MultiSampleMask` | On a multisampled target, a mask keeping every active sample or none is applied; a partial active-sample mask is refused when drawing. Single-sample targets ignore the mask. `RendererFeature::MultiSampleMask` reports Restricted (CNA-DEMOS DC-R2, 2026-10-24). |
| `PresentInterval::Two` | SDL_gpu has no half-rate present mode; it presents with plain VSYNC, as `One` does. |
| `OcclusionQuery` | SDL_gpu has no query API. The renderer reports the limitation and refuses to construct a query rather than fabricate a result (`SDLGPU-80`). |

CNA-FIX-D01 corrects fog alpha composition in seven stock fragment paths: RGB blends toward
`FogColor * composedAlpha`, with completed fragment alpha preserved. The registered
`SdlGpu_CrossFamily_StockEffectFogContract` checks 41 pixels across the five classic stock effects,
including effect/texture alpha, both DualTexture alphas, BasicEffect vertex alpha and lit/unlit
texturing, and textured/untextured SkinnedEffect. Before the fix it passed 32/41; after it, SDLGPU,
EasyGL and Software each pass 41/41. Regeneration changes exactly the seven intended fragment
arrays; the other 33 SPIR-V arrays are byte-identical. This is Linux/Vulkan evidence, not new
Direct3D 12/Metal qualification.

Use current GraphicsDevice capability/format queries for the active device. The shared device
derives `FloatRenderTargets` and `HalfFloatRenderTargets` from format support; an internal
renderer capability default is not the public device answer. Neither an SDL driver name nor a
historical fixture sweep establishes every format capability.

Parity covers the ordinary XNA surface only, not the CNAEXT graphics extensions. SDLGPU agrees
with CNA-FIX-D24's genuine-XNA One/Zero blend-function witness: the colour/alpha equations remain
independent, and Min/Max with those factors are refused. The shortcut is corrected in EasyGL,
Software and CNA's FNA3D adapter. Any current renderer defect is recorded in
[KNOWN_ISSUES.md](../KNOWN_ISSUES.md); an EasyGL comparison is not an independent Microsoft oracle.

## Platform evidence

| Platform and SDL driver | Evidence |
|---|---|
| Linux, Vulkan | The 2026-10-31 Debug sweep covered all 392 registered `SdlGpu_*` cases: 387 passes and five declared contract skips, with no failures. Validation errors/warnings were fatal where the fixture enabled/captured the corresponding diagnostics. This does not qualify disabled ShaderCross/compiled-effect legs. |
| Windows, Direct3D 12 | MinGW cross-builds under Wine with vkd3d-proton: headless devices and pipelines (`SDLGPU-99`…`SDLGPU-114`) and a real swapchain through a private compositor (`SDLGPU-94`), with D3D12 debug-layer errors and warnings fatal. A hosted native-MSVC smoke workflow exists, but no successful run is recorded and neither route is Windows-hardware qualification. |
| macOS, Metal | Historical evidence: the Mac mini M4 campaign (`AM4-*`, 2026-10-07/08) passed the full tree 10,962/10,962 before the 2026-10-13 native-renderer retirement. SDLGPU has not been re-qualified on Apple hardware since ([`apple-platforms.md`](apple-platforms.md)). |
| iOS, Metal | Never successfully built with SDLGPU; the non-gating CI leg is an attempt, not qualification ([`apple-platforms.md`](apple-platforms.md)). |
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
- **Backbuffer readback and split-frame flushes use a CNA-owned proxy.** SDL's swapchain texture
  remains write-only. A `GetBackBufferData` request or a pending-frame flush before Present enables
  persistent proxy-backed rendering; presentation blits the proxy to the swapchain. A windowed
  game that needs neither can stay on the direct swapchain path. Headless presentation also uses
  a proxy, so absence of readback alone does not prove that no proxy is allocated. Once enabled,
  this renderer keeps the proxy path, recreating the texture as its dimensions/format require.
  Readback addresses the requested
  logical backbuffer rather than a letterboxed or live-window viewport (`GFX-165`, formerly the
  open swapchain leg of `SDLGPU-39`).
- **CNJ and native CNB Effect assets are intentionally different contracts.** CNJ carries the
  renderer-dialect source used by `ShaderEffect`. The CNB container reserves an Effect identifier
  but defines no asset schema because CNA has not chosen XNA Effect Framework bytecode,
  renderer-specific source, or a new renderer-neutral payload as that wire contract. Source-to-CNB
  therefore refuses explicitly; this is not an SDLGPU compatibility failure.
- **The backbuffer is what was asked for.** The requested `DepthFormat` becomes a real depth
  attachment, and the colour format reports `SurfaceFormat::Color` because SDL's swapchain is
  RGBA8/BGRA8 (`SDLGPU-132`); backbuffer MSAA honours `MultiSampleCount` (`SDLGPU-85`).

## Tests

The integration programs are in `modules/renderers/sdl-gpu/examples/` and register as `SdlGpu_*`
when that examples subdirectory is enabled with `SDLGPU` as the default renderer; separately
registered shared shader/source-layout checks can also use the `SdlGpu_` prefix. The shared
renderer-neutral parity fixtures are in
`modules/graphics/examples/parity/`. GPU fixture registrations use failure patterns for captured
Vulkan validation or D3D12 debug-layer errors/warnings when the corresponding layer is enabled
(`SDLGPU-115`); check the individual registration's failure patterns and debug-layer availability. Test
counts in the platform table are dated evidence, not a fixed inventory; use
`ctest -N -R '^SdlGpu_'` in the current build tree. Validation-specific assertions require a Debug
build, while the same fixtures retain their functional pixel checks in other configurations.

`CNA_SDLGPU_TEST_VIDEO_DRIVER` selects the SDL video driver: `x11` on Linux, which receives
`CNA_TEST_DISPLAY`, and the native driver elsewhere. `offscreen` and `dummy` clear both `DISPLAY`
and `WAYLAND_DISPLAY` so no host display leaks in (`SDLGPU-116`). Run them through the private
runner, never on the live desktop:

```bash
cmake --build cmake-build-sdlgpu --parallel 4
tools/platform/run_gpu_tests_private.sh cmake-build-sdlgpu -R '^SdlGpu_' -j6
```

Build the selected fixture targets (or the whole existing configuration as above) before testing;
the private runner does not rebuild executables or synchronize an old example with a newer shared
library.

## Current verification

Rechecked on 2026-11-01 against renderer selection, shader intake, capability/mask handling,
cache limits, backbuffer proxy paths and fixture registration. In the existing Linux Debug tree
(ShaderCross and compiled effects OFF), CNA-FIX-D01 rebuilt all 375 executable targets behind the
393 registered `SdlGpu_*` cases. The final private Vulkan run has **391 passed, two skipped, zero
failed**, with no graphics-profile deaths. The two skips are the unsupported occlusion-query
precision/reuse cases. The expanded fog fixture passes 41/41 on SDLGPU, with independent EasyGL
and Software controls also passing 41/41. This does not add Windows/Apple/mobile hardware or
disabled optional-feature qualification.
