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

## What does not work yet

| Feature | Behaviour today | Tracked as |
|---|---|---|
| Dual-texture, environment-mapped, skinned or PBR 3D shading | throws, naming the unsupported combination | Phase 5 |
| Instanced draws | throws | Phase 5 |
| Per-vertex (Gouraud) lighting (`BasicEffect.PreferPerPixelLighting = false`) | ignored -- always renders per-pixel, matching every CNA backend except D3D9 | not planned |
| A lit draw whose `VertexDeclaration` has a Normal but no TextureCoordinate (or vice versa) | throws -- see the source comment on why both are required together | not planned |
| Vertex elements other than Position/Color at usage index 0 (Normal, TexCoord, …) | ignored by the colored-3D pipeline | `SOKOL-22` |
| `RenderTarget2D`, `RenderTargetCube`, MRT | `SetRenderTargets` with any target throws; `CreateRenderTarget2D` returns null, so the shared layer raises `NotSupportedException` | `SOKOL-25`/`SOKOL-26` |
| `TextureCube`, `Texture3D` | no resource created; `SetData`/`GetData` raise `NotSupportedException` | `SOKOL-27` |
| Custom `Effect` via `SpriteBatch.Begin(effect)` / `ShaderEffect` | `CreateEffectBackend` returns null | `SOKOL-28` |
| `OcclusionQuery` | `CreateOcclusionQuery` returns null — sokol_gfx exposes no query API at all | `SOKOL-29` |
| `RasterizerState` fill mode and depth bias | accepted and ignored — sokol_gfx exposes no polygon fill mode, and depth bias is not yet wired into any pipeline. Cull mode *is* honoured | `SOKOL-23` |
| Stencil test operations | `ApplyDepthStencilState` records the depth half only; stencil is not in the pipeline key | `SOKOL-23` |
| `BlendState.MultiSampleMask` | ignored — sokol_gfx has no per-sample coverage mask (it exposes alpha-to-coverage only) | no upstream API |
| `Viewport.MinDepth` / `MaxDepth` | ignored — `sg_apply_viewport` carries no depth range | `SOKOL-21` |
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
- **Every distinct vertex layout, topology and render state combination creates a pipeline
  object.** sokol_gfx bakes all of them into the pipeline, including the index type, so the cache
  is keyed on the full set. A scene that cycles through many combinations grows the cache; nothing
  evicts from it for the lifetime of the device.

## Verification status

All three CTest entries run under Xvfb with Mesa's **llvmpipe software GL** on this dev machine — a real
GL 4.1 driver and real rendering, but not discrete-GPU hardware. `SOKOL-30` tracks running them
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
