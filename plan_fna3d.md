# plan_fna3d.md — the FNA3D graphics renderer

Ledger for CNA's **42nd** public renderer identity, `FNA3D`. Companion documents:
`docs/fna3d-renderer.md` (capability boundary and validation status),
`modules/renderers/fna3d/effects/README.md` (stock-effect provenance),
`fna3d-spike/README.md` (the existence gates).

## Why this renderer

CNA reimplements XNA 4.0. FNA3D is the graphics library **FNA** — the reference implementation
CNA's own CLAUDE.md names as authoritative — renders through, and its API is XNA 4.0's device
model in C. Two consequences follow that no other CNA renderer gets:

1. Every FNA3D enumeration is numerically the XNA enumeration CNA already ports, so the state
   mapping is the identity and can be *proved* with `static_assert` rather than transcribed.
2. FNA3D executes XNA's own compiled stock effects through MojoShader, so this is the one CNA
   renderer whose `BasicEffect`/`SkinnedEffect`/… are Microsoft's actual shader programs rather
   than a reimplementation.

Like `LLGL`, `DILIGENT`, `SOKOL` and `BGFX`, FNA3D names a portable middleware layer rather than a
native API: it picks SDL_GPU, Direct3D 11 or OpenGL at runtime. That is still one CNA identity.

## Design decisions

1. **Fetched and pinned, not vendored.** `cmake/ThirdPartyFNA3D.cmake` pulls FNA3D at release tag
   26.08 (`3240147`) with its MojoShader submodule and builds it as a static archive, the same
   shape `ThirdPartySokol.cmake` uses. FNA3D's only dependency is SDL 3.2.0+, which is exactly the
   SDL3 CNA already vendors, so it resolves `SDL3::SDL3` from CNA's own imported target — no second
   SDL build and no system SDL requirement.
2. **Stock effect binaries are committed.** FNA3D exposes no shader entry point except
   `FNA3D_CreateEffect`, which takes a compiled Direct3D 9 Effect binary; nothing anywhere in the
   library compiles shader source, and the drivers refuse to draw without a bound MojoShader
   program. The six XNA stock effects are therefore committed under
   `modules/renderers/fna3d/effects/` (106 KB, Ms-PL, full provenance in that directory's README)
   and embedded into a generated header at build time. They are behavioural contract, not build
   input: the renderer's `ShaderIndex` arithmetic is only correct against these exact artefacts.
   They are not rebuildable here — `.fx` → `.fxb` needs `fxc`, which exists on no platform CNA
   builds on.
3. **The driver decides the window flags.** `FNA3D_PrepareWindowAttributes()` both selects the
   driver and reports the SDL flags it needs, and it must run before `SDL_CreateWindow` because it
   also primes the GL attributes the visual is chosen from. `GraphicsDevice`'s existing
   `getRendererWindowFlags()` gained one `#ifdef CNA_RENDERER_FNA3D` block calling
   `Fna3d::Detail::PrepareWindowFlags()` — the same runtime-decides-the-flag shape LLGL, Diligent
   and bgfx already use, through a header free of SDL and FNA3D includes.
4. **Real per-stream vertex declarations.** FNA3D binds an `FNA3D_VertexDeclaration` per stream
   rather than deriving a layout from a byte stride, so the caller's own `VertexDeclaration` is
   kept verbatim and used at draw time. The repository-wide stride table stands in only for the
   internal routes that bind no public buffer, and an unknown stride is refused by name rather
   than approximated. This is why `MultiStreamVertexInput` is true.
5. **Backbuffer readback is cropped in CNA.** `FNA3D_ReadBackbuffer`'s sub-rectangle origin
   differs between drivers (measured — see FNA3D-0b below). CNA's contract is top-left, so the
   renderer reads the whole backbuffer, which every driver agrees is top-row-first, and crops.

## CNA's divergences from XNA on this renderer

| # | Divergence | Why |
|---|---|---|
| 1 | `GraphicsCapability::CustomEffects` is false; `CreateEffectRenderer` returns null | `IEffectRenderer` is handed GLSL/HLSL **source**; FNA3D compiles none. Structural, not deferred. |
| 2 | `GraphicsCapability::Instancing` is false; `DrawInstancedPrimitivesEx` refuses | FNA3D instances fine, but the stock effects declare no per-instance vertex input — in real XNA, hardware instancing needs a custom Effect, which is divergence 1 again. Rendering anyway would stack every instance on record 0. |
| 3 | `Texture3D::GetData` is served from an upload mirror on drivers without volume readback | FNA3D's OpenGL driver implements no `GetTextureData3D`. A volume texture in XNA has no GPU write path, so mirroring the uploads is exact, not invented. Measured once per device by a 1×1×1 probe; not kept on drivers that do support the readback. |
| 4 | The 2D sprite submission disables culling and restores the tracked state immediately | Sprite quads have a fixed winding a game's `RasterizerState.CullMode` has no reason to agree with, and XNA's 2D pipeline is not culled in practice. |
| 5 | `SetDataOptions::NoOverwrite` is downgraded to `None` on a driver without it | `FNA3D_SupportsNoOverwrite` answers per driver. XNA's `NoOverwrite` is a promise the *caller* makes, so honouring it as a plain write is correct where the driver has no fast path; claiming the fast path it does not have would not be. |
| 6 | `ITextureRenderer::GetData` returns false for a block-compressed texture on OpenGL/D3D11 | Those FNA3D drivers refuse compressed `GetTextureData2D` and write nothing. The contract says `true` means the whole region was written, so the honest answer is `false` — measured once per device by a 4×4 DXT1 probe rather than assumed from the driver name. |

## Tasks

| ID | Task | Status |
|---|---|---|
| FNA3D-0 | Existence gate: device creation, clear, present, readback, texture round-trip, Effect parsing | **done** — `fna3d-spike/fna3d_device_spike.c` |
| FNA3D-0b | Existence gate: a real textured draw through the stock SpriteEffect, standalone | **done** — `fna3d-spike/fna3d_sprite_spike.c`; found the sub-rectangle readback quirk |
| FNA3D-1 | `cmake/ThirdPartyFNA3D.cmake`: pinned fetch, static build, MojoShader include/define surface | **done** |
| FNA3D-2 | Identity registration: enum, selector, option, dispatch, module directory, validators 41→42 | **done** |
| FNA3D-3 | Window-flag hook in `GraphicsDevice::getRendererWindowFlags()` | **done** |
| FNA3D-4 | Device creation, teardown, clear family, present, presentation modes, readback | **done** — `Fna3d_Smoke` |
| FNA3D-5 | Stock-effect loading, parameter writes with FNA's own packing, technique/pass application | **implemented, partially validated** — parameter packing is proven only for the effects FNA3D-8 renders plus FNA3D-26's corpus |
| FNA3D-6 | 2D SpriteBatch through the stock SpriteEffect | **done** — `Fna3d_2D` |
| FNA3D-7 | Textures 2D/3D/Cube, mip levels, sub-rectangle upload and readback | **implemented, partially validated** — cube faces and mip levels are not pixel-verified |
| FNA3D-8 | 3D draw routes and the stock-effect variant mapping | **implemented, partially validated** — `Fna3d_3D` renders BasicEffect + AlphaTestEffect only; DualTexture/EnvironmentMap/Skinned, lighting variants and fog are covered only by FNA3D-26's corpus, with two open rows |
| FNA3D-9 | Vertex and index buffers, `SetDataOptions`, per-stream declarations | **implemented, partially validated** — no runtime test splits attributes across two real streams, and no `SetDataOptions` behaviour test exists |
| FNA3D-10 | Render targets 2D and cube: MSAA resolve, mips, depth/stencil, MRT, `PreserveContents` | **done** — `Fna3d_RenderTarget` (2D, MSAA, depth/stencil, PreserveContents) plus `Fna3d_RenderTarget_Advanced` (FNA3D-30/31/32: cube faces, MRT, mips) |
| FNA3D-11 | Occlusion queries | **done** |
| FNA3D-12 | Blend / depth-stencil / rasterizer / sampler state, write masks, scissor, viewport | **implemented, partially validated** — `Fna3d_State` covers blend/depth/stencil/scissor/viewport/write masks; sampler filter and address modes are not systematically covered |
| FNA3D-13 | Truthful capability reporting and deterministic rejections | **done** |
| FNA3D-14 | Capability + rejection example test | **done** — `Fna3d_Capabilities` |
| FNA3D-15 | Unit tests: stock-effect `ShaderIndex` arithmetic | **done** |
| FNA3D-16 | Unit tests: presentation layout and its inverse transform | **done** |
| FNA3D-17 | Unit tests: enum bridge rejection and the stride table | **done** |
| FNA3D-18 | Documentation: renderer doc, registry, physical-module inventory, third-party notices | **done** |
| FNA3D-19 | Format-correct transfer sizing (`Fna3dSurfaceFormats`) for block-compressed formats | **done** — `Fna3d_Compressed`, `Fna3dSurfaceFormatTests` |
| FNA3D-20 | Driver limit queries: `FNA3D_SupportsDXT1/S3TC/BC7/SRGBRenderTargets`, `FNA3D_GetMaxTextureSlots` | **done** |
| FNA3D-21 | Compressed-readback probe; `GetData` refuses instead of reporting an untouched buffer | **done** |
| FNA3D-22 | `FNA3D_SupportsNoOverwrite` gate: downgrade `NoOverwrite` to `None` where the driver has none | **done** |
| FNA3D-23 | `FNA3D_SetTextureName` on textures and render targets, for capture tools | **done** |
| FNA3D-24 | `FNA3D_LinkedVersion()` vs `FNA3D_COMPILED_VERSION` check at device creation | **done** |
| FNA3D-25 | Sampler-slot ceiling: refuse a bind past the driver's real fragment texture-slot count | **done** |

## Validation / conformance phase (FNA3D-26+)

Added after an external audit found that several tasks above were marked **done** on the strength
of code existing rather than a test proving it — most sharply FNA3D-10, which claimed cube, mips
and MRT while the test it cited renders none of the three. The statuses above are corrected; this
phase is what has to close them. Nothing here is a new feature: it is the missing proof for
features that are already written.

| ID | Task | Status |
|---|---|---|
| FNA3D-26 | XNA oracle conformance: build `cna_oracle_render_fna3d` and run the 39-scene corpus against the real XNA 4.0 reference images | **done** — 10/39 exact, 0 failed to render, at the EasyGL baseline; `docs/fna3d-parity-report.md` |
| FNA3D-27a | Diagnose the two corpus rows where FNA3D is worse than EasyGL | **done** — real defect: `SetMatrix4x3Array` dropped the translation row of every bone matrix. All six skinned scenes now at or better than the EasyGL baseline; `Fna3dMatrixPackingTests` pins it |
| FNA3D-27 | Runtime pixel coverage for the three stock effects `Fna3d_3D` never renders: DualTexture, EnvironmentMap, Skinned — plus the lighting variants and fog | **open** (corpus covers them; a renderer-local oracle test does not yet) |
| FNA3D-28 | Real multi-stream draw: split one `VertexDeclaration`'s attributes across two genuine vertex buffers and verify pixels. `MultiStreamVertexInput` is reported true on a non-null capability check alone today | **open** |
| FNA3D-29 | Buffer update semantics: `Discard` / `NoOverwrite` / `None`, partial updates, non-zero offsets, reallocation-then-write, consecutive updates | **open** |
| FNA3D-30 | `RenderTargetCube`: render each of the six faces, read back, and sample | **done** — `Fna3d_RenderTarget_Advanced`; six faces hold six distinct colours, geometry renders into a bound face and does not leak into an unbound one |
| FNA3D-31 | MRT: bind two or more real targets, draw into all of them, verify each one's contents independently | **done** — `Fna3d_RenderTarget_Advanced`; slot 1 genuinely receives the write and each target keeps its own storage |
| FNA3D-32 | Render-target mipmaps: level count, base level after requesting mips, storage above level 0 | **done** — `Fna3d_RenderTarget_Advanced` |
| FNA3D-33 | Sampler conformance: Point/Linear/Anisotropic, Wrap/Clamp/Mirror per axis, mip filtering, `MaxMipLevel`, LOD bias, `MaxAnisotropy` | **open** |
| FNA3D-34 | Driver matrix: the same core conformance suite on OpenGL, Direct3D 11 and SDL_GPU | **open — external gate** (no Vulkan ICD, no Windows on this host) |
| FNA3D-35 | Negative / lifetime suite: cross-device resources, use-after-destroy, invalid regions and levels, teardown with outstanding resources, resize/recreate | **open** |

The renderer's status stays **implemented**, not **validated**, until FNA3D-27..33 and FNA3D-35 are
closed. FNA3D-34 is an external gate and cannot be closed on this host.

## Deferred / external gates

| Item | State |
|---|---|
| SDL_GPU driver | Not exercised in CNA's containers (no Vulkan ICD, so FNA3D declines it and falls through to OpenGL). The renderer code is driver-agnostic; validating it needs a machine with a working Vulkan/Metal/D3D12 stack. |
| Direct3D 11 driver | Windows or DXVK-native only. External gate. The renderer code is driver-agnostic in the sense that it makes the same FNA3D calls; it is **not** established that the observable results match, and this lane has already found three driver-dependent behaviours (sub-rectangle readback origin, volume readback, compressed readback), so an OpenGL pass must not be read as validating the other drivers. |
| macOS / iOS | Not exercised. External gate. |
| Custom `ShaderEffect` | Blocked upstream by design (divergence 1). Would need FNA3D to gain a shader-source entry point, or CNA to ship a D3D9 effect compiler. |
| Instanced drawing | Blocked by divergence 2, which is divergence 1 again. |
| `DebugSimulateContextLoss` / `DebugRestoreContext` | Not implemented; FNA3D exposes no device-loss surface. |
| Block-compressed readback | Implemented and measured, but FNA3D's OpenGL and Direct3D 11 drivers both refuse compressed `GetTextureData2D` outright. On those drivers `GetData` reports that it read nothing; on SDL_GPU it reads the blocks back. External gate: the "reads back" arm is unexercised here because no container has a working SDL_GPU stack. |
| Public compressed `Texture2D` | Blocked one level above this renderer: the shared `Texture::ValidateFormat` still admits only `SurfaceFormat::Color` for every renderer but Skia, so no public XNA texture under FNA3D is compressed. The renderer contract itself has no such restriction and is sized correctly for every format (FNA3D-19). Opening that gate is a shared-graphics decision, not an FNA3D one. |
| `FNA3D_SetTextureDataYUV` | Unreachable: `IGraphicsRenderer` has no YUV texture route, and `VideoDecoder` converts YUV→RGBA in the media module before any renderer sees it. Using it would need a shared-contract change. |
| `FNA3D_GetVertexBufferData` / `FNA3D_GetIndexBufferData` | Unreachable: `IVertexBufferRenderer`/`IIndexBufferRenderer` expose no readback method, and XNA's `GetData` on those buffers is served from the shared layer's own CPU shadow. Would need a shared-contract change. |
| `FNA3D_CloneEffect` | Unreachable: CNA has one instance of each stock effect per device and no public `Effect` clone route. |
| `FNA3D_VerifyVertexSampler` | Unreachable: the stock effects declare no vertex-shader sampler, and `IGraphicsRenderer` exposes no vertex-texture binding. The driver's vertex slot count is nevertheless reported (`GetMaxVertexTextureSlotsEXT`). |
