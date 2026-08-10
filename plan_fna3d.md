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

## Tasks

| ID | Task | Status |
|---|---|---|
| FNA3D-0 | Existence gate: device creation, clear, present, readback, texture round-trip, Effect parsing | **done** — `fna3d-spike/fna3d_device_spike.c` |
| FNA3D-0b | Existence gate: a real textured draw through the stock SpriteEffect, standalone | **done** — `fna3d-spike/fna3d_sprite_spike.c`; found the sub-rectangle readback quirk |
| FNA3D-1 | `cmake/ThirdPartyFNA3D.cmake`: pinned fetch, static build, MojoShader include/define surface | **done** |
| FNA3D-2 | Identity registration: enum, selector, option, dispatch, module directory, validators 41→42 | **done** |
| FNA3D-3 | Window-flag hook in `GraphicsDevice::getRendererWindowFlags()` | **done** |
| FNA3D-4 | Device creation, teardown, clear family, present, presentation modes, readback | **done** — `Fna3d_Smoke` |
| FNA3D-5 | Stock-effect loading, parameter writes with FNA's own packing, technique/pass application | **done** |
| FNA3D-6 | 2D SpriteBatch through the stock SpriteEffect | **done** — `Fna3d_2D` |
| FNA3D-7 | Textures 2D/3D/Cube, mip levels, sub-rectangle upload and readback | **done** |
| FNA3D-8 | 3D draw routes and the stock-effect variant mapping | **done** — `Fna3d_3D` |
| FNA3D-9 | Vertex and index buffers, `SetDataOptions`, per-stream declarations | **done** |
| FNA3D-10 | Render targets 2D and cube: MSAA resolve, mips, depth/stencil, MRT, `PreserveContents` | **done** — `Fna3d_RenderTarget` |
| FNA3D-11 | Occlusion queries | **done** |
| FNA3D-12 | Blend / depth-stencil / rasterizer / sampler state, write masks, scissor, viewport | **done** — `Fna3d_State` |
| FNA3D-13 | Truthful capability reporting and deterministic rejections | **done** |
| FNA3D-14 | Capability + rejection example test | **done** — `Fna3d_Capabilities` |
| FNA3D-15 | Unit tests: stock-effect `ShaderIndex` arithmetic | **done** |
| FNA3D-16 | Unit tests: presentation layout and its inverse transform | **done** |
| FNA3D-17 | Unit tests: enum bridge rejection and the stride table | **done** |
| FNA3D-18 | Documentation: renderer doc, registry, physical-module inventory, third-party notices | **done** |

## Deferred / external gates

| Item | State |
|---|---|
| SDL_GPU driver | Not exercised in CNA's containers (no Vulkan ICD, so FNA3D declines it and falls through to OpenGL). The renderer code is driver-agnostic; validating it needs a machine with a working Vulkan/Metal/D3D12 stack. |
| Direct3D 11 driver | Windows or DXVK-native only. External gate. |
| macOS / iOS | Not exercised. External gate. |
| Custom `ShaderEffect` | Blocked upstream by design (divergence 1). Would need FNA3D to gain a shader-source entry point, or CNA to ship a D3D9 effect compiler. |
| Instanced drawing | Blocked by divergence 2, which is divergence 1 again. |
| `DebugSimulateContextLoss` / `DebugRestoreContext` | Not implemented; FNA3D exposes no device-loss surface. |
