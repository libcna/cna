# sokol_gfx backend — capability boundary

`CNA_GRAPHICS_BACKEND=SOKOL` renders through [sokol_gfx](https://github.com/floooh/sokol), a
single-header GPU abstraction that itself dispatches onto OpenGL 4.1 core, GLES3, D3D11, Metal or
WebGPU. CNA keeps ownership of the SDL window and the game loop; this backend creates only the GPU
context inside it (`sokol_app` is deliberately unused). The implementation plan, task list and
design rationale live in [`../plan_sokol.md`](../plan_sokol.md).

**This backend is experimental.** It covers 2D in full plus vertex-coloured, textured and lit 3D
geometry; it is not comparable to EasyGL/Vulkan/D3D11 and must not be described as having XNA 3D
parity. Everything outside the boundary below fails loudly —
either a `std::runtime_error` naming the missing capability, or a `System::NotSupportedException`
raised by the shared layer because the backend creates no resource — never a silent no-op.

## Configure

```bash
cmake -S . -B cmake-build-sokol -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_BACKEND=SOKOL
```

| Option | Values | Default | Notes |
|---|---|---|---|
| `CNA_SOKOL_API` | `GLCORE`, `GLES3`, `D3D11`, `METAL`, `WGPU` | `GLCORE` | Which native API sokol_gfx dispatches onto. **Only `GLCORE` is implemented and verified**; the others resolve their `SOKOL_*` define and warn, but the context-creation path for a non-GL API does not exist yet and construction throws. |
| `CNA_SOKOL_GIT_TAG` | commit SHA | `27b4960…` | The pinned upstream sokol commit fetched at configure time. |
| `FETCHCONTENT_SOURCE_DIR_SOKOL` | path | — | CMake's own override; point it at an existing sokol checkout for offline builds. |

Requires the OpenGL development headers (`mesa-common-dev` / `libgl1-mesa-dev` on Debian/Ubuntu).
`find_package(OpenGL REQUIRED)` fails the configure deliberately if they are missing, rather than
letting the build reach a confusing `GL/gl.h: No such file or directory`.

## What works

| Feature | Status | Evidence |
|---|---|---|
| Window + GPU context + `sg_setup` lifecycle | ✅ | `Sokol_Smoke` checks A–F |
| `Clear` / `ClearColorAndDepth` / `ClearDepth` / `ClearStencil` / the combined variants | ✅ | `Sokol_Smoke`: the cleared colour is read back off the real back buffer |
| `Present()`, 60-frame loop, swap interval | ✅ | `Sokol_Smoke` |
| Back-buffer read-back (`GraphicsDevice::GetBackBufferData`) | ✅ | Both tests depend on it |
| `Texture2D` upload, mip levels, `GetData` | ✅ | `Sokol_2D`, plus the XNB content-loading tests |
| `SpriteBatch`: placement, source rect, scale, rotation, origin, both `SpriteEffects` flips | ✅ | `Sokol_2D` checks B, C, D, G — all pixel-verified |
| Colour tint (real per-channel multiply) | ✅ | `Sokol_2D` check E |
| `BlendState` (all thirteen `Blend` factors, all five `BlendFunction` ops, colour write masks) | ✅ | `Sokol_2D` check F proves `NonPremultiplied` and `Opaque` differ |
| `SamplerState`: all nine `TextureFilter` values, all three `TextureAddressMode` values, anisotropy | ✅ | Each filter maps to its own min/mag/mip triple; `Sokol_2D` exercises `PointClamp` |
| `VertexBuffer` / `IndexBuffer` (16- and 32-bit) upload and count round-trip | ✅ | `Sokol_Smoke` check E |
| Viewport and scissor rectangles | ✅ | Wired to `sg_apply_viewport` / `sg_apply_scissor_rect` |
| Back-buffer MSAA | ✅ | Negotiated with the GL context; reports the driver's *granted* count |
| Vertex-coloured 3D geometry (`DrawPrimitives`/`DrawIndexedPrimitives`, all five `PrimitiveType` values, 16- and 32-bit indices) | ✅ | `Sokol_3D` checks A, B |
| `BasicEffect.DiffuseColor` and `VertexColorEnabled` | ✅ | `Sokol_3D` checks C, D — a real per-channel multiply |
| Depth testing and depth writes (`DepthStencilState.DepthBufferEnable`/`WriteEnable`/`Function`) | ✅ | `Sokol_3D` check E — a real occlusion proof, both with and without the test |
| Face culling (`RasterizerState.CullMode`) | ✅ | `Sokol_3D` check G — a clockwise triangle survives and a counter-clockwise one vanishes |
| Arbitrary vertex layouts via `VertexDeclaration` (Position/Color/TextureCoordinate/Normal, usage index 0) | ✅ | The 3D pipeline is keyed on the real declaration, not on a fixed stride |
| Textured 3D draws (`BasicEffect.TextureEnabled`) with `DiffuseColor`/vertex-colour tint, alpha test, fog | ✅ | `Sokol_Lit3D` checks A-D |
| Lit 3D draws (`BasicEffect.LightingEnabled`): ambient + up to 3 real per-pixel Blinn-Phong directional lights, specular, emissive, alpha test, fog | ✅ | `Sokol_Lit3D` checks E-I -- real per-pixel lighting, not per-vertex |
| `SamplerState` for the 3D texture unit (`GraphicsDevice.SamplerStates[0]`) | ✅ | Read at draw time, same as every other backend |
| Virtual resolution / presentation scaling / window↔logical transforms | ✅ | Same geometry as EasyGL's `FixedHeightDynamicWidth` |
| `RenderTarget2D` bind/draw/sample (`SetRenderTarget`/`SetRenderTargets`, real colour + optional depth-stencil attachment, viewport/scissor reset on bind/unbind) | ✅ | `Sokol_RenderTarget_ViewportScissorReset`, `Sokol_RenderTarget2D_Depth`, `Sokol_RenderTarget_DepthStencilUsage`, `Sokol_RenderTarget_PassBoundary` |
| Sampling a `RenderTarget2D` as a texture, including a never-read-back target the same frame it was produced | ✅ | `Sokol_RenderTarget_ProducerConsumer`, `Sokol_RenderTarget_BackbufferConsumer`, `Sokol_RenderTarget_SamplingOrientation` — SpriteBatch and BasicEffect/AlphaTestEffect textured 3D alike, top-left logical orientation preserved |
| A brand-new `RenderTarget2D` usable immediately, no warm-up frame | ✅ | `Sokol_RenderTarget_FirstUse` |
| `TextureCube` storage (`SetData`/`GetData`, every declared mip level, all 6 faces) | ✅ | `CnaTests`' `TextureCubeTest` suite (49 tests), plus the XNB `TextureCubeReader` and CNJ cube content-loading tests |
| `Texture3D` storage (`SetData`/`GetData`, every declared mip level, box regions) | ✅ | `CnaTests`' `Texture3DTest` suite (39 tests), `CnjTexture3DTest`, `Texture3DTextureCubeContentTypeReaderTest` |
| Stencil test operations for 3D draws (`DepthStencilState.StencilEnable`/`StencilFunction`/`StencilPass`/`StencilFail`/`StencilDepthBufferFail`, masks, reference, two-sided mode) | ✅ | Wired into `Pipeline3DKey`/`Get3DPipeline`; `SpriteBatch` never requests stencil (matches XNA) |
| `RenderTargetCube` bind/draw/unbind, one face at a time, with per-face colour isolation and a shared depth-stencil buffer | ✅ | `Sokol_RenderTarget_PassBoundary` (C1/C2), `Sokol_RenderTarget_DepthStencilUsage` (U1-U4), plus the cube legs in `Sokol_RenderTarget_FirstUse`/`BackbufferConsumer` |
| `OcclusionQuery` (real `GL_SAMPLES_PASSED` sample count, GL-only) | ✅ | `Sokol_OcclusionQuery_Cycle`, `Sokol_OcclusionQuery_VisibleQuad`, `Sokol_OcclusionQuery_OccludedQuad` |
| `RasterizerState.DepthBias`/`SlopeScaleDepthBias` | ✅ | `Sokol_RasterizerState_DepthBias` -- a real coplanar-redraw proof, both constant and slope-scaled bias |
| `RenderTarget2D` MSAA + resolve (`MultiSampleCount` > 1) | ✅ | `Sokol_RenderTarget2D_Msaa` -- a real differential anti-aliasing proof (a solid binary edge at `MultiSampleCount=0` vs. genuinely blended pixels at `8`), not just "resolve doesn't corrupt solid colours" |
| `Viewport.MinDepth`/`MaxDepth` | ✅ | `Sokol_Viewport_MinMaxDepth` -- REMED-GFX-079's 25-check backend-agnostic 3D-viewport oracle, including a real depth-remap proof (compressing `MaxDepth` pulls a farther quad in front of a nearer one) |
| `GetBackBufferData` while a `RenderTarget2D`/`RenderTargetCube` face is bound (reads the bound target's own content) | ✅ | Same oracle's check G; matches `EasyGLGraphicsBackend::ReadBackbuffer`'s "read from whatever's bound" convention |

## What does not work yet

| Feature | Behaviour today | Tracked as |
|---|---|---|
| Dual-texture, environment-mapped, skinned or PBR 3D shading | throws, naming the unsupported combination | Phase 5 |
| Instanced draws | throws | Phase 5 |
| Per-vertex (Gouraud) lighting (`BasicEffect.PreferPerPixelLighting = false`) | ignored -- always renders per-pixel, matching every CNA backend except D3D9 | not planned |
| A lit draw whose `VertexDeclaration` has a Normal but no TextureCoordinate (or vice versa) | throws -- see the source comment on why both are required together | not planned |
| Vertex elements other than Position/Color at usage index 0 (Normal, TexCoord, …) | ignored by the colored-3D pipeline | `SOKOL-22` |
| `RenderTarget2D::GetData` (direct CPU readback) | throws `System::NotSupportedException` — `SokolRenderTargetBackend` does not override `ITextureBackend::GetData`, so it inherits the base class's `return false` default. Sampling the target as a texture, or reading the backbuffer after drawing it there, both work. | `SOKOL-26` |
| `RenderTarget2D` mip-mapped (`mipMap=true`) | `CreateRenderTarget2D` throws `NotYetImplemented` | `SOKOL-26` |
| `RenderTargetCube::GetData` (direct CPU readback) | throws `System::NotSupportedException`, same boundary as `RenderTarget2D::GetData` | `SOKOL-26` |
| `RenderTargetCube` mip-mapped | throws `NotYetImplemented` | `SOKOL-26` |
| `RenderTargetCube` MSAA | `multiSampleCount` is always silently clamped to 1/ignored -- **a permanent sokol_gfx API boundary, not a "not implemented yet" gap**: its own validation layer hard-rejects a `SG_IMAGETYPE_CUBE` image with `sample_count > 1` (`VALIDATE_IMAGEDESC_ATTACHMENT_MSAA_CUBE_IMAGE`), confirmed empirically (a real `[sg][panic]` validation abort) while prototyping the same per-face multisample + resolve layout `RenderTarget2D` uses. The same kind of declared boundary `WebGPUGraphicsBackend`/`D3D9RenderTargetCubeBackend` report for their own reasons | `SOKOL-26` (closed as a permanent gap) |
| MRT (`SetRenderTargets` with more than one binding) | throws `NotYetImplemented` | `SOKOL-26` |
| Custom `Effect` via `SpriteBatch.Begin(effect)` / `ShaderEffect` | `CreateEffectBackend` returns null | `SOKOL-28` |
| `RasterizerState.FillMode` (`WireFrame`) | accepted and ignored — sokol_gfx exposes no polygon fill mode at all, unlike EasyGL's CPU-side triangle-to-`GL_LINES` re-expansion at draw time (not implemented here). A permanent, not-just-"not yet" gap | `SOKOL-23` |
| `BlendState.MultiSampleMask` | ignored — sokol_gfx has no per-sample coverage mask (it exposes alpha-to-coverage only) | no upstream API |
| `CNA_SOKOL_API` other than `GLCORE` | configure warns; construction throws | `SOKOL-31` |

`GraphicsDevice::SupportsCapability()` reports this boundary, so a game can query ahead of time
instead of catching. Two entries need reading carefully: `MultiSampleAntiAliasing` is `true` for the
**back buffer only** (there are no render targets here to multisample), and `ThreeD` is `true`
because the 3D pipeline genuinely exists — it does not promise that every stock effect shades
correctly, which the table above is the authority on.

## Known limitations inside the supported set

- **16384 sprite quads per frame.** The sprite streaming buffer is allocated once at construction
  and sized to exactly the largest run a uint16 index buffer can address. A frame that exceeds the
  cap raises `"Sokol backend: exceeded the per-frame sprite capacity of 16384 quads"` rather than
  silently dropping sprites.
- **Every texture and buffer upload recreates its GPU resource.** sokol_gfx allows at most one
  `sg_update_image()`/`sg_update_buffer()` per resource per frame, which a caller writing several
  mip levels — or re-uploading a streaming vertex buffer — in one frame violates immediately.
  Creating an immutable resource with initial data carries no such restriction, so that is what
  every upload does. Correct in all cases, but it allocates; `SOKOL-24` covers improving it.
- **Back-buffer read-back is GL-only** (`glReadPixels`). sokol_gfx has no read-back API, so any
  other `CNA_SOKOL_API` refuses `ReadBackbuffer` instead of returning fabricated pixels.
- **`TextureCube` allocates no GPU resource at all** (`SokolTextureCubeBackend` is CPU storage
  only). Nothing on this backend samples a cube map yet -- there is no cube shader variant, and
  `EnvironmentMapEffect`'s 3D draw path throws `NotYetImplemented` -- so a real `sg_image` would be
  a resource with no consumer. `SetData`/`GetData` round-trip exactly; a future `EnvironmentMapEffect`
  implementation is what would add the matching `sg_image`/view.
- **`Texture3D` allocates no GPU resource either** (`SokolTexture3DBackend` is the same CPU-storage
  shape as `TextureCube` above, one flat voxel buffer per mip level). Nothing on this backend samples
  a volume texture yet, so `SetData`/`GetData` round-trip exactly with no GPU-visible effect.
- **`RenderTargetCube`, unlike the plain `TextureCube` above, DOES allocate a real `sg_image`** --
  it exists specifically to be rendered into, so the resource has a genuine consumer even before
  any shader samples it back. Its depth-stencil buffer is a single 2D image shared by all six
  faces (matching FNA3D's own convention), not six separate per-face buffers, so a depth/stencil
  test on one face is gated by whatever an earlier bind of a *different* face last wrote there.
- **Every distinct vertex layout, topology and render state combination creates a pipeline
  object.** sokol_gfx bakes all of them into the pipeline, including the index type, so the cache
  is keyed on the full set. A scene that cycles through many combinations grows the cache; nothing
  evicts from it for the lifetime of the device.
- **Any raw GL call this backend makes outside sokol_gfx (occlusion queries, `ReadBackbuffer`'s
  `glReadPixels`) must never leave a GL error pending.** sokol_gfx's own GL backend asserts
  `glGetError() == 0` after routine internal calls, so an error this code sets and never consumes
  surfaces later as a hard abort at a completely unrelated call site (measured: `glBeginQuery`
  called twice with no intervening `glEndQuery` set `GL_INVALID_OPERATION` that stayed pending
  until `sg_shutdown()` destroyed the first tracked buffer, minutes of wall-clock and dozens of
  unrelated GL calls later). `SokolOcclusionQueryBackend` guards every raw call this way -- see its
  own `Begin()`/`hasResult_` doc comments.
- **The stencil reference value is baked into the pipeline object, not applied dynamically.**
  Unlike most graphics APIs (and unlike XNA's own `GraphicsDevice.ReferenceStencil`, a per-draw
  value), sokol_gfx's `sg_stencil_state.ref` is part of `sg_pipeline_desc` and has no separate
  "set the current stencil ref" call. `Pipeline3DKey` includes it, so a scene that changes
  `ReferenceStencil` between otherwise-identical stencil-testing draws creates one pipeline per
  distinct value rather than reusing one.
- **A multisampled `RenderTarget2D` allocates a genuinely separate resolve image**, following
  sokol_gfx.h's own documented offscreen-MSAA workflow: the multisample colour (and, when
  requested, depth-stencil) image is never sampled directly and its content need not survive past
  `sg_end_pass()` (`SG_STOREACTION_DONTCARE`); only the single-sample resolve image -- the same
  image `GetColorImageIdEXT()`/`GetColorTextureViewIdEXT()` always named, MSAA or not -- is what a
  later pass samples. Every pipeline (sprite and 3D alike) now keys on the *active pass's* real
  sample count rather than the window's, since sokol_gfx bakes `sample_count` into the pipeline and
  rejects a mismatch against the pass it draws into -- a target's own MSAA count is independent of
  the swapchain's.

## Verification status

All CTest entries below run under Xvfb with Mesa's **llvmpipe software GL** on this dev machine — a
real GL 4.1 driver and real rendering, but not discrete-GPU hardware. `SOKOL-30` tracks running them
against a real GPU.

```bash
ctest --test-dir cmake-build-sokol -R Sokol --output-on-failure
```

| Test | Checks | Result |
|---|---|---|
| `Sokol_Smoke` | 13 | all pass |
| `Sokol_2D` | 15, every one a real pixel read-back | all pass |
| `Sokol_3D` | 10, nine of them real pixel read-backs | all pass |
| `Sokol_Lit3D` | 10, every one a real pixel read-back | all pass |
| `Sokol_RenderTarget_ViewportScissorReset` | 6 | all pass |
| `Sokol_RenderTarget2D_Depth` | 1, a real depth-occlusion proof inside a render target | all pass |
| `Sokol_RenderTarget_DepthStencilUsage` | 29, incl. 4 cube legs (U1-U4) | all pass |
| `Sokol_RenderTarget_PassBoundary` | 30, incl. 4 cube legs (C1/C2) | all pass |
| `Sokol_RenderTarget_ProducerConsumer` | 27 | all pass |
| `Sokol_RenderTarget_BackbufferConsumer` | 3 real, the rest honestly INFO-skipped (no direct `RenderTarget2D`/`RenderTargetCube::GetData`) | all pass |
| `Sokol_RenderTarget_FirstUse` | 16, incl. a cube leg (N) | all pass |
| `Sokol_RenderTarget_SamplingOrientation` | 29 | all pass |
| `Sokol_OcclusionQuery_Cycle` | Begin/End/IsComplete/PixelCount plus every invalid-call-sequence and dispose-while-active case | all pass |
| `Sokol_OcclusionQuery_VisibleQuad` | 3, real BasicEffect + depth-tested geometry | all pass |
| `Sokol_OcclusionQuery_OccludedQuad` | 3, real BasicEffect + depth-tested geometry | all pass |
| `Sokol_RasterizerState_DepthBias` | 4, a real coplanar-redraw proof | all pass |

The render-target fixtures above are shared, backend-agnostic oracles also registered for EasyGL/
Vulkan/bgfx/SDL_GPU/etc.; SOKOL reuses them rather than duplicating bespoke tests. Most of their
checks assert `System::NotSupportedException` from a direct `RenderTarget2D::GetData()` call — see
the "What does not work yet" table — which every one of these fixtures treats as a legitimate,
distinctly-asserted backend declaration rather than a failure. The checks that DO exercise real
pixel content on SOKOL go through `SpriteBatch`/3D sampling or `GetBackBufferData()` instead.

The full `CnaTests` suite also runs under this backend. Note that the shared suite gates several
capability-dependent expectations on an explicit list of backend macros (`kCubeStorageSupported`
and friends); `CNA_BACKEND_SOKOL` was added to those lists for the cube-texture and multi-render-
target gaps documented above, exactly as `SDL_RENDERER`/`ASCII`/`CANVAS`/`DX3`/`HEADLESS` already
are.

### Environment note

This sandbox's Xvfb intermittently refuses `SDL_InitSubSystem(SDL_INIT_VIDEO)` with "No available
video device" — measured at 105 failures in 400 attempts by a **standalone SDL3 program that links
neither CNA nor sokol**. Any window-creating CNA test can therefore abort at start-up here roughly
one run in ten. That is an environment property, not a backend defect; re-run the test.
