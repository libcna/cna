# Diligent Engine Graphics Backend — Implementation Plan

> **Status (2026-07-31): Phase `DILIGENT-1` (the 2D/3D baseline) is implemented.** What that means
> concretely is in the "What the baseline actually does" section below — read it before assuming
> parity with Vulkan/EasyGL/SDL_GPU, which this backend does **not** have. Render targets, cube and
> volume textures, MSAA, occlusion queries, custom `ShaderEffect` programs, instancing, and the
> `AlphaTest`/`DualTexture`/`EnvironmentMap`/`Skinned`/`Pbr` effect families are all **not**
> implemented, and each one *refuses loudly* rather than rendering a near-miss.
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
| CTest targets | `DiligentDeviceSelectionTest.*` (no GPU needed), `Diligent_2D`, `Diligent_3D`, `Diligent_RenderTarget` |

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
9. **OpenGL is built but unverified.** The device path exists and is reachable via
   `CNA_DILIGENT_DEVICE=opengl`, but every claim in this plan's ✅ rows was measured on the Vulkan
   device type. GL's swap-chain image origin differs from Direct3D's and CNA has not yet confirmed
   the sprite path's Y orientation there — treat GL as `🟨` until `DILIGENT-30` closes.
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
  `GetData` (`DILIGENT-23`/`DILIGENT-40`). They are storage and readback only so far — no shader
  variant samples them yet, so `EnvironmentMapEffect` is still refused.

Deliberately refused (each throws, naming itself):

- `RenderTarget2D` (`DILIGENT-20`/`DILIGENT-21`): off-screen colour, an optional real depth-stencil
  buffer, `GetData` readback, sampling the unbound target, and mip regeneration on unbind.

Deliberately refused (each throws, naming itself):

- Cube-map render targets, MRT (2..4 slots), occlusion queries, custom `ShaderEffect` programs,
  hardware instancing, fog, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`,
  `SkinnedEffect`, `PbrEffect`, MSAA. `SupportsCapability()` reports each of these honestly.

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
| `DILIGENT-12` | Render state family (`ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState`/`ApplySamplerState`) | ✅ | MRT slots 1..3 write masks and `MultiSampleMask` have no effect: single target, single sample |
| `DILIGENT-13` | `ReadBackbuffer` | ✅ | |
| `DILIGENT-14` | Honest `SupportsCapability()` + loud refusals for the unimplemented set | ✅ | Design decision 7 |
| `DILIGENT-15` | `Diligent_DeviceSelection` unit tests (no GPU required) | ✅ | Runs in the normal `CnaTests` suite |
| `DILIGENT-16` | `Diligent_2D`/`Diligent_3D` CTest binaries | ✅ | `Diligent_2D` 6/6 and `Diligent_3D` 5/5, on a real Vulkan device — Mesa `lavapipe` (software rasterizer) under Xvfb, not hardware. See "Verification status" |
| `DILIGENT-17` | `docs/diligent-backend.md` | ✅ | |

### Phase `DILIGENT-2` — render targets

| Task | Description | Status | Notes |
| --- | --- | --- | --- |
| `DILIGENT-20` | `RenderTarget2D` (`CreateRenderTarget2D`, `SetRenderTarget2D`, `SetRenderTargets` single slot) | ✅ | The pipeline cache key now carries the bound target's colour/depth formats, as predicted. Two real defects found while verifying: the key's `operator==`/hash had to learn the new field (a stale pipeline was reused and Vulkan rejected the render pass), and the sprite projection had to span the target rather than the window's logical canvas |
| `DILIGENT-21` | `RenderTarget2D` `GetData` readback and `PreserveContents` semantics | ✅ | Readback reuses `ReadTextureRegion`. Diligent's immediate context binds without a load operation, so contents always survive a bind cycle — which satisfies `PreserveContents` and is a legal superset of `DiscardContents` |
| `DILIGENT-22` | `RenderTargetCube` + per-face binding | ⬜ | `DILIGENT-23` (its dependency) is done |
| `DILIGENT-23` | `TextureCube` (`CreateTextureCube`, `SetData`/`GetData` per face) | ✅ | Six array slices of one `RESOURCE_DIM_TEX_CUBE`; full mip chain. Verified by the shared `TextureCubeTests`/`CnjCapabilityMatrixTests`/XNB cube fixtures, which now run for real on this backend instead of asserting the refusal |
| `DILIGENT-24` | MRT (`SetRenderTargets` with 2..4 slots) | ⬜ | Refused explicitly rather than binding slot 0 and dropping the rest. Unblocks the per-slot colour write masks `DILIGENT-12` currently ignores |
| `DILIGENT-25` | MSAA back buffer + render targets, device-probed clamping | ⬜ | `ApplyMultiSampleCount()` currently reports 1 |
| `DILIGENT-26` | Mip generation for render targets (`GenerateMips`) | 🟨 | Implemented: a mipped target is created with `MISC_TEXTURE_FLAG_GENERATE_MIPS` and regenerates on unbind, at the same point FNA3D does. No pixel test asserts the generated levels yet |

### Phase `DILIGENT-3` — remaining effect families

| Task | Description | Status | Notes |
| --- | --- | --- | --- |
| `DILIGENT-30` | Verify the OpenGL device type end-to-end (sprite Y orientation in particular) | ⬜ | Design decision 9 |
| `DILIGENT-31` | `AlphaTestEffect` (per-pixel discard, all four compare modes) | ⬜ | |
| `DILIGENT-32` | Fog for the `BasicEffect` family (`GpuDrawParams::fogVector`) | ⬜ | |
| `DILIGENT-33` | `DualTextureEffect` | ⬜ | Needs a second sampler slot in the resource layout |
| `DILIGENT-34` | `EnvironmentMapEffect` | ⬜ | `DILIGENT-23` (its dependency) is done; still needs a cube-sampling shader variant |
| `DILIGENT-35` | `SkinnedEffect` (72-bone palette) | ⬜ | Diligent has no push-constant size cap of SDL_GPU's kind; a uniform buffer is enough |
| `DILIGENT-36` | `PbrEffect`/`SkinnedPbrEffect` | ⬜ | |
| `DILIGENT-37` | Per-vertex lighting variant (`PreferPerPixelLighting == false`) | ⬜ | Same tracked cross-backend divergence as everywhere except D3D9 |

### Phase `DILIGENT-4` — remaining device surface

| Task | Description | Status | Notes |
| --- | --- | --- | --- |
| `DILIGENT-40` | `Texture3D` | ✅ | `RESOURCE_DIM_TEX_3D`, sub-box upload and readback. Verified by the shared `Texture3D*` tests |
| `DILIGENT-41` | `OcclusionQuery` | ⬜ | Diligent's `IQuery`/`QUERY_TYPE_OCCLUSION` maps directly |
| `DILIGENT-42` | Custom `ShaderEffect` (`CreateEffectBackend`) | ⬜ | Diligent compiles HLSL at runtime on every device type, so unlike SDL_GPU no extra compiler dependency is needed — but CNA's `ShaderEffect` contract is GLSL-shaped; resolve that first |
| `DILIGENT-43` | Hardware instancing (`DrawInstancedPrimitivesEx`) | ⬜ | |
| `DILIGENT-44` | `SetDataOptions` streaming hints (`Discard`/`NoOverwrite`) | ⬜ | Currently ignored via the interface's own default |
| `DILIGENT-45` | `vertexStart`/`startIndex`/`baseVertex` sub-range coverage tests | 🟨 | Forwarded to Diligent already; untested |
| `DILIGENT-46` | Debug markers (`SetStringMarkerEXT` → `IDeviceContext::InsertDebugLabel`) | ⬜ | |
| `DILIGENT-47` | Compressed texture upload | ⬜ | Blocked cross-backend: `ImageData` has no compressed-format field, `Texture2D.cpp` always decompresses first (same blocker as `WEBGPU-111`) |

---

## Verification status — read before claiming anything works

This plan distinguishes three levels, in the same discipline `plan_dx.md`/`plan_webgpu.md` already
use:

1. **Builds** — `cmake --build ... --target cna_backend_graphics_diligent` succeeds against the
   pinned DiligentCore. ✅
2. **Runs without a GPU** — `Diligent_DeviceSelection` exercises the device-preference and override
   parsing with no device created at all. ✅
3. **Real device pixels** — a real Diligent device renders and a test asserts on read-back pixels.
   ✅ **reached, on a software device**: `Diligent_2D` (6/6) and `Diligent_3D` (5/5) run against a
   genuine Vulkan device provided by Mesa's `lavapipe` ICD under Xvfb. These are real draws through
   the real Vulkan engine — real pipelines, real HLSL→SPIR-V compilation, real depth testing — read
   back through `GraphicsDevice.GetBackBufferData`, not stubs. Two real defects were found this way
   and fixed: the back buffer's actual format is not necessarily the one CNA requests (Diligent
   substitutes a supported one, and this surface gave BGRA), and the staging map needed
   `MAP_FLAG_DO_NOT_WAIT` to pair with the explicit `WaitForIdle()`.
4. **Real hardware GPU pixels** — ⬜ **not reached**. `lavapipe` is a CPU rasterizer; it exercises
   the API and the shaders but not a vendor driver. Anything driver-dependent (real MSAA sample
   counts, anisotropy, present modes, GL's swap-chain origin) stays unproven. Do **not** add a
   `DILIGENT` column to `docs/graphics-backend-feature-matrix.md` until this level is reached —
   that document's ✅ means hardware-verified.

### Cross-backend test suite

`CnaTests` under `CNA_GRAPHICS_BACKEND=DILIGENT`: **5692 passed, 7 skipped, 1 failed**. The single
failure is `XnbContainerFuzzTest.MutatedRealModelFixtureNeverCrashesAndOnlyFailsCleanly`, which
fails identically on the `HEADLESS` backend (verified in the same session) — a pre-existing gap in
that test's accepted-exception list (`System::ArgumentException` from `VertexBuffer::SetData`'s
declaration/stride validation is not listed), unrelated to this backend.

Twenty-one further tests failed before this backend declared its boundaries in the shared fixtures
(cube textures, render targets, custom effects, wireframe capability). Each was guarded the way the
repo already guards its narrower backends — a per-backend `#if` naming the `DILIGENT-` task that
removes it — rather than by loosening a shared assertion for every backend. `DILIGENT-23`/
`DILIGENT-40` have since removed the cube guards again: those tests now run for real here.
