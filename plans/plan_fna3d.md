# plan_fna3d.md — the FNA3D graphics renderer

Ledger for CNA's **43rd of 46** public renderer identities, `FNA3D`. Companion documents:
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
   `getRendererWindowRequirements()` gained one `#ifdef CNA_RENDERER_FNA3D` block calling
   `Fna3d::Detail::PrepareWindowNeedsOpenGl()` — the same runtime-decides-the-flag shape LLGL, Diligent
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
| FNA3D-2 | Identity registration: enum, selector, option, dispatch, module directory, registry/selector validators | **done** — FNA3D is identity 43 in the current 46-identity registry |
| FNA3D-3 | Window-flag hook in `GraphicsDevice::getRendererWindowRequirements()` | **done** |
| FNA3D-4 | Device creation, teardown, clear family, present, presentation modes, readback | **done** — `Fna3d_Smoke` |
| FNA3D-5 | Stock-effect loading, parameter writes with FNA's own packing, technique/pass application | **done** — all five stock effects render through `Fna3d_XNA_Oracle`; the `float4x3` bone packing is pinned by `Fna3dMatrixPackingTests` after FNA3D-27a found it wrong |
| FNA3D-6 | 2D SpriteBatch through the stock SpriteEffect | **done** — `Fna3d_2D` |
| FNA3D-7 | Textures 2D/3D/Cube, mip levels, sub-rectangle upload and readback | **done** — `Fna3d_Smoke`, `Fna3d_Compressed`, and `Fna3d_RenderTarget_Advanced` for cube faces and mip storage |
| FNA3D-8 | 3D draw routes and the stock-effect variant mapping | **done** — `Fna3d_3D` (BasicEffect, AlphaTestEffect) plus `Fna3d_XNA_Oracle` (DualTexture, EnvironmentMap, Skinned, the lighting variants, fog, all eight alpha-test functions) |
| FNA3D-9 | Vertex and index buffers, `SetDataOptions`, per-stream declarations | **done** — `Fna3d_Buffers` (FNA3D-28/29) |
| FNA3D-10 | Render targets 2D and cube: MSAA resolve, mips, depth/stencil, MRT, `PreserveContents` | **done** — `Fna3d_RenderTarget` (2D, MSAA, depth/stencil, PreserveContents) plus `Fna3d_RenderTarget_Advanced` (FNA3D-30/31/32: cube faces, MRT, mips) |
| FNA3D-11 | Occlusion queries | **done** |
| FNA3D-12 | Blend / depth-stencil / rasterizer / sampler state, write masks, scissor, viewport | **done** — `Fna3d_State` plus `Fna3d_Sampler` (FNA3D-33) |
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
| FNA3D-27 | Runtime pixel coverage for the three stock effects `Fna3d_3D` never renders: DualTexture, EnvironmentMap, Skinned — plus the lighting variants and fog | **done** — `Fna3d_XNA_Oracle` registers the 39-scene corpus as a permanent CTest, which is the only coverage those three families have |
| FNA3D-28 | Real multi-stream draw: split one `VertexDeclaration`'s attributes across two genuine vertex buffers and verify pixels | **done** — `Fna3d_Buffers`; POSITION in stream 0 and COLOR in stream 1 render correctly, in both binding orders |
| FNA3D-29 | Buffer update semantics: `Discard` / `NoOverwrite` / `None`, source-window selection, consecutive updates, both index widths | **done** — `Fna3d_Buffers`. Note: CNA's buffer surface has **no destination-offset `SetData` overload** at all (`VertexBuffer`'s own header states the destination write always begins at the buffer's start), so a mid-buffer patch is not a route this renderer can be tested against — `startIndex` selects the source window and that is what is pinned |
| FNA3D-30 | `RenderTargetCube`: render each of the six faces, read back, and sample | **done** — `Fna3d_RenderTarget_Advanced`; six faces hold six distinct colours, geometry renders into a bound face and does not leak into an unbound one |
| FNA3D-31 | MRT: bind two or more real targets, draw into all of them, verify each one's contents independently | **done** — `Fna3d_RenderTarget_Advanced`; slot 1 genuinely receives the write and each target keeps its own storage |
| FNA3D-32 | Render-target mipmaps: level count, base level after requesting mips, storage above level 0 | **done** — `Fna3d_RenderTarget_Advanced` |
| FNA3D-33 | Sampler conformance: Point/Linear/Anisotropic, Wrap/Clamp/Mirror per axis, `MaxMipLevel`, LOD bias, `MaxAnisotropy` | **done** — `Fna3d_Sampler`; Point and Linear are distinguishable, all three address modes place the right texel past u=1, and the two axes are independent |
| FNA3D-34 | Driver matrix: the same core conformance suite on OpenGL, Direct3D 11 and SDL_GPU | **open — external gate** (no Vulkan ICD, no Windows on this host) |
| FNA3D-35 | Negative suite: invalid regions and levels, disposed resources, draw-range validation, documented refusals, and resource-before-device teardown | **done** — `Fna3d_Lifetime`; post-device resource lifetime is covered separately by FNA3D-39 |

FNA3D-26..33 and FNA3D-35 are closed. **FNA3D-34 remains open and is an external gate**: only
FNA3D's OpenGL driver is exercised on this host, so the renderer is validated *on that driver*, not
across the driver matrix. Do not describe it as validated on SDL_GPU or Direct3D 11.

### Findings this phase produced

| Finding | Where | Status |
|---|---|---|
| `SetMatrix4x3Array` dropped the translation row of every bone matrix, silently turning translation bones into identity bones | found by the XNA oracle corpus | **fixed** (FNA3D-27a), pinned by `Fna3dMatrixPackingTests` |
| `Texture2D` transfers had no `isDisposed_` guard, so a valid upload/readback after `Dispose()` could bypass the disposed-resource contract | shared graphics layer, affects every renderer | **fixed** (FNA3D-45) — transfer entry points raise `ObjectDisposedException`, pinned by `Texture2DTest.TransfersAfterDisposeThrowObjectDisposedException` |
| CNA's buffer surface has no destination-offset `SetData` overload, so a mid-buffer partial update is not a route any renderer can be tested against | shared graphics layer | **reported** — `startIndex` selects the source window and that is what `Fna3d_Buffers` pins |

## Deferred / external gates

| Item | State |
|---|---|
| SDL_GPU driver | Not exercised in CNA's containers (no Vulkan ICD, so FNA3D declines it and falls through to OpenGL). The renderer code is driver-agnostic; validating it needs a machine with a working Vulkan/Metal/D3D12 stack. |
| Direct3D 11 driver | Windows or DXVK-native only. External gate. The renderer code is driver-agnostic in the sense that it makes the same FNA3D calls; it is **not** established that the observable results match, and this lane has already found three driver-dependent behaviours (sub-rectangle readback origin, volume readback, compressed readback), so an OpenGL pass must not be read as validating the other drivers. |
| macOS / iOS | Not exercised. External gate. |
| Multiple simultaneous FNA3D devices on one thread | Not claimed. The OpenGL driver makes a context current at device creation but does not switch to the owning context around commands or teardown; a native two-live-device probe corrupts the second teardown. FNA3D-51 must either establish a one-device contract or add an upstream context-routing solution. |
| Custom `ShaderEffect` | Blocked upstream by design (divergence 1). Would need FNA3D to gain a shader-source entry point, or CNA to ship a D3D9 effect compiler. |
| Instanced drawing | Blocked by divergence 2, which is divergence 1 again. |
| `DebugSimulateContextLoss` / `DebugRestoreContext` | Not implemented; FNA3D exposes no device-loss surface. |
| Block-compressed readback | Implemented and measured, but FNA3D's OpenGL and Direct3D 11 drivers both refuse compressed `GetTextureData2D` outright. On those drivers `GetData` reports that it read nothing; on SDL_GPU it reads the blocks back. External gate: the "reads back" arm is unexercised here because no container has a working SDL_GPU stack. |
| Public compressed `Texture2D` | Blocked one level above this renderer: the shared `Texture::ValidateFormat` still admits only `SurfaceFormat::Color` for every renderer but Skia, so no public XNA texture under FNA3D is compressed. The renderer contract itself has no such restriction and is sized correctly for every format (FNA3D-19). Opening that gate is a shared-graphics decision, not an FNA3D one. |
| `FNA3D_SetTextureDataYUV` | Unreachable: `IGraphicsRenderer` has no YUV texture route, and `VideoDecoder` converts YUV→RGBA in the media module before any renderer sees it. Using it would need a shared-contract change. |
| `FNA3D_GetVertexBufferData` / `FNA3D_GetIndexBufferData` | Unreachable: `IVertexBufferRenderer`/`IIndexBufferRenderer` expose no readback method, and XNA's `GetData` on those buffers is served from the shared layer's own CPU shadow. Would need a shared-contract change. |
| `FNA3D_CloneEffect` | Unreachable: CNA has one instance of each stock effect per device and no public `Effect` clone route. |
| `FNA3D_VerifyVertexSampler` | Unreachable: the stock effects declare no vertex-shader sampler, and `IGraphicsRenderer` exposes no vertex-texture binding. The driver's vertex slot count is nevertheless reported (`GetMaxVertexTextureSlotsEXT`). |

## Reconciliation remediation (FNA3D-36+)

The earlier, now-archival plan was useful as a design checklist, but it predates this
implementation and must not replace the evidence above. Its remaining applicable rules are
carried forward here: validate every native boundary before calling FNA3D, make post-device
destruction harmless, and keep normal draw submission allocation-free. The completed work below
is based on source inspection, not only on the older document's intended design.

| ID | Task | Status |
|---|---|---|
| FNA3D-36 | Remove the per-draw `std::vector` allocations in vertex-binding assembly. Use CNA's 16-stream ceiling and fixed-capacity declaration storage, while retaining caller declarations verbatim and rejecting oversized stream/declaration input. | **done** — stack `std::array` binding/declaration storage; canonical layouts are static `std::span` views |
| FNA3D-37 | Validate 2D, 3D, and cube transfer regions and byte counts before every native FNA3D call; make format-size arithmetic overflow-safe; reject short `ImageData` uploads before FNA3D can read past caller memory. | **done** — checked subtraction-form region bounds, `int64_t` byte arithmetic, format-aware cube/volume transfers, short-image rejection, and `Fna3dSurfaceFormatTests` coverage |
| FNA3D-38 | Make the Texture3D upload mirror exact for partial uploads: track defined voxels and refuse reads that include data never supplied by the caller. | **done** — per-mip voxel coverage; `Fna3d_Lifetime` covers rejection of an undefined read without modifying the destination, while a defined voxel round-trips |
| FNA3D-39 | Replace raw resource-to-device ownership with a shared device-liveness control block, so destruction or use of an FNA3D resource after its device is gone cannot enter freed native state. Cover resources, queries, render targets and SpriteBatch together. | **done** — one device token is invalidated before native teardown; `Fna3d_Device_Lifetime` keeps every wrapper alive across renderer destruction and then destroys them safely |
| FNA3D-40 | Reject native resources carrying a different FNA3D device token at every binding/draw boundary. | **done** — render targets, sampled textures, environment maps, vertex buffers and index buffers compare their token; `Fna3d_Device_Lifetime` covers principal target/sprite/buffer routes with a wrapper surviving into a sequential control device |
| FNA3D-41 | Make vertex/index buffer allocation and upload byte arithmetic safe against signed overflow before calling FNA3D. | **done** — a checked signed-32-bit byte-count helper guards construction, growth and upload; overflow cases are pinned by `Fna3d_Device_Lifetime` |
| FNA3D-42 | Split SpriteBatch submissions before the 16-bit index space rolls over. | **done** — each native draw is capped at 16,384 quads; `Fna3d_2D` renders a visible sprite immediately beyond that boundary |
| FNA3D-43 | Make native construction transactional: destroy the device/effects if renderer setup throws, roll back partially created render targets, and dispose a native texture if wrapper allocation fails. | **done** — renderer, texture factory, render-target, and Texture3D mirror construction paths now have rollback |
| FNA3D-44 | Enforce 4×4 block alignment (with legal NPOT edge tails) for compressed 2D transfer regions, not only ordinary bounds. | **done** — shared format-region predicate plus `Fna3dSurfaceFormatTests` boundary coverage |
| FNA3D-45 | Restore the shared disposed-resource contract for `Texture2D` uploads and readbacks. | **done** — valid transfer entry points now throw `ObjectDisposedException`; generic graphics test coverage applies to every renderer |
| FNA3D-46 | Clear sampler slot 1 when `EnvironmentMapEffect` has no environment map, and reject an environment map from another FNA3D device. | **done** — every apply verifies slot 1 with either the current cube or null; `Fna3d_Device_Lifetime` exercises a live→null transition and foreign-device rejection |
| FNA3D-47 | Re-run ASan/UBSan over the enlarged 13-test FNA3D suite, including the post-device and >16-bit SpriteBatch paths added by this audit. | **done** — 13/13 pass with leak detection disabled for the external graphics stack; UBSan still reports the pre-existing MojoShader decimal-parser signed overflow, but no CNA-originating sanitizer defect was found |
| FNA3D-48 | Make the documented OpenGL test-driver pin an actual CTest property for the entire FNA3D suite. | **done** — every registered `Fna3d_*` test receives `FNA3D_FORCE_DRIVER=OpenGL`; other drivers remain opt-in direct runs for FNA3D-34 |
| FNA3D-49 | Retain textures queued by deferred `SpriteBatch` until the renderer has completed `End`, instead of leaving raw wrapper/renderer pointers that may dangle between `Draw` and submission. | **done** — the shared SpriteBatch queue owns each `ITextureRenderer` through renderer `End`; `SpriteBatchSortModeTest.DeferredRetainsTextureRendererThroughEnd` pins the destruction order for every backend |
| FNA3D-50 | Add deterministic native-allocation failure injection for device/effect/texture/render-target/buffer/query construction and buffer-growth rollback paths. | **open — test seam required** |
| FNA3D-51 | Decide and enforce the device/thread/context contract: either document/reject a second live FNA3D device and cross-thread use, or contribute context switching to the upstream OpenGL driver and test concurrent creation/commands/teardown. | **open — upstream/contract decision** |

After this reconciliation, the remaining work is explicit: FNA3D-34 needs external driver/OS
infrastructure, and FNA3D-50 needs a native failure-injection seam. FNA3D-51 needs an explicit
one-device policy or an upstream OpenGL context solution. None is evidence that can be
substituted by an ordinary OpenGL debug build.

### Reconciliation findings

| Finding | Resolution |
|---|---|
| `ApplyVertexBindingsEXT` and the canonical-layout helper allocated nested `std::vector`s once per draw. | **fixed** in FNA3D-36 with bounded stack arrays and static `std::span` layout views. |
| Texture-region checks either reached FNA3D unchecked or used signed addition/multiplication that could overflow. Cube and volume transfers also assumed four bytes per texel. | **fixed** in FNA3D-37: format-aware, overflow-safe byte counts and checked 2D/3D mip regions protect every exposed transfer. |
| OpenGL's missing `GetTextureData3D` fallback mirrored only bytes, so a partial upload made never-written voxels look like valid zeroes. | **fixed** in FNA3D-38: coverage is tracked separately and an incomplete read returns `false`; the shared layer leaves its caller buffer untouched and raises its documented unsupported-read error. |
| Every resource wrapper retained a raw `FNA3D_Device*`; destructing one after its renderer was undefined behaviour, and type checks accepted resources from another FNA3D device. | **fixed** in FNA3D-39/40 with a shared liveness/identity token and the sequential replacement-device `Fna3d_Device_Lifetime` regression. |
| Buffer capacity/upload multiplication could overflow `int`, and SpriteBatch's 16-bit indices wrapped after 16,384 quads. | **fixed** in FNA3D-41/42 with checked byte arithmetic and bounded draw chunks. |
| Render-target/effect/texture construction could leak already-created native objects when a later allocation threw or returned null. | **fixed** in FNA3D-43; deterministic native-failure injection remains FNA3D-50. |
| A null environment map left the previous slot-1 cube bound, so effect state could leak between draws. | **fixed** in FNA3D-46 by explicitly verifying a null sampler binding. |
| The FNA3D test CMake claimed every test pinned OpenGL, but only the oracle script actually set the hint. | **fixed** in FNA3D-48 by assigning the environment property to all tests in the module directory. |
| Deferred `SpriteBatch` queued raw `Texture2D*` values and released the queue before the backend's `End`; destroying a texture after `Draw` could leave FNA3D's final texture group dangling. | **fixed** in FNA3D-49 by retaining renderer ownership until backend submission completes. |

## Status

The FNA3D renderer is feature-complete for the currently supported single-device OpenGL route.
Its 13-test native suite, XNA oracle corpus, focused unit regressions, and ASan/UBSan run pass on
Mesa llvmpipe. The audit's CNA-originating correctness and lifetime defects are fixed.

The remaining FNA3D work is deliberately limited to these explicit tasks:

1. **FNA3D-34 — complete the driver and platform matrix.** Run the same native conformance suite
   on SDL_GPU with a working Vulkan/Metal/D3D12 backend, on Direct3D 11 under Windows, and on the
   relevant macOS/iOS configurations. An OpenGL pass does not establish identical behaviour on
   those drivers.
2. **FNA3D-50 — add deterministic native-allocation failure injection.** Provide a test seam for
   device, effect, texture, render-target, buffer, query, and buffer-growth failures, then prove
   that every partially constructed native object is rolled back without leaks or stale state.
3. **FNA3D-51 — define and enforce the device/thread/context contract.** Either reject a second
   live FNA3D device and cross-thread graphics use with a deterministic diagnostic, or implement
   the required OpenGL context routing upstream and cover concurrent creation, commands, and
   teardown with native tests.

Known structural limitations remain documented rather than disguised as incomplete renderer
work: custom source effects and stock-effect instancing require a shader compilation route FNA3D
does not expose; compressed readback depends on the selected upstream driver; and public
compressed `Texture2D` creation is gated by CNA's shared graphics layer.
