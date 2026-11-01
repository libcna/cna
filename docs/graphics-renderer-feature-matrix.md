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
[`handoff.md` §5](../handoff.md#5-known-bugs-and-limitations).

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
| `RenderTarget2D` and `RenderTargetCube` | ✅ | ✅ | ✅ | ✅ | ✅ Color only | ✅ |
| MRT, ordered 2–4 attachments under HiDef | ⚙ | ⚙ | ⚙ | ✅ | ⚙ | ⚙ |
| Backbuffer and ordinary render-target readback | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Blend, depth/stencil, rasterizer and sampler state | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 16-bit and 32-bit indexed drawing | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

The public MRT ceiling is four attachments even when a native API exposes more. Reach permits one
attachment and therefore reports no MRT capability. Texture and target support is also
format-specific: use the live per-format profile before selecting a non-`Color`, compressed,
floating-point or filterable resource. FNA3D's compressed readback additionally depends on its
selected internal driver.

Stock-effect support does not establish every combined state. CNA-FIX-D01's shared 41-pixel fog
contract verifies the corrected SDLGPU alpha composition against EasyGL and Software controls;
see [the renderer boundary](sdl-gpu-renderer.md#parity-with-easygl) for the tested combinations and
platform qualification.

## Rasterizing families: conditional and differentiating features

| Feature | EasyGL | WebGPU | SDLGPU | Software | DirectX9 | FNA3D |
|---|---:|---:|---:|---:|---:|---:|
| MSAA | ⚙ live context | ⚙ live adapter | ⚙ live device | ✅ 4× CPU | ⚙ 2D targets; no cube MSAA | ⚙ selected driver |
| Wireframe | ⚙ native polygon mode | ◇ triangle-edge expansion | ✅ | ✅ | ✅ | ✅ |
| Multiple vertex streams | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ |
| Instanced drawing | ✅ | ✅ | ✅ | ✅ | ✅ | ⚙ compiled-effect shader + driver |
| Occlusion queries | ◇ exact or boolean by GL profile | ⚙ exact or visibility-only by adapter | ❌ | ✅ exact CPU count | ✅ | ⚙ selected driver |
| Float and half-float render targets | ⚙ per format/context | ⚙ per format/adapter feature | ❌ | ✅ | ❌ | ⚙ per format/driver |
| CNAEXT `ShaderEffect` execution | ✅ GLSL / GLSL ES | ✅ WGSL | ✅ SPIR-V package; ⚙ GLSL text | ❌ | ✅ HLSL | ❌ |
| Compiled XNA Effect bytecode | ⚙ opt-in | ⚙ opt-in | ⚙ opt-in | ⚙ opt-in | ⚙ opt-in | ✅ always on |

Compiled-effect support is controlled independently for each opt-in family and is off by default:
`CNA_EASYGL_COMPILED_EFFECTS`, `CNA_WEBGPU_COMPILED_EFFECTS`,
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
`Present()` path on every renderer. Shared code validates and clips a non-default request before
offering it to the active renderer; an unsupported request throws `NotSupportedException` rather
than presenting a different region.

| Renderer | Source rectangle | Destination rectangle | Foreign window |
|---|---:|---:|---:|
| `HEADLESS` | ◇ accepted and traced | ◇ accepted and traced | ◇ accepted and traced |
| `SOFTWARE` | ✅ clipped CPU source region | ❌ | ❌ |
| Every other active identity | ❌ | ❌ | ❌ |

HEADLESS's acceptance remains non-rasterizing and therefore makes no presentation-result claim.

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
  API; float/half-float targets are not advertised; partial multisample masks are refused, and
  `PresentInterval::Two` maps to `One`. See [`sdl-gpu-renderer.md`](sdl-gpu-renderer.md).
- **Software** — deterministic CPU rasterization with no physical window, GPU context or swap
  timing. It implements the classic rendering surface, exact CPU queries and optional compiled
  effects, but not source `ShaderEffect` execution. See
  [`software-renderer.md`](software-renderer.md).
- **DirectX9** — its stricter goal is exact XNA 4.0 behavior. The active 40-scene corpus matches
  under Wine+DXVK, not verified real Windows hardware. Render targets are `Color` only, cube MSAA
  and multiple vertex streams are unsupported, and depth bias lacks a discriminating differential
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
limitations belong in renderer documents or `handoff.md` §5 rather than being copied here.
