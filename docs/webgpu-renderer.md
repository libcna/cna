# WebGPU graphics renderer

## Status

Status: current native and browser contract, reconciled with the implementation, live capability
queries and known-defect ledger on **2026-11-01**, with the native presentation correction verified
on **2026-11-02** (CNA-FIX-D21).

`WEBGPU` is a **supported** CNA renderer identity. It uses pinned wgpu-native on desktop and
Emscripten's `emdawnwebgpu` port in a browser. Current execution evidence covers Linux x86_64 and
headless Chrome. Windows, macOS and linux-aarch64 native packages are configured and checksum-pinned
but are not qualified on their target hardware.

**Parity, stated precisely.** WebGPU implements the targeted CNA/XNA rendering surface on the
tested Linux/native and browser environments, including every stock effect, PBR extensions,
instancing, render targets, custom and compiled-effect shader routes, and browser presentation.
It is not project-wide parity certification: several operations have explicit capability or route
boundaries, optional features vary by adapter, and native platforms outside Linux x86_64 remain
unqualified.

Applications must therefore use the live renderer capability profile, numeric limits and
per-format queries. The explicit product defects and route-specific boundaries are in
[Important limitations](#important-limitations). The completed campaign plan and its
`WEBGPU-1` through `WEBGPU-209` row names remain in Git history rather than being repeated as a
task diary here; `WEBGPU-1` still names the platform-qualification boundary.

## Sources of truth

This page owns WebGPU-specific build, platform, rendering and lifecycle behavior. It deliberately
delegates moving inventories and shared contracts:

- [`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md) is the concise
  cross-renderer comparison.
- [`renderer-capability-profiles.md`](renderer-capability-profiles.md) defines the live query model.
- [`cross-renderer-parity-fixtures.md`](cross-renderer-parity-fixtures.md) owns the shared parity and
  conformance corpora.
- [`web-emscripten-graphics-limitations.md`](web-emscripten-graphics-limitations.md) owns the browser
  toolchain, canvas, main-loop, threading and persistence contract.
- [`shader-effect-vs-fx-bytecode.md`](shader-effect-vs-fx-bytecode.md) separates source
  `ShaderEffect` from compiled XNA Effect bytecode; [`fx-compiled-effects.md`](fx-compiled-effects.md)
  owns the bytecode runtime and its input restrictions.
- [`sampler-state-support.md`](sampler-state-support.md),
  [`occlusionquery-support.md`](occlusionquery-support.md) and the render-target documents own the
  corresponding shared XNA contracts.
- [`handoff.md` section 5](../handoff.md#5-known-bugs-and-limitations) is the sole current defect
  ledger. A historical WebGPU paragraph must not override it.

The implementation lives in `modules/renderers/webgpu/`; native dependency discovery is
`cmake/ThirdPartyWebGPU.cmake`, and renderer selection is in `cmake/RendererSelection.cmake`.

## Build and deployment

Select the renderer at configure time:

```bash
cmake -S . -B cmake-build-webgpu -G Ninja \
  -DCNA_GRAPHICS_RENDERER=WEBGPU \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-webgpu --parallel 8
```

CNA pins native WebGPU to **wgpu-native v29.0.1.1**. With an empty `CNA_WEBGPU_ROOT` and
`CNA_WEBGPU_AUTO_DOWNLOAD=ON` (the default), CMake selects the official host package, verifies its
pinned SHA-256 before extraction and caches it in the build tree. An unknown package/version hash
fails closed.

For an offline or externally managed package, provide an absolute extracted root:

```bash
cmake -S . -B cmake-build-webgpu -G Ninja \
  -DCNA_GRAPHICS_RENDERER=WEBGPU \
  -DCNA_WEBGPU_ROOT=/absolute/path/to/wgpu-native \
  -DCNA_WEBGPU_AUTO_DOWNLOAD=OFF \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-webgpu --parallel 8
```

The root may contain the official `include/webgpu/webgpu.h` layout, the older
`include/webgpu-headers/webgpu.h` layout or a directly discoverable `webgpu.h`, plus the matching
library under `lib/` or `bin/`. Changing `CNA_WEBGPU_ROOT` in an existing tree clears the cached
find result before resolving the new package.

Native executables receive the wgpu-native runtime next to the target. Linux uses a sibling
`libwgpu_native.so` through the target's runtime path; Windows receives `wgpu_native.dll`. An
installed C++ consumer must preserve the exported `WebGPU::WebGPU` dependency described by the CNA
package rather than linking an unrelated system version.

### Browser build

An Emscripten configuration does not download or copy wgpu-native. The same renderer sources link
`--use-port=emdawnwebgpu`, which routes the unified WebGPU C API to `navigator.gpu`:

```bash
emcmake cmake -S . -B cmake-build-wasm-webgpu -G Ninja \
  -DCNA_GRAPHICS_RENDERER=WEBGPU \
  -DCNA_BUILD_EXAMPLES=ON \
  -DCNA_BUILD_TESTS=OFF
cmake --build cmake-build-wasm-webgpu --parallel 8 --target cna_demo_2d
```

The browser platform requirements, HTTP MIME rules, Asyncify ownership and headless-Chrome
qualification boundary are in
[`web-emscripten-graphics-limitations.md`](web-emscripten-graphics-limitations.md). Node can prove
module loading, but it has no browser canvas and is not WebGPU execution evidence.

## Surface and renderer selection

Native WebGPU needs a real platform window that wgpu-native can turn into a surface. An SDL
`offscreen` window and `PresentationParameters.HeadlessEXT` do not provide a surface-free
backbuffer. Rendering into an offscreen `RenderTarget2D` after a windowed device exists is supported
and is a different operation.

Renderer availability means that an identity was compiled in, not that the current process can
create its device. A program that permits substitution must install an explicit fallback before
the first device is constructed:

```cpp
const std::vector<CNA::GraphicsRendererType> fallback{
    CNA::GraphicsRendererType::Software};
CNA::GraphicsRendererSelection::SetFallbackChain(fallback);
CNA::GraphicsRendererSelection::SetPreferred(CNA::GraphicsRendererType::WebGPU);
```

Without an opt-in fallback, surface/device creation failure propagates. With one, the failed WebGPU
attempt is retained in `GetFallbackHistory()` and `GetActive()` reports the identity that actually
succeeded. The complete precedence and latch rules are in
[`runtime-renderer-selection.md`](runtime-renderer-selection.md).

Native surface creation uses the platform window's Win32, X11, Wayland, Cocoa or Android handle
directly; the renderer does not depend on `sdl3webgpu`. On the browser target it creates a surface
from the `#canvas` CSS selector.

## Current rendering contract

The implementation is a deferred renderer: public calls capture resources, state, viewport,
scissor and effect values at call time; replay preserves the original draw/clear order. Render
target changes and readbacks flush the required work without regrouping draws by family.

| Area | Current WebGPU behavior |
|---|---|
| 2D and presentation | `SpriteBatch`, sorting, blend/sampler/rasterizer state, logical presentation, resize, readback and native/browser presentation are implemented. |
| Classic 3D | `BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect` and `SkinnedEffect` execute renderer-owned WGSL, including stock fog and XNA's opaque-black fallback for an enabled but unbound classic texture. |
| CNA PBR extensions | `PbrEffect` and `SkinnedPbrEffect`, material maps, IBL and shadows use the same renderer and capability model. |
| Draw paths | User, buffer-backed, indexed and instanced draws are implemented. Declaration-driven layouts, multiple vertex streams and per-instance streams are supported within the route boundaries below. |
| State | Blend equations/factors, per-target color masks, culling, scissor, viewport/depth range, depth bias, complete depth/stencil operations and multisample masks are captured per draw. |
| Resources | `Texture2D`, `TextureCube`, `Texture3D`, vertex/index buffers, 2D/cube render targets, mip chains, readback and compressed transfers are implemented. |
| Render targets | Per-target MSAA, mip regeneration after resolve, two through four 2D MRT attachments and sampled render targets are implemented. |
| Queries | Occlusion queries use native query sets, resolve buffers and asynchronous readback. Separate contiguous runs are aggregated into one logical query. |
| Custom shaders | WGSL `ShaderEffect` source is compiled and executed. Compiled XNA Effect bytecode is an independent opt-in route. |
| Wireframe | Triangle edges are expanded into a portable line list; the implementation is shared by native and browser targets. |

### Stock effects and vertex input

Stock dispatch is chosen from effect state and vertex semantics, not only a byte stride. Ordinary
families resolve each declaration by `VertexElementUsage` and semantic index, so reordered elements
and compatible split streams do not need a renderer-specific packed struct. Skinned input is
normalized into CNA's canonical GPU record when its declaration supplies the required position,
normal, UV, blend weights and byte-or-vector blend indices. PBR routes likewise validate the input
they consume instead of silently reinterpreting an unrelated stride.

Multiple input bindings and instance streams are real WebGPU vertex buffers. An instance frequency
larger than one is materialized so XNA's divisor behavior remains observable even where the native
step mode offers only vertex/instance. `GetMaxVertexStreams()` reports the smaller of the live
device limit and CNA's resolver limit. Base-instance drawing is a separate capability and remains
unsupported.

An absent texture on a classic stock effect samples opaque black when XNA does. PBR maps use their
own neutral material defaults: white for multiplicative/no-op maps and a flat tangent-space normal
for an absent normal map. The two rules are intentionally not interchangeable.

### Wireframe

Core WebGPU has no portable polygon mode. CNA converts every triangle in a wireframe draw to three
line edges at queue time and submits an indexed line list. This covers ordinary, indexed, user,
stock-effect, custom-effect, instanced and SpriteBatch polygon routes on both native and browser
targets. Line and point primitives are unchanged because they have no polygon interior.

An edge shared by two triangles is emitted twice. Depth bias is dropped for the expanded line draw
because WebGPU forbids polygon depth bias on line topology. `SpriteBatch::Begin` still follows XNA:
a null rasterizer argument selects its default solid state, so pass a wireframe state to `Begin`
when sprite outlines are wanted.

## Textures, formats and render targets

Support is classified by resource kind and use. A format can be sampleable but not renderable, or
renderable but unable to filter or blend without an optional adapter feature. Use
`GraphicsDevice::GetRendererSurfaceFormatSupportEXT` (per-usage `TextureStorage`, `Sampled`,
`Filterable`, `RenderTarget`, `Blendable`, … facts), `SupportsSurfaceFormatAsRenderTargetEXT`, the
rest of the renderer capability profile and the returned applied MSAA count instead of a
hard-coded table.

Stable WebGPU-specific boundaries are:

- `SurfaceFormat::Bgr565`, `Bgra5551` and `Bgra4444` are refused for `Texture2D` and
  `TextureCube`; WebGPU has no matching packed 16-bit color texture, and CNA does not report a
  widened RGBA8 allocation as the requested format.
- `NormalizedByte2` and `NormalizedByte4` use native signed-normalized storage where the XNA
  profile permits them. Signed-normalized textures can carry authored mips but cannot use CNA's
  render-pass mip generator.
- DXT1/3/5 and BC7, including the CNA sRGB variants, stay compressed on `Texture2D` and
  `TextureCube` when the device exposes `TextureCompressionBC`. Content loaders preserve the block
  stream on that route and fall back to the shared decoded path when native compressed transfer is
  unavailable.
- `Rgba64` render targets require the native `TextureFormat16bitNorm` feature or browser
  `texture-formats-tier1`, plus actual `RENDER_ATTACHMENT` usage. CNA probes both rather than
  inferring support from the enum.
- `Single`, `Vector2` and `Vector4` targets remain renderable/readable without the optional
  float32 features. Filtering them requires `Float32Filterable`; blending into them requires
  `Float32Blendable`. Only the dependent draw is refused when the feature is absent.
- Requested MSAA is reduced to an actually creatable count. A target owns and resolves its own
  sample count; 2D and cube targets can also regenerate their mip chains after unbinding.

Ordinary `Texture2D` and `TextureCube` storage uses explicitly authored mip levels, including
RGBA8 textures: uploading level zero does not invoke renderer mip generation. Supply each level
explicitly, including levels read from XNB content. Automatic mip regeneration belongs to mipped
render targets when they are unbound, after MSAA resolve where applicable.

### Multiple render targets

The public XNA ceiling is four color attachments. WebGPU accepts two through four compatible
`RenderTarget2D` objects and builds one render pass with a shared depth/stencil attachment. A custom
WGSL fragment shader can write `@location(0..N-1)`; stock effects and SpriteBatch write attachment
zero only, with write masks disabled on the remaining attachments. `Clear()` clears all bound
attachments, and MSAA resolves each independently.

Shared validation rejects null, foreign-device, duplicate or incompatible targets before the
active set changes. Any slot may hold a `RenderTargetCube` face (E01, 2026-11-02): each face slot
uses that cube's own attachment and, multisampled, resolves into the face, and every written slot
regenerates its mip chain. Binding a single target, a cube face or the backbuffer flushes and ends
the previous MRT set.

## Shader routes

### Source `ShaderEffect`

The live device reports `ShaderDialectEXT::Wgsl` and
`ExecutesShaderEffectSourceEXT() == true`. Invalid WGSL produces an effect compile error rather
than an uncaught device validation error. A draw snapshots its uniform and resource state, so later
setter calls do not change already queued work.

The legacy untyped string contract remains available:

| Element | Legacy convention |
|---|---|
| Entry points | vertex `vs_main`, fragment `fs_main` |
| Vertex locations | declaration element order, continuing across bound streams |
| Uniforms | one block at `@group(0) @binding(0)`, described by `DeclareUniformBlockEXT`; `World`, `View` and `Projection` are injected by those exact names |
| Optional 2D sample | sampler at binding 1 and `texture_2d<f32>` at binding 2 |

With `CNA_CNAEXT=ON`, explicitly typed WGSL packages use the reflected descriptor contract and
their declared vertex/fragment entry points. The renderer supports 2D, cube and volume resources
within that contract and refuses unbindable groups, arrays or resource dimensions by name. New
portable extension code should query `SupportsShaderLanguageEXT` and prefer typed payloads rather
than guessing from the renderer identity.

Both source contracts work on 3D, instanced and SpriteBatch paths. A legacy SpriteBatch shader uses
the fixed position/UV/color layout. Sprite geometry receives the normal `Begin` transform, but a
custom WGSL effect does not receive an automatically named `MatrixTransform` uniform.

### Compiled XNA Effects

`CNA_WEBGPU_COMPILED_EFFECTS=ON` enables the Direct3D 9 Effect Framework bytecode route and adds the
MojoShader dependency. It is off by default. MojoShader emits SPIR-V; native WebGPU consumes the
adapted module, while the Emscripten route translates it to WGSL before browser validation. The
same option therefore builds on both targets.

The public capability is false when the option is off, and construction is refused instead of
silently substituting a stock shader. Reflection, pass/state behavior, supported sampler types,
SpriteBatch inheritance and remaining shader-model refusals are maintained in
[`fx-compiled-effects.md`](fx-compiled-effects.md).

## Browser presentation and asynchronous work

Native callbacks are driven synchronously by pumping `wgpuInstanceProcessEvents()`. In an
Emscripten build, waits yield through Asyncify so JavaScript promises, adapter/device requests and
buffer maps can complete. CNA application targets that need this route receive the required
Asyncify configuration from the renderer-aware build helpers.

Native rendering acquires the current surface texture and presents it explicitly. A browser canvas
texture can expire at any task yield, including a readback or capability probe, so the browser path
renders into an owned backbuffer. `Present` then acquires the canvas, copies the finished image and
submits without waiting. This also preserves untouched pixels across partial draws. Resize follows
the acquired canvas texture's actual dimensions and recreates depth/MSAA attachments when needed.

The browser loop, canvas/CSS size distinction, C ABI embedding and platform qualification are
owned by [`web-emscripten-graphics-limitations.md`](web-emscripten-graphics-limitations.md).

## Queries and device loss

An occlusion query tags the draws between `Begin` and `End`. Each contiguous render-pass run gets a
native query slot, and readback aggregates all runs in the logical interval. Vulkan has been
measured returning an exact sample count; Metal returns visibility (zero or one). Other native and
browser adapters must be treated according to `PixelCountIsPreciseEXT()` rather than assumed exact.

The deterministic debug path can destroy and recreate the device, then restore ordinary registered
buffers, `Texture2D` resources and `RenderTarget2D` objects. Render-target pixel contents are
discarded on recreation. `DebugSimulateContextLoss()` deliberately refuses while a live
`TextureCube`, `RenderTargetCube`, `Texture3D`, occlusion query or custom `ShaderEffect` exists,
because those objects cannot yet rebuild their native handles. The driver-reported device-lost
callback currently records the diagnostic; asynchronous real-driver recovery is not a qualified
contract.

## Important limitations

These are current boundaries, not completed campaign history:

- **Platform qualification:** native execution is verified on Linux x86_64, and browser execution
  in headless Chrome. Windows, macOS and linux-aarch64 native packages remain unverified on their
  target hardware (`WEBGPU-1`). Browser evidence is Chrome-centric; Firefox, Safari and a broad
  physical-device matrix are not continuous gates.
- **A native surface is mandatory:** there is no surface-free WebGPU backbuffer. Use an explicit
  renderer fallback when the process may run without a presentable window.
- **Recovery is resource-dependent:** the live resource kinds named in the device-loss section
  block simulated recovery until they are disposed, and asynchronous recovery after a real
  driver-reported device loss is not qualified.
- **Base instance is unsupported.** Instancing itself, instance offsets and instance frequencies
  are implemented; the separate base-instance capability remains false.
- **Some fixed stock layouts require one per-vertex stream:** ordinary stock families, custom WGSL
  and compiled effects can bind split declarations, but the current skinned/PBR fixed-layout
  normalization routes refuse a split vertex rather than read only its first stream.
- **Optional formats are adapter facts:** packed 16-bit color textures are refused; BC,
  normalized 16-bit targets, float32 filtering/blending and applied MSAA depend on live features
  and probes as described above.
- **Metal partial sample masks:** a mask that keeps no samples is handled exactly and one that
  keeps all samples is ordinary; a partial mask is refused on wgpu-native's Metal backend because
  it cannot apply the requested pipeline sample mask.
- **LOD bias has named route limits:** stock XNA effects and compiled effects apply it in shader
  code and clamp it to WebGPU's practical range. Custom WGSL owns its own sample calls, and the PBR
  extension families currently accept the state without applying it. See
  [`sampler-state-support.md`](sampler-state-support.md).
- **Custom SpriteBatch WGSL has no automatic `MatrixTransform` uniform:** geometry is transformed,
  but a shader needing that named value must carry its own uniform.

## Validation

Never run the native GPU suite on the owner's live desktop. Build with at most eight jobs and use
the private compositor/GPU runner:

```bash
cmake --build cmake-build-webgpu --parallel 8
tools/platform/run_gpu_tests_private.sh cmake-build-webgpu \
  -L WebGPU --output-on-failure -j6
```

The shared rendered corpus is a separate, stable parity gate:

```bash
tools/platform/run_gpu_tests_private.sh cmake-build-webgpu \
  -R '^WebGPU_Parity_' --output-on-failure -j6
```

Browser harnesses must run in an actual browser, not Node-only module mode. The canvas-lifetime
driver requires Node 22+ with global `WebSocket` support. The standard smoke and canvas-lifetime
routes are:

```bash
tools/platform/run_gpu_tests_private.sh --exec \
  env CNA_WEBGPU_DEMO=cna_house3d_demo scripts/run-webgpu-browser-test.sh
tools/platform/run_gpu_tests_private.sh --exec \
  node scripts/run-webgpu-canvas-lifetime-test.mjs cmake-build-wasm-webgpu
```

Focused native programs under `modules/renderers/webgpu/examples/` cover formats, MSAA, render
targets, queries, custom/compiled effects, draw ordering, device recovery, browser lifetime and
performance cardinality. Their individual historical pass totals are intentionally not copied
here; CTest registration and the shared corpus files are the moving inventory.

`WebGPU_PresentationModeContract` runs the shared five-mode contract, including an asymmetric
Overscan marker that distinguishes cropping from distortion. `WebGPU_SpriteBatchPresentation`
checks logical scaling, custom/deferred viewports, user transforms and offscreen independence.
The renderer uses the logical extent supplied by `SpriteBatch` when it queues each sprite; it no
longer infers it from a physical viewport or the previously configured surface. Sprite geometry
compensates for WebGPU's bounded native viewport so Overscan clips the excess at the correct scale.
These native regressions also pass on EasyGL and SDLGPU; they add no browser or other-platform
qualification.

## Architecture summary

- Stock shaders are WGSL modules; per-draw effect data uses uniform buffers because WebGPU has no
  portable push constants.
- Immutable pipelines and bind groups are cached by every state that affects them. Transient
  vertex/uniform buffers use bounded pools; SpriteBatch uses retained buffers rather than creating
  one native object per draw. Pool reuse needs no explicit fence because writes and prior draws use
  the same FIFO device queue, while WebGPU keeps submitted resources alive through their use.
- The ordered replay stream is the single authority for draw, clear, query and target-transition
  ordering. A later state mutation cannot retroactively change an earlier command.
- WebGPU's `VertexElementFormat::Color` maps to normalized RGBA bytes, not a nonexistent BGRA
  vertex format.
- Fullscreen, input and window policy remain in the shared platform layer; the renderer consumes a
  surface and a presentation rectangle.

Historical implementation checkpoints belong to Git history; current behavior and the rationale
for durable choices belong here, in the focused subsystem pages and in the executable tests.
