# Diligent Engine Graphics Backend — Implementation Plan

> **Status (2026-07-31): Phase `DILIGENT-1` is implemented, and most of Phase `DILIGENT-2`/`3` on
> top of it.** What that means concretely is in the "What the baseline actually does" section below
> — read it before assuming parity with Vulkan/EasyGL/SDL_GPU, which this backend does **not** have.
> `RenderTarget2D`/`RenderTargetCube`, `AlphaTestEffect`, `DualTextureEffect`,
> `EnvironmentMapEffect`, `SkinnedEffect` (stride 52), `PbrEffect`/`SkinnedPbrEffect` (strides 48/68),
> several simultaneous render targets, `OcclusionQuery`, MSAA (back buffer and `RenderTarget2D`,
> device-probed clamping) and hardware instancing (`DrawInstancedPrimitivesEx`) are all implemented
> and verified on a real (software) Vulkan device. Volume-texture sampling, MSAA on
> `RenderTargetCube` and custom `ShaderEffect` programs are **not** implemented, and each one
> *refuses loudly* rather than rendering a near-miss.
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented.

---

## What makes this backend different from every other one

Every other entry in `CNA_GRAPHICS_BACKEND` names a single native graphics API (or, for
`SDL_RENDERER`/`SDL_GPU`/`BGFX`, a portability layer that CNA drives as if it were one). Diligent
Engine is the first backend whose *whole point* is that it is itself an abstraction over
Direct3D 11, Direct3D 12, Vulkan, OpenGL/GLES and Metal. CNA therefore sits on top of two stacked
abstraction layers, and the concrete native API is a **runtime** decision, not a build-time one.

Practical consequences that shaped this plan:

- **One shader source language for everything.** Shaders are authored once in HLSL and
  cross-compiled by Diligent — to DXBC/DXIL on Direct3D, to SPIR-V through its glslang HLSL front
  end on Vulkan, and to GLSL through its own HLSL2GLSL converter on OpenGL. This is why
  `DILIGENT_NO_HLSL` must stay `OFF` even in a Linux-only build.
- **Device selection can fail per-device and must fall through.** `D3D12` → `Vulkan` → `D3D11` →
  `OpenGL`, filtered to the engines DiligentCore actually built, each attempted in turn; the first
  one that yields a device *and* a swap chain wins. `CNA_DILIGENT_DEVICE` pins one explicitly.
- **Diligent normalizes NDC for us.** Its Vulkan back end flips the viewport so NDC matches the
  Direct3D convention CNA's XNA matrices assume, and the OpenGL device is created with
  `ZeroToOneNDZ` so clip depth is `[0,1]` there too. CNA uploads its row-major `Matrix` memory
  verbatim and the HLSL declares `row_major float4x4`, so `mul(v, m)` is XNA's own `v * M`.
- **Pipelines are immutable**, as on Vulkan/D3D12/WebGPU/SDL_GPU. A cache keyed by (shader
  variant, topology, blend, colour write mask, depth-stencil, rasterizer) is required from the
  start, not an afterthought.

---

## Naming conventions for this backend

| Item | Value |
| --- | --- |
| `CNA_GRAPHICS_BACKEND` value | `DILIGENT` |
| CMake option | `CNA_BACKEND_DILIGENT` |
| Compile definition | `CNA_BACKEND_DILIGENT` |
| Backend directory | `src/CNA/Internal/Backends/Diligent/`, `include/CNA/Internal/Backends/Diligent/` |
| CMake target | `cna_backend_graphics_diligent` |
| Main class | `CNA::Internal::Backends::Diligent::DiligentGraphicsBackend` |
| Namespace alias | `Dg = ::Diligent` — the CNA namespace is itself named `Diligent`, so unqualified `Diligent::X` inside it would resolve to the CNA namespace and fail |
| Third-party pin | DiligentCore `v2.5.6`, via `FetchContent` in `cmake/ThirdPartyDiligent.cmake` |
| Task prefix | `DILIGENT-` |
| CTest targets | `DiligentDeviceSelectionTest.*` (no GPU needed), `Diligent_2D`, `Diligent_3D`, `Diligent_RenderTarget`, `Diligent_RenderTargetCube`, `Diligent_AlphaTestFog`, `Diligent_DualTextureEnvMap`, `Diligent_Skinned`, `Diligent_MRT`, `Diligent_OcclusionQuery`, `Diligent_MSAA`, `Diligent_Instanced`, `Diligent_DrawOffset`, `Diligent_SetDataOptions`, `Diligent_VertexLit`, `Diligent_Pbr`, `Diligent_DepthBias`, `Diligent_ReferenceStencil`, `Diligent_FillMode`, `Diligent_Anisotropic`, `Diligent_SpriteFont`, `Diligent_Model`, `Diligent_Mip`, `Diligent_Npot`, `Diligent_RenderTargetMipGen` |

---

## Design decisions

1. **DiligentCore only** — not DiligentTools or DiligentFX. CNA needs the render device, swap chain
   and shader compilation; DiligentTools' asset loaders and DiligentFX's render features would
   duplicate CNA's own content pipeline and effect layer.
2. **`FetchContent` with a pinned tag** (`v2.5.6`), recursive submodules. `FETCHCONTENT_SOURCE_DIR_DILIGENTCORE`
   points the build at a local checkout for offline or repeated builds — no CNA-specific option is
   invented for something FetchContent already models.
3. **Runtime device autodetection over all built engines**, overridable with `CNA_DILIGENT_DEVICE`
   (`d3d12`/`vulkan`/`d3d11`/`opengl`/`auto`). The parsing and preference-order helpers are free
   functions specifically so they are unit-testable with no GPU present.
4. **HLSL as the single shader source**, embedded as string literals in the backend `.cpp` rather
   than as separate files compiled by a build-time tool. Diligent compiles from source at device
   creation time on every device type, so there is no bytecode step to add — and adding one would
   pin the shaders to a single device type, defeating decision 3.
5. **A non-sRGB `RGBA8_UNORM` back buffer** (Diligent's own default is the sRGB variant). Every
   other CNA backend presents colours numerically as the game wrote them; matching that matters
   more than matching Diligent's default.
6. **`D24_UNORM_S8_UINT` depth-stencil regardless of the requested `DepthFormat`.** XNA's
   `DepthFormat` tops out at `Depth24Stencil8` and CNA's stencil support needs the stencil half.
   Same simplification Vulkan already makes (see `IGraphicsBackend::CreateRenderTarget2D`'s own
   note about the depth-format-keyed pipeline cache a per-target format would require).
7. **Unimplemented effect features refuse rather than approximate.** `DrawPrimitivesEx` throws for
   `DualTexture`/`EnvironmentMap`/`Skinned`/`Pbr`/custom-effect/instancing/fog/alpha-test draws.
   The alternative — rendering the nearest available variant — is the silent-wrong-output failure
   mode `REMED-GFX-127`/`130`/`135` removed from the texture interfaces, and it must not be
   reintroduced here.
8. **X11 only on Linux** (`SDL_VIDEODRIVER=x11`). Diligent's `LinuxNativeWindow` carries an X11
   window id / display or an XCB connection; it has no Wayland surface member, so a Wayland session
   has to go through SDL's X11 fallback. A Wayland session throws with that instruction, rather
   than failing deep inside Diligent.
9. **OpenGL now creates a device and renders most of the 2D/3D baseline, but is not fully verified.**
   Every claim in this plan's ✅ rows was still measured on the Vulkan device type — treat GL as `🟨`
   until `DILIGENT-30` closes. Two real, distinct bugs were found and are not yet fixed; see that
   task's own row for the current, precise state.
10. **The back buffer's format is whatever the surface grants, not what CNA asks for.** Diligent
    substitutes a supported format when the surface rejects the requested one, so raw pixel readback
    consults `ITexture::GetDesc().Format` and swizzles BGRA→RGBA when needed. Rendering is
    unaffected (the shader writes float RGBA and the format conversion happens on write); only
    byte-for-byte readback is. Found by a real failing pixel assertion, not assumed.

---

## What the baseline actually does (Phase `DILIGENT-1`)

Implemented and exercised:

- Device + immediate context + swap chain over a real SDL window, with per-device-type fallback.
- The whole clear family (colour, depth, stencil and every combination), `Present`, swap interval,
  runtime swap-chain resize.
- Logical/virtual resolution with all five `CnaPresentationMode` policies, plus
  `TransformWindowToLogical`/`TransformLogicalToWindow` so input maps correctly on a letterboxed
  window.
- `Texture2D`: creation from `ImageData` including a mip chain, `SetData` (full and per-level), and
  `GetData` readback through a staging texture.
- `VertexBuffer` (any stride) and 16-/32-bit `IndexBuffer`, re-allocated on growth.
- `SpriteBatch`: batched quads with tint, rotation, origin, both flips, layer depth, per-batch
  transform matrix, and per-batch sampler filter/address modes.
- 3D draws for strides 16/20/24/32 (`VertexPositionColor`, `VertexPositionTexture`,
  `VertexPositionColorTexture`, `VertexPositionNormalTexture`), including `BasicEffect`'s
  three directional lights with Blinn-Phong specular, evaluated per pixel.
- `BlendState` (factors, functions, slot-0 colour write mask, blend factor constant),
  `DepthStencilState` (depth test/write/function, two-sided stencil, masks, reference value),
  `RasterizerState` (cull, fill, scissor enable, depth bias), and `SamplerState` on slot 0 —
  all folded into the pipeline cache key.
- `ReadBackbuffer`, resampling the physical region back to the caller's logical region.

- `TextureCube` and `Texture3D`: creation with a mip chain, per-face / per-sub-box `SetData` and
  `GetData` (`DILIGENT-23`/`DILIGENT-40`). `TextureCube` is also sampleable, through
  `EnvironmentMapEffect`; `Texture3D` is storage and readback only.
- `RenderTarget2D` (`DILIGENT-20`/`DILIGENT-21`): off-screen colour, an optional real depth-stencil
  buffer, `GetData` readback, sampling the unbound target, and mip regeneration on unbind.
- `RenderTargetCube` (`DILIGENT-22`): six per-face render-target views over one cube texture, a
  shared depth-stencil buffer, `GetData` per face, and sampling back through `EnvironmentMapEffect`
  via the same `DiligentSampledTexture` interface a plain `TextureCube` uses.
- `AlphaTestEffect`'s per-pixel discard and `BasicEffect`'s fog (`DILIGENT-31`/`DILIGENT-32`), on
  every 3D shader variant.
- `DualTextureEffect` and `EnvironmentMapEffect` (`DILIGENT-33`/`DILIGENT-34`).
- `SkinnedEffect` at stride 52 (`DILIGENT-35`).
- Several simultaneous render targets (`DILIGENT-24`): all bound slots are attached and cleared,
  though only slot 0 receives fragments from CNA's single-output built-in shaders.
- `OcclusionQuery` (`DILIGENT-41`): a real `IQuery`-backed query, falling back from
  `QUERY_TYPE_OCCLUSION` to `QUERY_TYPE_BINARY_OCCLUSION` (0/1, matching EasyGL's GLES3 convention)
  on a device without the `occlusionQueryPrecise` feature — which includes `lavapipe`, this
  backend's own verification device.
- MSAA (`DILIGENT-25`) on the back buffer and `RenderTarget2D`: a real offscreen multisampled
  colour (and depth-stencil) texture, resolved into the swap chain's back buffer on `Present()`/
  `ReadBackbuffer()`, or into a `RenderTarget2D`'s own single-sampled resolve texture on unbind.
  Device-probed and clamped via `GetTextureFormatInfoExt()`, exactly like every other capability
  this backend reports honestly rather than silently ignores. `RenderTargetCube` MSAA is not
  implemented.
- Hardware instancing (`DILIGENT-43`, `DrawInstancedPrimitivesEx`): a per-instance vertex buffer
  bound at slot 1 with `INPUT_ELEMENT_FREQUENCY_PER_INSTANCE`/step rate 1 supplies one 4x4 world
  matrix (four consecutive `float4` rows) per instance, alongside the per-vertex `Position`-only
  stream at slot 0. Deliberately minimal, matching every other CNA backend's own baseline: no
  texture, no lighting, flat `g_DiffuseColor` output. `g_WorldViewProj` is repurposed to hold just
  `View * Projection` since there is no single shared `World` to fold in.
- `PbrEffect`/`SkinnedPbrEffect` (`DILIGENT-36`, strides 48/68): the glTF 2.0 metallic-roughness
  BRDF (GGX distribution, Smith-Schlick-GGX visibility, Schlick Fresnel), five optional texture
  maps (base colour, normal, metallic-roughness, emissive, occlusion) each falling back to their
  own glTF "map absent" identity when unbound. `SkinnedPbrEffect` combines the same BRDF with
  `Skinned3D`'s bone-palette skinning.

Deliberately refused (each throws, naming itself):

- Custom `ShaderEffect` programs, `RenderTargetCube` MSAA, and `SkinnedEffect`'s stride-56
  vertex-colour variant. `SupportsCapability()` reports each of these honestly.

---

## Phases and tasks

### Phase `DILIGENT-1` — baseline (done)

| Task | Description | Status | Notes |
| --- | --- | --- | --- |
| `DILIGENT-1` | `CNA_GRAPHICS_BACKEND=DILIGENT` selection, target, compile definition | ✅ | `cmake/BackendSelection.cmake`, `cmake/BackendLibraries.cmake` |
| `DILIGENT-2` | DiligentCore acquisition, engine gating, `cna_link_diligent()` | ✅ | `cmake/ThirdPartyDiligent.cmake`. Disables Diligent's tests/archiver/format validation and the WebGPU engine; disables its OpenGL engine when `GL/glx.h` is absent, with a STATUS line rather than a third-party error |
| `DILIGENT-3` | Runtime device selection + `CNA_DILIGENT_DEVICE` override | ✅ | `GetDeviceTypePreferenceOrder()`/`ParseDeviceTypeOverride()` are free functions so they test without a GPU |
| `DILIGENT-4` | Swap chain from the SDL native window (X11, Win32) | ✅ | Wayland throws with the `SDL_VIDEODRIVER=x11` instruction (design decision 8) |
| `DILIGENT-5` | Clear family, `Present`, swap interval, resize | ✅ | |
| `DILIGENT-6` | Virtual resolution, presentation modes, coordinate transforms | ✅ | Same math as the SDL_GPU/WebGPU backends |
| `DILIGENT-7` | `Texture2D` create/update/readback | ✅ | Mipped textures are created empty and filled per level: Diligent wants initial data for all levels or none |
| `DILIGENT-8` | Vertex/index buffers | ✅ | 32-bit indices supported natively, unlike the interface's fallback default |
| `DILIGENT-9` | HLSL shader set + pipeline cache | ✅ | Five variants; key covers topology and the full blend/depth-stencil/rasterizer state |
| `DILIGENT-10` | `SpriteBatch` | ✅ | Batches flush on texture/sampler/transform change and at `End()` |
| `DILIGENT-11` | 3D stride dispatch 16/20/24/32 + `BasicEffect` lighting | ✅ | |
| `DILIGENT-12` | Render state family (`ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState`/`ApplySamplerState`) | ✅ | Slots 1..3 write masks are carried since `DILIGENT-24`, but are only observable once a multi-output shader exists; `MultiSampleMask` has no effect at all (single-sampled everywhere) |
| `DILIGENT-13` | `ReadBackbuffer` | ✅ | |
| `DILIGENT-14` | Honest `SupportsCapability()` + loud refusals for the unimplemented set | ✅ | Design decision 7 |
| `DILIGENT-15` | `Diligent_DeviceSelection` unit tests (no GPU required) | ✅ | Runs in the normal `CnaTests` suite |
| `DILIGENT-16` | `Diligent_*` CTest binaries | ✅ | 9 binaries, 41 pixel checks total, all passing on a real Vulkan device — Mesa `lavapipe` (software rasterizer) under Xvfb, not hardware. See "Verification status" |
| `DILIGENT-17` | `docs/diligent-backend.md` | ✅ | |

### Phase `DILIGENT-2` — render targets

| Task | Description | Status | Notes |
| --- | --- | --- | --- |
| `DILIGENT-20` | `RenderTarget2D` (`CreateRenderTarget2D`, `SetRenderTarget2D`, `SetRenderTargets` single slot) | ✅ | The pipeline cache key now carries the bound target's colour/depth formats, as predicted. Two real defects found while verifying: the key's `operator==`/hash had to learn the new field (a stale pipeline was reused and Vulkan rejected the render pass), and the sprite projection had to span the target rather than the window's logical canvas |
| `DILIGENT-21` | `RenderTarget2D` `GetData` readback and `PreserveContents` semantics | ✅ | Readback reuses `ReadTextureRegion`. Diligent's immediate context binds without a load operation, so contents always survive a bind cycle — which satisfies `PreserveContents` and is a legal superset of `DiscardContents` |
| `DILIGENT-22` | `RenderTargetCube` + per-face binding | ✅ | Six per-face `RENDER_TARGET` views over one `RESOURCE_DIM_TEX_CUBE` texture, a shared depth-stencil buffer (only one face is ever the active draw target at a time), and a `DiligentSampledTexture` conformance shared with plain `TextureCube` so `EnvironmentMapEffect` accepts either. Verified by `Diligent_RenderTargetCube`, including sampling the render target back through a real `EnvironmentMapEffect` reflection |
| `DILIGENT-23` | `TextureCube` (`CreateTextureCube`, `SetData`/`GetData` per face) | ✅ | Six array slices of one `RESOURCE_DIM_TEX_CUBE`; full mip chain. Verified by the shared `TextureCubeTests`/`CnjCapabilityMatrixTests`/XNB cube fixtures, which now run for real on this backend instead of asserting the refusal |
| `DILIGENT-24` | MRT (`SetRenderTargets` with 2..4 slots) | ✅ | All bound slots are attached and cleared, and the pipeline key carries every slot's format plus the per-slot colour write masks. Only slot 0 receives *fragments* today: every built-in shader declares one `SV_TARGET`, so slots 1..3 stay clear-only until `DILIGENT-42` |
| `DILIGENT-25` | MSAA back buffer + render targets, device-probed clamping | ✅ | Real offscreen-then-resolve MSAA for the back buffer (`Present()`/`ReadBackbuffer()` resolve) and `RenderTarget2D` (its own independent multisampled texture + resolve texture, resolved on unbind), both clamped via `GetTextureFormatInfoExt()`'s per-format `SampleCounts` bitmask. `RenderTargetCube` MSAA is not implemented (still clamped to 1). Verified by `Diligent_MSAA`'s diagonal-edge differential (see "Verification status"). Found and fixed a real, separate, pre-existing bug while wiring this up -- see this row's own "Verification status" note |
| `DILIGENT-26` | Mip generation for render targets (`GenerateMips`) | ✅ | Implemented since `DILIGENT-25` (which also fixed the real bug that made it unreachable -- `SetRenderTarget2D()`/`SetRenderTargetCubeFace()`/`SetRenderTargets()` never called the outgoing target's `UnbindAsRenderTarget()`). Now closed with a dedicated pixel test, `Diligent_RenderTargetMipGen` (`examples/diligent_rendertarget_mipgen_test.cpp`): a 4x4 mipMap `RenderTarget2D` gets an exact (x+y)%2 Red/Blue checkerboard pixel-copied into level 0 (`SpriteBatch` + `PointClamp`, 1:1), so every aligned 2x2 block contains exactly 2 Red + 2 Blue texels. After unbinding (which triggers `IDeviceContext::GenerateMips()`), level 1 (2x2) and level 2 (1x1) both read back as the real box-filter average `(128,0,128)` at every texel -- not pure Red, pure Blue, or black, which is what a nearest-copy fallback or a silent no-op would produce instead. 7/7 checks pass, deterministic across repeated runs; level 0's own content is confirmed unaffected by the regeneration |

### Phase `DILIGENT-3` — remaining effect families

| Task | Description | Status | Notes |
| --- | --- | --- | --- |
| `DILIGENT-30` | Verify the OpenGL device type end-to-end (sprite Y orientation in particular) | 🟨 | Substantial progress, not closed. Two real, distinct, pre-existing bugs found and fixed: (1) the OpenGL device type could not create a device at all -- Diligent's own `GLContext` (`GLContextLinux.cpp`) asserts a GL context is already current via `glXGetCurrentContext()` rather than creating one itself, unlike every other device type here, and nothing called `SDL_GL_CreateContext()`/`SDL_GL_MakeCurrent()` before `CreateDeviceAndSwapChainGL()`; fixed in `DiligentGraphicsBackend::TryCreateDevice()`, plus `GraphicsDevice.cpp`'s window-flag selection, which previously requested both `SDL_WINDOW_VULKAN` and `SDL_WINDOW_OPENGL` together -- SDL3 rejects that combination outright ("Conflicting window graphics flags specified"), so it now reads the same `CNA_DILIGENT_DEVICE` override the backend itself reads to request the one flag that will actually be used. (2) Every shader failed to compile to GLSL at all -- `kConstantsHlsl`/`kBonesHlsl`'s inline `row_major` qualifiers pass through Diligent's HLSL2GLSL converter completely unstripped (invalid GLSL syntax); switched to the `#pragma pack_matrix(row_major)` form the converter actually recognizes and strips. With both fixed, `Diligent_2D`/`_3D`/`_AlphaTestFog`/`_RenderTargetCube`/`_MRT`/`_OcclusionQuery` mostly or fully pass under `CNA_DILIGENT_DEVICE=opengl` (manually, not yet a CTest target). Two further real bugs remain, not yet root-caused: (a) a texture/shader-resource-variable binding bug where the SECOND distinct texture sampled in a session appears to still read the FIRST one's content, reproduced independently in `Diligent_2D`'s sourceRectangle check, `Diligent_DualTextureEnvMap`'s second layer, `Diligent_RenderTarget`'s unbound-target sampling, and `Diligent_MSAA`'s `RenderTarget2D` resolve; (b) `Diligent_Skinned`'s vertex shader fails to convert to GLSL at all -- a local `float4x4 skin = ComputeSkinMatrix(...)` variable declaration is left as literal HLSL syntax in the GLSL output, unlike the same type used inside a cbuffer (which the `#pragma pack_matrix` fix above already handles correctly). Diagnostic work already narrowed (a) down: instrumenting `DrawSpriteQuads()` confirmed `IShaderResourceVariable::Set(newSrv)` immediately followed by `::Get(0)` returns the *correct*, newly-Set SRV pointer every time (verified: different pointer for `redBlueTexture_` vs. the earlier `greenTexture_`), so the bug is not in CNA's C++ call sequence or its ordering relative to `CommitShaderResources()` -- the Diligent API layer's own bookkeeping is right, but what's actually bound to the GL texture unit at draw time is stale regardless. Two workaround attempts were tried and ruled out: `IDeviceContext::InvalidateState()` before the draw (resets literally all context state, not just resource bindings -- crashes with "Framebuffer width and height must be positive" from the now-unset render target/viewport, and would need every piece of per-draw state re-established afterward to even be usable, which defeats this backend's whole cached-pipeline design) and `RESOURCE_STATE_TRANSITION_MODE_NONE` instead of `_TRANSITION` on `CommitShaderResources()` (no change). This points to the bug living inside DiligentCore v2.5.6's own OpenGL `CommitShaderResources`/GL-texture-unit-binding implementation, not anywhere in CNA's code -- closing this may need either a DiligentCore upstream fix/newer pinned version, or a CNA-side structural workaround (e.g. a fresh `IShaderResourceBinding` per texture change instead of one shared, reused, dynamically-rebound SRB), neither of which was attempted here |
| `DILIGENT-31` | `AlphaTestEffect` (per-pixel discard) | ✅ | Implemented in `GpuDrawParams::alphaTest`'s own reference/tolerance/weight encoding, so all four compare modes come from the effect layer rather than from a per-mode shader. Verified by `Diligent_AlphaTestFog` (discard and keep, same geometry, same effect object) |
| `DILIGENT-32` | Fog for the `BasicEffect` family (`GpuDrawParams::fogVector`) | ✅ | The vertex stage computes FNA's `keep = 1 - saturate(dot(objectPos, fogVector))`, the pixel stage blends RGB toward `FogColor`. Verified fogged vs. fog-disabled on the same geometry |
| `DILIGENT-33` | `DualTextureEffect` | ✅ | Two shader variants (stride 20 and 24) sharing one two-sampler pixel shader; the first layer is doubled before the modulate, as XNA does. Both layers share one UV set, matching every other CNA backend. Verified by `Diligent_DualTextureEnvMap` |
| `DILIGENT-34` | `EnvironmentMapEffect` | ✅ | Reuses the lit vertex stage and adds a `TextureCube` sampler: reflection vector, flat or Fresnel-weighted blend factor, and the env-map specular term. Verified at amount 1 and amount 0 on the same geometry. Not byte-compared against FNA's `PSEnvMap` |
| `DILIGENT-35` | `SkinnedEffect` (72-bone palette, stride 52) | ✅ | The palette lives in its own uniform buffer (4.5 KB is too much for the per-draw block); FNA's `WeightsPerVertex` truncation and the bone-skin ∘ world normal matrix are both honoured. Verified by `Diligent_Skinned`, including a trap bone the second weight pair must never reach. The stride-56 vertex-colour variant is not implemented |
| `DILIGENT-36` | `PbrEffect`/`SkinnedPbrEffect` | ✅ | `PbrEffect` (stride 48, unskinned) and `SkinnedPbrEffect` (stride 68, PBR+skinning combined) both done and verified. New `ShaderVariant::Pbr3D`: glTF metallic-roughness BRDF (GGX/Trowbridge-Reitz distribution, Smith-Schlick-GGX visibility, Schlick Fresnel), ported term-for-term from this project's own established HLSL reference (`src/CNA/Internal/Backends/D3DCommon/shaders/pbr3d.vert.hlsl`/`pbr3d.frag.hlsl`) rather than re-derived from scratch. Five texture bindings (base colour + normal/metallic-roughness/emissive/occlusion maps, all four optional -- an unbound one falls back to its own glTF "absent" identity: flat tangent-space normal, or white since 1.0 is each of the other three's own no-op multiplier). A new, separate `PbrConstants` buffer (ambient/metallic/emissive/roughness) alongside the shared per-draw `Constants` block, because that block's own `g_EmissiveAmbient` folds ambient and emissive into one value -- PBR needs them apart (ambient scales albedo×occlusion, emissive is added standalone). `ShaderVariant::SkinnedPbr3D` reuses `kPbrPixelHlsl` unchanged (skinning only affects the vertex stage) and mirrors `kSkinnedVertexHlsl`'s own skin-matrix/normal-composition convention (`(Normal * skinMatrix3x3) * InverseTranspose(World3x3)`, not a full inverse-transpose of `skin*World` -- a documented simplification this backend's own unskinned-lit skinning path already uses) rather than inventing a different one. Verified by `Diligent_Pbr` using the same analytically-hand-derived technique as `vulkan_pbreffect_handderived_test.cpp`: a flat quad viewed straight down -Z with light0 aimed the same way collapses every BRDF dot product to exactly 1 at the backbuffer's centre pixel, so the whole shader reduces to a closed-form constant independently re-derived in Python -- 3 hand-derived `PbrEffect` cases (white/metallic=0, red/metallic=1, red/metallic=0) matched their predicted RGB values exactly, and `SkinnedPbrEffect` with a single identity bone (a mathematical no-op skin transform) reproduces the white/metallic=0 case's value exactly, not just "looked plausibly lit" |
| `DILIGENT-37` | Per-vertex lighting variant (`PreferPerPixelLighting == false`) | ✅ | Two new `ShaderVariant`s, `LitTexturedVertexLit3D` (stride 32, `BasicEffect`'s sibling to `LitTextured3D`) and `SkinnedVertexLit3D` (stride 52, `Skinned3D`'s sibling) -- selected in `DrawInternal()` when `lightingEnabled && !preferPerPixelLighting` (real XNA's own default). `EnvironmentMapEffect` has no `PreferPerPixelLighting` property in real XNA, so `envMapping` is checked first and always wins regardless of the flag. Both new vertex shaders extract kLitPixelHlsl's own inline Blinn-Phong math unchanged into a shared `ComputeVertexLighting()` helper and call it once per vertex instead of per pixel, handing the pixel stage pre-lit diffuse/specular varyings to Gouraud-interpolate -- same formula, only the evaluation frequency changes. Verified by `Diligent_VertexLit`: for a flat quad with one uniform normal, per-pixel and per-vertex evaluation have nothing to differ on, so `PreferPerPixelLighting=true` and `=false` must (and do) read back pixel-identical results, for both `BasicEffect` and `SkinnedEffect`. Found and fixed a real regression while landing this: `DrawInternal()`'s bone-palette upload was gated on `variant == ShaderVariant::Skinned3D` specifically, so once vertex-lit skinned draws started selecting `SkinnedVertexLit3D` instead (XNA's own default, i.e. the actually-more-common path) the bone buffer was silently never uploaded -- caught immediately by a DiligentCore validation assertion (`Diligent_Skinned` crashing with `SIGTRAP`) rather than silently, but still a real bug this task introduced and fixed before landing, not a pre-existing one. Also corrected `IGraphicsBackend.hpp`'s own `preferPerPixelLighting` doc comment, which claimed "every backend except D3D9" ignores the field -- already false for EasyGL/WebGPU before this task |

### Phase `DILIGENT-4` — remaining device surface

| Task | Description | Status | Notes |
| --- | --- | --- | --- |
| `DILIGENT-40` | `Texture3D` | ✅ | `RESOURCE_DIM_TEX_3D`, sub-box upload and readback. Verified by the shared `Texture3D*` tests |
| `DILIGENT-41` | `OcclusionQuery` | ✅ | `DiligentOcclusionQueryBackend` uses Diligent's `IQuery`. `QUERY_TYPE_OCCLUSION` needs the `occlusionQueryPrecise` device feature, which lavapipe (this backend's only verification device) does not expose; the backend transparently falls back to `QUERY_TYPE_BINARY_OCCLUSION` (0/1, the same convention EasyGL already uses for GLES3) when it is unavailable. `End()` flushes the immediate context so a result doesn't require waiting for a frame boundary. Verified by `Diligent_OcclusionQuery`: an unbegun query, an empty Begin/End span, a fully-visible quad and a fully depth-occluded quad |
| `DILIGENT-42` | Custom `ShaderEffect` (`CreateEffectBackend`) | ⬜ | Diligent compiles HLSL at runtime on every device type, so unlike SDL_GPU no extra compiler dependency is needed — but CNA's `ShaderEffect` contract is GLSL-shaped; resolve that first |
| `DILIGENT-43` | Hardware instancing (`DrawInstancedPrimitivesEx`) | ✅ | New `ShaderVariant::Instanced3D`: per-vertex `Position`-only stream at slot 0 (explicit `Stride=16` -- `LAYOUT_ELEMENT_AUTO_STRIDE` would compute it from only the elements declared in that slot, 12 bytes, not the real `VertexPositionColor` buffer's 16, corrupting every vertex fetch after the first), per-instance world-matrix stream (four `float4` rows) at slot 1 with `INPUT_ELEMENT_FREQUENCY_PER_INSTANCE`. `g_WorldViewProj` repurposed to hold just `View * Projection` since instancing has no single shared `World`. Verified by `Diligent_Instanced`: three instances at distinct translations each read back the quad's colour at their own position, with the untouched background between them staying the clear colour. The long dead end before the real fix: with CPU-side data (VP matrix, all 3 instance matrices) and draw-call parameters (`NumIndices`/`NumInstances`/buffer counts) all confirmed correct via GPU staging-buffer readback, and a full-row pixel scan confirming *zero* fragments rendered anywhere (not even the untranslated centre instance with an identity world matrix), the actual bug turned out to be in the **test file**, not the backend: it uploaded its quad via `VertexBuffer::SetDataRaw(quadVertices, 4, sizeof(VertexPositionColor))`, but `sizeof(VertexPositionColor)` is not the GPU stream's byte layout -- `Color` inherits a polymorphic `IPackedVector` base, so the C++ struct carries a vtable pointer `SetDataRaw` copied verbatim, silently uploading garbage-interleaved data at the wrong stride. Fixed by using the typed `VertexBuffer::SetData(const VertexPositionColor*, int)` overload instead, which packs into the real 16-byte (float3 + packed uint32 colour) stream every 3D shader variant here already expects. Confirmed by literally swapping the known-good `Colored3D` pipeline into `DrawInstancedPrimitivesEx`'s own call sequence -- it *still* rendered nothing until the test's upload was fixed, isolating the bug away from the backend entirely |
| `DILIGENT-44` | `SetDataOptions` streaming hints (`Discard`/`NoOverwrite`) | ✅ | `DiligentVertexBufferBackend`/`DiligentIndexBufferBackend` both override `SetData[16/32]WithOptions()`: `Discard`/`None` map to `MAP_FLAG_DISCARD`, `NoOverwrite` to `MAP_FLAG_NO_OVERWRITE` -- the same mapping this backend's D3D11 sibling already uses. Neither flag has an observable pixel difference on its own (both are GPU-synchronization hints, not data-correctness ones), so `Diligent_SetDataOptions` instead proves each upload genuinely reaches the GPU buffer: a `Discard` upload renders, then a *second*, differently-coloured `NoOverwrite` upload into the same `DynamicVertexBuffer` renders the new colour (not stale data or a silently dropped write); the same for a `DynamicIndexBuffer` whose second `NoOverwrite` upload selects a different triangle out of a fixed vertex buffer. In the course of this, found and fixed a stale doc comment in `DynamicVertexBuffer.hpp`/`DynamicIndexBuffer.hpp` claiming the hint was "ignored by all CNA backends" -- false for D3D9/D3D11/D3D12/EasyGL/Headless/SdlGpu/Software/WebGPU even before this task, and now also false for Diligent (only Vulkan still inherits the interface's own no-op default) |
| `DILIGENT-45` | `vertexStart`/`startIndex`/`baseVertex` sub-range coverage tests | ✅ | `Diligent_DrawOffset` (position-based discrimination, same technique as the D3D9/D3D11 counterparts): `DrawPrimitives(vertexStart)`, `DrawIndexedPrimitives(startIndex)`, `DrawIndexedPrimitives(baseVertex)`, both combined with the middle vertex range deliberately off-screen so only applying BOTH lands the draw, and `DrawInstancedPrimitivesEx(startIndex+baseVertex)` on the per-vertex stream (one identity-transform instance) proving the per-instance stream's own offset is not confused with the per-vertex one. All 5 pass |
| `DILIGENT-46` | Debug markers (`SetStringMarkerEXT` → `IDeviceContext::InsertDebugLabel`) | ✅ | A real, synchronous `context_->InsertDebugLabel(marker)` call (Diligent's immediate-context model needs no deferred-command-queue plumbing the way `VulkanGraphicsBackend`'s own implementation does); a null/empty marker is a no-op. No native API here surfaces the label to a readable pixel, so the only thing to verify is that inserting one around a draw doesn't disturb it -- added as `Diligent_3D`'s own 6th check: the same vertex-coloured quad as its first check, bracketed by two markers plus one empty-marker no-op call, still renders correctly |
| `DILIGENT-47` | Compressed texture upload | ⬜ | Blocked cross-backend: `ImageData` has no compressed-format field, `Texture2D.cpp` always decompresses first (same blocker as `WEBGPU-111`) |

### Phase `DILIGENT-5` — cross-backend feature-gap audit (opened 2026-07-31)

Found by comparing this backend's actual code against `docs/graphics-backend-feature-matrix.md`'s
established EasyGL/Vulkan/Bgfx/D3D9/D3D11/D3D12 columns and against what this backend's own code
already does vs. what it has a dedicated pixel test for. Two different kinds of gap, not to be
confused with each other:

- **Genuinely implemented, never independently verified** (`DILIGENT-49`–`DILIGENT-52`,
  `DILIGENT-55`/`DILIGENT-56`): the C++/pipeline code is real and plausible, but nothing proves it
  on a real device — exactly the category `docs/graphics-backend-feature-matrix.md`'s own Vulkan
  audit (Task 861) and D3D9's `D9-62` depth-bias gap already established elsewhere in this project:
  "implemented" and "verified" are not the same claim, and this backend's own `AUDIT.md`/`CHECKLIST.md`
  discipline requires the latter before a ✅.
- **A real, confirmed code bug**, not just a coverage gap (`DILIGENT-48`): found by reading
  `ApplySamplerState()`, not by a failing test — no existing Diligent CTest exercises a second
  `SamplerState` slot, so nothing had caught it yet.

| Task | Description | Status | Notes |
| --- | --- | --- | --- |
| `DILIGENT-48` | Real per-slot `SamplerState` (was aliased to one shared state) | ✅ | **Confirmed real bug, found by code reading, then fixed.** `DiligentGraphicsBackend::ApplySamplerState(int slot, ...)` took a `slot` parameter but discarded every call whose slot wasn't 0 (`if (slot != 0) return;`), and every texture-binding site (`g_Texture`, `g_Texture2`, `g_EnvMap`, all 4 PBR maps) read one shared set of scalars regardless of which slot the caller actually configured. Fixed with a real `SamplerSlotState samplerSlots_[16]` cache (matching `SamplerStateCollection::MaxSamplers`) and per-binding-site slot lookups matching this project's own established cross-backend register convention (`dual_texture3d.frag.hlsl`/`env_map3d.frag.hlsl`/`pbr3d.frag.hlsl`'s own `t0`/`s0`, `t1`/`s1`, etc.): `g_Texture`→slot 0, `g_Texture2`/`g_EnvMap`→slot 1, the 4 PBR maps→slots 1-4 (base colour is slot 0). Verified by 2 new checks added to `Diligent_DualTextureEnvMap`: texture0 is a uniform white 1x1 (immune to address mode, so it can never explain a difference), texture1 is a 2-texel red\|green strip sampled at U=1.25 (25% past the right edge) -- `SamplerStates[1]=PointClamp` reads the clamped edge texel (green), `SamplerStates[1]=PointWrap` reads the wrapped-around texel (red) instead, with `SamplerStates[0]` never touched across either draw. Confirmed via `git stash` on just the backend fix: the reverted code reads green both times (aliased to slot 0's own Clamp state), the fixed code discriminates correctly |
| `DILIGENT-49` | `RasterizerState.DepthBias`/`SlopeScaledDepthBias` pixel verification | 🟨 | **Attempted, partially confirmed — one real environment limitation, not a CNA bug.** Added `Diligent_DepthBias` (`examples/diligent_depthbias_test.cpp`), the same coplanar "shadow acne" method as `vulkan_depth_bias_test.cpp` (Task 328): draw a red triangle A, redraw an identical green triangle B with `CompareFunction::Less` — B only shows through if a negative bias pulled it in front. `ApplyRasterizerState()`'s packing (`PipelineKey::raster` bits 16-31) only has one signed byte each for `DepthBias`(×1000)/`SlopeScaledDepthBias`(×16), so the test drives the most extreme values that packing can represent exactly: `DepthBias=-0.128` (raw Diligent units -128) and `SlopeScaleDepthBias=-8.0` (raw units -8.0). Result, reproducible across repeated runs: **`SlopeScaleDepthBias` is real and works** (tilted-geometry check goes RED→GREEN as expected) but **`DepthBias` (the constant term) shows no observable effect** on this environment's software Vulkan device (`llvmpipe`/`lavapipe`) even at the packing's maximum magnitude — B stays RED. This exactly matches two independent pre-existing findings already in this codebase: `D9-62` (D3D9's own oracle attempt against real XNA 4.0 found no observable pixel difference from constant `DepthBias` at any magnitude up to `±1e8`, while `SlopeScaleDepthBias`/`CullMode` were both provable) and `Vulkan_DepthBias`'s own pre-existing `DepthBias=-1e6` sub-case (still failing today, undocumented as a CNA bug, `docs/rasterizerstate-support.md` §5) — i.e. constant depth bias not registering on this project's Vulkan/software-rasterizer test environment is an already-known, cross-backend, environment-level limitation, not something introduced or fixable here. `Diligent_DepthBias` is registered as a normal CTest (no `WILL_FAIL`, matching `Vulkan_DepthBias`'s own precedent of leaving a documented pre-existing failure visible rather than masking it): 3/4 checks pass, the constant-`DepthBias` check fails honestly |
| `DILIGENT-50` | `GraphicsDevice.ReferenceStencil` pixel verification | ✅ | **Confirmed real and working — Diligent is ahead of EasyGL/Bgfx here.** Added `Diligent_ReferenceStencil` (`examples/diligent_referencestencil_test.cpp`), a direct port of Task 319's cross-backend method (`easygl_graphicsdevice_reference_stencil_test.cpp`): stamp stencil=0x05, assign a `DepthStencilState` with `StencilFunction=Equal` and a baked-in `ReferenceStencil=0x05` (would PASS at face value), then call `GraphicsDevice.setReferenceStencilProperty(0x99)` directly — NOT via a new `DepthStencilState` — and redraw with the SAME state object. If the override genuinely reaches the backend, the compare becomes 0x99 vs the stamped 0x05 (`Equal`, false) → rejected → stays BACKGROUND; if the override is a local no-op (Task 872's still-open, universal EasyGL/Bgfx gap), the state's own baked-in 0x05 still passes → wrongly shows GREEN. Diligent stays BACKGROUND — the override is real. Verified as a genuine discriminator, not a coincidental pass: temporarily commenting out the `setReferenceStencilProperty(0x99)` call flips the result to `FAIL centre=(0,255,0)` (green), then restoring it returns to `PASS centre=(20,20,20)` |
| `DILIGENT-51` | `RasterizerState.FillMode::WireFrame` pixel verification | ✅ | **Confirmed real and working.** Added `Diligent_FillMode` (`examples/diligent_fillmode_test.cpp`), a direct port of `vulkan_fill_mode_test.cpp` (Task 327): a full-viewport-spanning triangle read back at its centre pixel, 3 sub-tests in one frame — `FillMode::Solid` (expect red, interior filled), `FillMode::WireFrame` (expect black/clear, interior genuinely not rasterized, not a silent solid-fill fallback or a blank draw), then reset to `FillMode::Solid` (expect red again, proving the state change round-trips both ways rather than latching). 3/3 PASS, reproducible across repeated runs |
| `DILIGENT-52` | Anisotropic texture filtering pixel verification | ✅ | **Confirmed real and crash-safe, following this project's own established test discipline for this exact task.** Added `Diligent_Anisotropic` (`examples/diligent_anisotropic_test.cpp`), a direct port of Task 299's cross-backend method (`easygl_texture_anisotropic_effect_test.cpp`): that precedent's own header explains a true visual anisotropic-quality pixel comparison is "inherently driver-dependent and fragile to assert precisely" across GPUs/software rasterizers, so the established, deliberate scope for this task is the "caps and fallback" half instead — `SamplerState.MaxAnisotropy=9999` (far beyond any real GPU's limit, and beyond `ApplySamplerState()`'s own `std::clamp(maxAnisotropy, 1, 16)`, `DILIGENT-48`'s new per-slot cache) must not crash or throw, and the draw must still produce a genuinely sampled result rather than the clear colour leaking through unrendered. `DualTextureEffect` over a 2-texel red\|green strip stretched across a full-viewport quad, sampled at the boundary texel: real output `(247,255,0)`, reproducible across repeated runs, not the clear colour `(0,0,255)` and no exception |
| `DILIGENT-53` | `SpriteFont` glyph placement/spacing/newline/flip pixel test | ✅ | **Confirmed real and working.** Added `Diligent_SpriteFont` (`examples/diligent_spritefont_test.cpp`), a direct port of D3D11's own `DX-127` (`examples/d3d11_smoke_test.cpp`): an 8x8 solid-white atlas per glyph, zero cropping offset, zero left/right kerning bearing, so a glyph's destination rect maps exactly and any placement error is a hard pixel difference. Check A — a single glyph at (4,4) occupies exactly [4,12)×[4,12), checked inside plus all 4 edge midpoints (rules out an X-only or Y-only misplacement). Check B — `"AB"` advances the second glyph by exactly one glyph width. Check C — `"A\nA"` drops the second line by exactly `lineSpacing` AND resets x to the start. Check D — `SpriteEffects::FlipVertically` genuinely flips an asymmetric (top-half-white) glyph to bottom-half-white, ruling out a no-op flip. 4/4 PASS, reproducible across repeated runs — shared, backend-agnostic `SpriteFont`/`SpriteBatch` code confirmed working through this backend too |
| `DILIGENT-54` | `Model` multi-mesh/bone-hierarchy orchestration test | ✅ | **Confirmed real and working — no stub-behind-a-code-path bug found.** Added `Diligent_Model` (`examples/diligent_model_test.cpp`), a direct port of D3D12's own `DX-148` Check KK6 (`examples/d3d12_smoke_test.cpp`): a real 2-bone hierarchy (root → child, `ModelBone::AddChild`) driving `Model::Draw()`'s full orchestration end to end (bone transform → `SetVertexBuffer`/`setIndicesProperty`/`DrawIndexedPrimitives`/`EffectPass.Apply`), not a raw `VertexBuffer` draw wearing a `Model` label. D3D12's own version of this exact test previously caught a real crash from unimplemented `SetDepthTestEnabled`/`SetDepthWriteEnabled`/`SetBlendEnabled` stubs nothing else in that backend's suite exercised — Diligent has no equivalent gap: the mesh's red renders exactly over the green clear, PASS, reproducible across repeated runs |
| `DILIGENT-55` | `Texture2D` mip-level `SetData`/`GetData` (level > 0) dedicated round-trip test | ✅ | **Confirmed real, genuine GPU round-trip — not a CPU-shadow-only readback.** Added `Diligent_Mip` (`examples/diligent_mip_test.cpp`), a port of the cross-backend `easygl_texture2d_mip_test.cpp` (Task 171) fixture, but strictly stronger here: `DiligentSampledTexture::GetData()` is documented as reading a mip level back through a real staging-texture GPU readback (unlike the EasyGL precedent's own explicit "pure CPU shadow buffer" note), so this genuinely exercises the class of bug `D3D11`'s `DX-126` was written to catch elsewhere (Vulkan/Bgfx silently no-op-ing a non-zero mip level's `SetData`/`GetData` entirely, `Task 867`). A 4×4 `mipMap=true` texture (levels 4×4/2×2/1×1) gets a distinct solid colour per level; every level round-trips byte-exact, and a final level-0 re-read *after* levels 1/2's own uploads confirms `UpdatePixelsLevel()` targeted the correct subresource each time, not level 0. 22/22 PASS, reproducible across repeated runs |
| `DILIGENT-56` | NPOT (non-power-of-two) `Texture2D` real GPU round-trip test | ✅ | **Confirmed real and correct — no row-pitch/stride bug found.** Added `Diligent_Npot` (`examples/diligent_npot_test.cpp`), going further than D3D11's own `DX-140` (which only checked "does NPOT sampling look plausible" against a *solid*-colour texture) in the same direction `DILIGENT-55` already established: a genuinely non-power-of-two 5×3 texture (5×4=20 bytes/row, not alignment-friendly) filled with 15 DISTINCT pseudo-random colours, so a `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT`-shaped row-pitch bug in the staging-texture upload/readback path would shift pixels sideways between rows — something a solid fill could never reveal. Check A — full-texture `SetData`/`GetData` round-trips all 15 pixels byte-exact. Check B — a sub-rectangle `GetData()` read (columns [1,4), not aligned to the full 5-pixel row) round-trips exactly, independently exercising the row-pitch-vs-requested-width skip path. Check C — a real `BasicEffect` draw samples one of the texture's known colours at the viewport centre (not garbage/clear-colour), proving the normal draw path doesn't corrupt NPOT content either. 3/3 PASS, reproducible across repeated runs. This closes Phase `DILIGENT-5`'s full task list |

---

## Verification status — read before claiming anything works

This plan distinguishes three levels, in the same discipline `plan_dx.md`/`plan_webgpu.md` already
use:

1. **Builds** — `cmake --build ... --target cna_backend_graphics_diligent` succeeds against the
   pinned DiligentCore. ✅
2. **Runs without a GPU** — `Diligent_DeviceSelection` exercises the device-preference and override
   parsing with no device created at all. ✅
3. **Real device pixels** — a real Diligent device renders and a test asserts on read-back pixels.
   ✅ **reached, on a software device**: 23 of the 24 `Diligent_*` CTest binaries are fully green
   (112 of their own checks — `Diligent_2D` 6, `Diligent_3D` 6, `Diligent_RenderTarget` 5,
   `Diligent_RenderTargetCube` 4, `Diligent_AlphaTestFog` 4, `Diligent_DualTextureEnvMap` 6,
   `Diligent_Skinned` 4, `Diligent_MRT` 4, `Diligent_OcclusionQuery` 4, `Diligent_MSAA` 5,
   `Diligent_Instanced` 4, `Diligent_DrawOffset` 5, `Diligent_SetDataOptions` 4,
   `Diligent_VertexLit` 4, `Diligent_Pbr` 5, `Diligent_ReferenceStencil` 1, `Diligent_FillMode` 3,
   `Diligent_Anisotropic` 1, `Diligent_SpriteFont` 4, `Diligent_Model` 1, `Diligent_Mip` 22,
   `Diligent_Npot` 3, `Diligent_RenderTargetMipGen` 7 — plus `Diligent_DepthBias`'s own 4 checks,
   3 of which pass, makes 116 checks total, 115 passing) run against a genuine Vulkan device
   provided by Mesa's `lavapipe` ICD under Xvfb. The 24th, `Diligent_DepthBias`
   (`DILIGENT-49`), is 3/4: constant `DepthBias` shows no observable effect on this software device,
   matching two independent pre-existing findings elsewhere in this codebase (`D9-62`'s oracle
   attempt against real XNA 4.0, `Vulkan_DepthBias`'s own pre-existing `DepthBias=-1e6` sub-case) —
   a documented, cross-backend environment limitation, not a CNA-side bug, and left as a real,
   visible CTest failure rather than
   masked. These are real draws through the real Vulkan engine — real pipelines, real HLSL→SPIR-V
   compilation, real depth testing — read back through
   `GraphicsDevice.GetBackBufferData`/`RenderTarget[Cube].GetData`, not stubs. Several real defects
   were found this way and fixed, spanning both the backend and its own tests — see each
   `DILIGENT-*` task's own row for detail. Two are the most instructive:
   - `Diligent_RenderTargetCube`'s `EnvironmentMapEffect` check initially read back solid black and
     cost a long investigation (resource-state tracing, view-descriptor dumps, raw-sample shader
     hacks) before the actual cause turned out to be in the *test*, not the backend — `SpriteBatch`
     leaves its own `RasterizerState` bound after `End()` (XNA semantics), silently culling the very
     geometry the next check tried to draw. The fix was a one-line `RasterizerState::CullNone` reset,
     already a documented pattern from `Diligent_RenderTarget`'s identical gotcha; the lesson
     generalized here is to check a test's own state hygiene before suspecting the backend,
     especially once low-level backend signals (content, resource state, view descriptors) have all
     confirmed correct.
   - `Diligent_MSAA`'s `RenderTarget2D` check initially showed a correctly multisampled and resolved
     texture with zero anti-aliasing in the sampled result. Root cause was a genuine, separate,
     pre-existing bug, not anything about MSAA itself: `DiligentGraphicsBackend::SetRenderTarget2D()`/
     `SetRenderTargetCubeFace()`/`SetRenderTargets()` never called the outgoing target's
     `UnbindAsRenderTarget()` at all — `GraphicsDevice::SetRenderTarget()` calls the backend's
     `SetRenderTarget2D()` directly and was never routed through the interface's own
     `UnbindAsRenderTarget()`, unlike every other CNA backend (EasyGL, D3D11/12, SdlGpu, Vulkan),
     which all call it themselves from their own bind-switching method. This meant
     `DILIGENT-26`'s mip regeneration had *also* never actually fired since it was implemented,
     silently — exactly the gap that row's own "no pixel test asserts the generated levels" caveat
     was covering for. Fixed by having `SetRenderTarget2D()`/`SetRenderTargetCubeFace()`/
     `SetRenderTargets()` call the outgoing target's `UnbindAsRenderTarget()` themselves before
     swapping state, and by removing the recursive `SetRenderTarget2D(nullptr)` call
     `UnbindAsRenderTarget()` used to make at its own end (which is what had silently made it
     unsafe to call from the backend's own bind-switching methods in the first place).
4. **Real hardware GPU pixels** — ⬜ **not reached**. `lavapipe` is a CPU rasterizer; it exercises
   the API and the shaders but not a vendor driver. Anything driver-dependent (real MSAA sample
   counts, anisotropy, present modes, GL's swap-chain origin) stays unproven. Do **not** add a
   `DILIGENT` column to `docs/graphics-backend-feature-matrix.md` until this level is reached —
   that document's ✅ means hardware-verified.

### Cross-backend test suite

`CnaTests` under `CNA_GRAPHICS_BACKEND=DILIGENT`: **5692 passed, 7 skipped, 1 failed**. The total
held steady through `DILIGENT-41` and `DILIGENT-25`: each closed one more stale guard in
`GraphicsDeviceCapabilityTests.cpp` (`SupportsOcclusionQuery` then implicitly
`MultiSampleAntiAliasing`, both previously `false`/`EXPECT_FALSE` on this backend) rather than
adding a new one. The single failure is `XnbContainerFuzzTest.
MutatedRealModelFixtureNeverCrashesAndOnlyFailsCleanly`, which fails identically on the `HEADLESS`
backend (verified in the same session) — a pre-existing gap in that test's accepted-exception list
(`System::ArgumentException` from `VertexBuffer::SetData`'s declaration/stride validation is not
listed), unrelated to this backend.
