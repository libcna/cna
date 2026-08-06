# Magnum Backend Implementation Plan

> CNA's `MAGNUM` backend renders through [Magnum](https://github.com/mosra/magnum)'s typed OpenGL
> wrappers (`Magnum::GL`) on a desktop OpenGL 3.3 core context, with SDL3 still owning the window
> and the GL context. It is CNA's fifteenth graphics backend and its first desktop-GL one — every
> other GL-family backend here targets an ES/WebGL profile (`EASYGL`) or a different API entirely.
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented.
>
> **Verified baseline (2026-08-04, `MAGNUM-1`–`MAGNUM-41`):** a clean Linux x86_64 configuration
> builds Corrade+Magnum and `libcna_backend_graphics_magnum.a`, and the backend renders end-to-end
> against Mesa's `llvmpipe` software rasterizer under `Xvfb` — clear, SpriteBatch textured quad, a
> 3D coloured primitive draw and a `RenderTarget2D` round trip all read back their expected pixels
> (`examples/magnum_smoke_test.cpp`). The full `CnaTests` suite runs green against this backend,
> including the shared multi-stream and instanced-draw pixel oracles.
>
> **Remaining work (check this section first; update it whenever a row's status changes):**
> - **Stock effect variants** — all of them are now generated and pixel-verified:
>   `DualTextureEffect` (`MAGNUM-50`), `EnvironmentMapEffect` (`MAGNUM-51`), `SkinnedEffect`
>   (`MAGNUM-52`), `PbrEffect`/`SkinnedPbrEffect` (`MAGNUM-53`) and both lighting families that
>   `PreferPerPixelLighting` selects between (`MAGNUM-60`).
> - **`SurfaceFormat` beyond `Color` (`MAGNUM-54`)** — every texture, cube, volume and render target
>   allocates RGBA8. The requested format reaches the backend and is recorded but not honoured.
> - **`BlendState.MultiSampleMask` (`MAGNUM-55`)** — only the all-ones default is applied. Magnum's
>   `GL::Renderer` wraps no sample-mask state at all (it has `SampleShading` and
>   `SampleAlphaToCoverage`, but neither `GL_SAMPLE_MASK` nor `glSampleMaski`), so closing this
>   means either calling into flextGL directly -- bypassing the wrapper layer this backend exists to
>   go through -- or contributing the state upstream. Deliberately left open rather than decided
>   unilaterally; a non-default coverage mask is rare in XNA content.
> - **Context-loss channel (`MAGNUM-58`)** — `SetContextRecoveryEnabled` /
>   `DebugSimulateContextLoss` / `DebugRestoreContext` keep `IGraphicsBackend`'s defaults.
> - **Cross-backend pixel-parity run (`MAGNUM-59`)** — the same scene measured on
>   EasyGL/Vulkan/Magnum, not started.

## Design decisions

1. **SDL3 keeps the window and the GL context.** Magnum ships `Platform::Sdl2Application`, which
   would own both — but CNA's `GraphicsDevice` already creates an SDL3 window and every other
   windowed backend borrows it. `Platform::GLContext` is Magnum's own supported entry point for
   exactly this case: it attaches to whatever context is already current. Magnum's SDL application
   is also SDL2-only, so adopting it would mean a second windowing stack alongside SDL3.

2. **No Emscripten target.** `Platform::GLContext` takes its entry points from one of Magnum's four
   platform context libraries (GLX/EGL/WGL/CGL), none of which exists for Emscripten — there the
   loader is baked into `EmscriptenApplication`, which owns the window and event loop CNA already
   owns through SDL3. A hard `FATAL_ERROR` in `cmake/BackendSelection.cmake` says so, mirroring the
   `CANVAS` (Emscripten-only) and `D3D11`/`D3D12`/`D3D9` (Windows-only) gates.

3. **Pinned master revisions, not the last release.** Magnum's last tag (`v2020.06`) predates the
   toolchains CNA builds with. `CNA_CORRADE_GIT_TAG` / `CNA_MAGNUM_GIT_TAG` pin the exact upstream
   revisions this backend was developed against, so a fetch is reproducible without tracking a
   moving branch.

4. **`CNA_MAGNUM_ROOT` before `FetchContent`.** An explicit install prefix is preferred for offline
   and reproducible builds, and a mismatch there is a hard error rather than a silent fallback to a
   download — the same shape `cmake/ThirdPartyWebGPU.cmake` already established.

5. **Only the used Magnum libraries are built.** `MAGNUM_WITH_GL` plus one platform context library.
   Magnum's `Shaders`, `Trade`, `Primitives`, `MeshTools`, `SceneGraph`, `Text`, `TextureTools` and
   `DebugTools` are all switched off: CNA generates its own GLSL and owns its own resource types, so
   every one of them left on is build time paid for nothing.

6. **Sampler state lives on the texture.** Magnum has no sampler-object wrapper, so a slot's
   requested `SamplerState` is remembered by the backend and written onto whichever texture the draw
   binds there. The whole state is written on every application, never only the changed fields: a
   texture object is long-lived and shared between draws, so a value left behind by an earlier
   ordinal (anisotropy in particular) would otherwise persist into every later one.

7. **Stock shaders are generated per vertex layout, with uniform gates.** One program per built-in
   stride (16/20/24/32), each carrying lighting, texturing and vertex-colour as uniform gates rather
   than as separate compiled variants. Ordinary XNA content draws the same layout both lit and unlit
   (`BasicEffect.LightingEnabled` is a per-draw property), so a gate keeps one program serving both
   instead of doubling the program count for a branch every driver folds away on a uniform.
   Lighting *frequency* is the exception and is a real variant: `PreferPerPixelLighting` decides
   which STAGE evaluates the lighting, which no uniform branch can express. The two families share
   one generated `cnaLighting()` function so the flag changes nothing but where it runs.

8. **`#version` is stripped before handing source to Magnum.** `GL::Shader` emits its own `#version`
   from the version passed to its constructor and then prefixes each added source with `#line`, so a
   source declaring one itself fails to compile outright. Both routes reach this — CNA's stock
   shaders declare their version for readability, and a `ShaderEffect`'s GLSL is ordinary
   caller-authored code that normally starts with one — so the strip lives in one place,
   `MagnumProgram::CompileAndLink`.

9. **Owned GL objects are released in front of the context.** Magnum's GL object destructors consult
   `GL::Context::current()` and abort outright when there is none. Member destruction order alone is
   not enough, because it runs after the destructor body that tears the context down, so every
   resource the backend owns is explicitly released there first. Found empirically: the stock shader
   cache outliving the context aborted the whole test binary rather than leaking a handle.

10. **Back-buffer MSAA comes from the context.** `SDL_GL_MULTISAMPLESAMPLES` makes the default
    framebuffer itself multisampled, so there is no off-screen buffer for this backend to resolve by
    hand every `Present()`. The applied count is read back from SDL rather than assumed.

11. **A vertex array is cached against its binding, and the draw's own range is not part of it.**
    Primitive, element count, instance count, base vertex and index offset are per-draw setters on
    an existing `GL::Mesh`, so a draw sweeping its start vertex reuses one cached array instead of
    building one per value. The shared start term therefore goes through the native base-vertex
    parameter (`glDrawArrays`'s `first`, `glDrawElementsBaseVertex`'s basevertex) rather than being
    folded into the attribute offsets; only a per-stream `vertexOffset` remainder is folded, because
    it differs per stream and no single native term expresses it. The cache lives on the vertex
    buffer, not on the graphics backend, so a destroyed buffer takes its own arrays with it -- and
    the key holds a monotonic buffer identity rather than an address, because a destroyed buffer's
    address can be reused by a later one and a key matching on the address alone would silently
    draw the wrong data.

## Tasks

| ID | Task | Status |
|----|------|--------|
| MAGNUM-1 | `cmake/ThirdPartyMagnum.cmake`: pinned Corrade+Magnum acquisition with `CNA_MAGNUM_ROOT` / system-install / `FetchContent` routes | ✅ |
| MAGNUM-2 | Backend selection + link wiring (`CNA_GRAPHICS_BACKEND=MAGNUM`, `CNA_BACKEND_MAGNUM`, per-platform context component) | ✅ |
| MAGNUM-3 | `SDL_WINDOW_OPENGL` on the shared window path; `GraphicsBackendType::Magnum` registration | ✅ |
| MAGNUM-4 | Emscripten hard gate | ✅ |
| MAGNUM-10 | GL 3.3 core context creation, `Platform::GLContext` init, capability banner | ✅ |
| MAGNUM-11 | Present, swap interval, back-buffer MSAA | ✅ |
| MAGNUM-12 | Virtual resolution, presentation modes, window/logical transforms | ✅ |
| MAGNUM-13 | Clears: colour/depth/stencil and every combination, with XNA's write-mask and write-enable overrides | ✅ |
| MAGNUM-14 | `ReadBackbuffer` with top-row-first normalization | ✅ |
| MAGNUM-20 | `Texture2D`: full-chain storage, level upload, readback | ✅ |
| MAGNUM-21 | `TextureCube` and `Texture3D`: storage, `SetData`, `GetData` | ✅ |
| MAGNUM-22 | `RenderTarget2D`: framebuffer, depth/stencil, mip regeneration, MSAA resolve, readback | ✅ |
| MAGNUM-23 | `RenderTargetCube`: per-face binding, per-face MSAA storage, `SetData`/`GetData` | ✅ |
| MAGNUM-24 | Multiple render targets (up to 4) | ✅ |
| MAGNUM-25 | Vertex/index buffers, including `SetDataOptions` orphaning and declaration capture | ✅ |
| MAGNUM-26 | Occlusion queries | ✅ |
| MAGNUM-30 | `SpriteBatch`: batching, rotation/origin/flip/tint/transform, sampler state, custom-effect program | ✅ |
| MAGNUM-31 | `ShaderEffect` runtime GLSL compile/link, uniform assignment, texture binding, real compile diagnostics | ✅ |
| MAGNUM-32 | Stock shader generation per built-in layout (diffuse, vertex colour, texture, 3 lights + specular, ambient, emissive, alpha test, fog) | ✅ |
| MAGNUM-33 | Draw routes: non-indexed, indexed, instanced; per-stream offsets and instance frequencies | ✅ |
| MAGNUM-34 | Multi-stream vertex input, including a per-instance matrix split across several bindings | ✅ |
| MAGNUM-35 | Pipeline state: blend, colour write masks, depth, two-sided stencil, cull, wireframe, polygon offset, scissor, viewport, depth range | ✅ |
| MAGNUM-36 | Per-slot sampler state applied on texture bind | ✅ |
| MAGNUM-40 | GTest coverage for the context-free surface (enum mappings, layout resolution, generated GLSL) | ✅ |
| MAGNUM-41 | `examples/magnum_smoke_test.cpp` + `ctest -R Magnum_Smoke` integration test | ✅ |
| MAGNUM-42 | `docs/magnum-backend.md` | ✅ |
| MAGNUM-43 | Stock shader selection keyed on program kind (stride + effect flags), not stride alone | ✅ |
| MAGNUM-44 | A bound stream with a declaration-less (stride 0) buffer falls back to its uploaded stride | ✅ |
| MAGNUM-50 | `DualTextureEffect` shader variant (strides 20 and 24), pixel-verified | ✅ |
| MAGNUM-51 | `EnvironmentMapEffect` shader variant (cube-map reflection, flat and Fresnel-weighted), pixel-verified | ✅ |
| MAGNUM-52 | `SkinnedEffect` shader variant (bone palette, `weightsPerVertex`, strides 52 and 56), pixel-verified | ✅ |
| MAGNUM-53 | `PbrEffect` / `SkinnedPbrEffect` shader variants (strides 48 and 68), pixel-verified | ✅ |
| MAGNUM-54 | Real `SurfaceFormat` storage beyond `Color` | ⬜ |
| MAGNUM-55 | `BlendState.MultiSampleMask` -- blocked on Magnum wrapping no sample-mask state; see the note above | ⬜ |
| MAGNUM-56 | MSAA for an ordered multi-target set, pixel-verified | ✅ |
| MAGNUM-57 | Cache `GL::Mesh` per binding configuration instead of building one per draw, pixel-verified | ✅ |
| MAGNUM-58 | Context-loss simulation/recovery channel | ⬜ |
| MAGNUM-59 | Cross-backend pixel-parity run (EasyGL/Vulkan/Magnum, same scene) | ⬜ |
| MAGNUM-60 | `PreferPerPixelLighting=false` per-vertex-lit shader family, pixel-verified | ✅ |
| MAGNUM-61 | Compile each shader stage at the GLSL version its own source declares | ✅ |

## Shared-test gates this backend changed

Three shared tests encoded a single backend's limitation as if it were universal. Each now names the
backend it is true of instead of asserting it everywhere:

- `GraphicsDeviceCapabilityTest` asserted a flat "no wireframe", which is GLES3's limitation (and
  therefore EasyGL's). Desktop GL has a real `glPolygonMode` and Vulkan reports its device's own
  `fillModeNonSolid`, so the strict arm now applies where it is true and every other backend gets
  the same "querying it does not throw" contract MSAA and anisotropic filtering already had.
- `RenderTargetCubeSetDataContractTest` gated "SetData is a real upload" on `CNA_BACKEND_EASYGL`.
  Magnum's render-target cube attaches an ordinary GL cube texture too, so an upload reaches it
  directly there as well.
- `InstancedDrawMultiStreamTests` gates its pixel oracles on the set of backends that actually
  rasterize an instanced draw and consume per-binding offsets and frequencies. Magnum joins both
  sets, which is what makes its instanced multi-stream path measured rather than skipped.
