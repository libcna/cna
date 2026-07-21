# OpenGL 4 Graphics Backend — Implementation Plan

> **Status (2026-07-21): Phase 1 (`GL4-1`–`GL4-12`) landed and verified.** `CNA_GRAPHICS_BACKEND=OPENGL4`
> configures, builds (`cna_backend_graphics_opengl4`), and a real window with a real desktop
> `SDL_GL_CONTEXT_PROFILE_CORE` context (confirmed via `glGetString(GL_VERSION)` reporting
> `4.5 (Core Profile) Mesa 25.2.8` on this dev machine's llvmpipe/Mesa driver under Xvfb) clears
> color/depth/stencil, uploads a real `Texture2D`, draws a real `SpriteBatch` scene (tint, alpha,
> rotation/flip, source-rectangle cropping, and all three sampler address modes — Wrap/Clamp/
> Mirror), and draws real `colored3d` geometry (`VertexPositionColor`/`BasicEffect.
> VertexColorEnabled`) both non-indexed and indexed, including a genuine depth-test occlusion
> proof verified two ways (draw-order-independent). Verified by `OpenGL4_Smoke` (8/8),
> `OpenGL4_Readback` (10/10), and `OpenGL4_3D` (4/4) — all real pixel-readback assertions via
> `GraphicsDevice.GetBackBufferData()`, not just "didn't throw" — `ctest -R OpenGL4`, all pass.
> Two real bugs were found and fixed while getting there — see `GL4-7`/`GL4-9`'s rows below.
>
> **Deliberately independent of EasyGL/`easy-gl`.** EasyGL requests
> `SDL_GL_CONTEXT_PROFILE_ES` (OpenGL ES 3.0 / WebGL2 — see `EasyGLGraphicsBackend`'s
> constructor), not a real desktop OpenGL 4.x core profile: no geometry/tessellation shaders, no
> desktop-only `GL_ARB_*` features, and `glGetString(GL_VERSION)` never reports "4.x" under that
> context. This backend requests `SDL_GL_CONTEXT_PROFILE_CORE` (4.1 minimum — the highest core
> version macOS's own driver ever exposes) and never touches `easy-gl` or the `metagl`/`easygl::`
> wrapper library it's built on. Its own hand-rolled loader (`GL4Loader.hpp`/`.cpp`) resolves the
> ~40 GL 1.2+ entry points a core-profile program needs (buffers, VAOs, shaders/programs,
> `glActiveTexture`, separate blend funcs, sampler objects) via `SDL_GL_GetProcAddress` — no
> third-party GL-loader dependency (no glad/GLEW vendored), matching this project's existing
> "zero new third-party dependency" preference for a from-scratch native backend (see
> `plan_sdlgpu.md`'s own "Why an SDL GPU backend" rationale).
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented.
>
> **Platform scope until expanded by a completed task:** Linux desktop (X11, verified under both
> a real X11 session's driver and Xvfb's software/llvmpipe GL 4.5 implementation), x86_64.
> Windows and macOS are code paths only (the `SDL_GL_CONTEXT_MAJOR/MINOR_VERSION`/
> `SDL_GL_CONTEXT_PROFILE_MASK` attributes and the loader are portable, and macOS's 4.1 ceiling is
> the reason this backend requests 4.1 rather than a higher minimum), not validation claims.
>
> **Remaining work (read this before picking a next task):**
> - **Render targets (`RenderTarget2D`/`RenderTargetCube`, MRT, MSAA)** — `CreateRenderTarget2D`/
>   `CreateRenderTargetCube` are not overridden, so every render-target request silently returns
>   `nullptr` via `IGraphicsBackend`'s own default. No FBO code exists yet at all. ⬜
> - **`Texture3D`/`TextureCube` (plain, non-render-target)** — `CreateTexture3D`/
>   `CreateTextureCube` are not overridden either; both return `nullptr`. ⬜
> - **`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`** — not overridden; both inherit
>   `IGraphicsBackend`'s default fallback to `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`,
>   which only reproduces the stride-16 `VertexPositionColor` layout correctly. A `BasicEffect`
>   with `TextureEnabled=true` (stride 20/24/32) or lighting enabled currently renders through
>   that same colored-only shader instead of throwing — geometry still draws, but texture/lighting
>   are silently ignored. `opengl4_3d_test.cpp` deliberately does not test those paths for this
>   reason (see its own top-of-file comment) rather than claim coverage it doesn't have. Needs:
>   `textured3d`/`colored_textured3d`/`lit_textured3d` GLSL variants (the existing
>   `VulkanGraphicsBackend`/`EasyGLGraphicsBackend` shaders are a solid algorithmic reference) and
>   real stride-keyed shader/VAO dispatch, mirroring `SdlGpuGraphicsBackend`'s own
>   `GetOrCreatePipeline*` pattern. ⬜
> - **`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`/`PbrEffect`** —
>   none implemented (all depend on the `DrawPrimitivesEx` work above landing first). ⬜
> - **Custom `ShaderEffect`** (NOXNA) — `CreateEffectBackend` is not overridden (default returns
>   `nullptr`). ⬜
> - **`ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState`** — not overridden (inherited
>   no-op defaults). `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` (the 3 pure
>   virtuals) and `SetViewport`/`ApplySamplerState` (both genuinely implemented) are the only real
>   per-draw state currently wired to real GL calls — cull mode, fill mode, scissor, blend
>   factors/equations, and stencil ops are all still whatever GL's own defaults are. ⬜
> - **MSAA** — `GraphicsBackendCreateArgs::multiSampleCount` is accepted but ignored;
>   `SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, ...)` is never called. `GetMultiSampleCount()`
>   keeps `IGraphicsBackend`'s own default (`0`). ⬜
> - **Occlusion queries** — `CreateOcclusionQuery` not overridden (returns `nullptr`, same
>   documented-permanent-limitation shape as `Headless`/`Software`/`SDL_gpu` for their own reasons
>   — unlike those, this is not a hard API limitation for OpenGL4 (`glGenQueries`/
>   `GL_SAMPLES_PASSED` exist), just not implemented yet). ⬜
> - **Mipmaps** — `CreateTexture`'s `ImageData::mipLevels` field is ignored; every texture is a
>   single mip level. `UpdatePixelsLevel` (per-level upload) is not overridden either. ⬜
> - **`TransformWindowToLogical`/`TransformLogicalToWindow`** — not overridden (default `false`),
>   so `Mouse`/touch physical→logical coordinate mapping doesn't work yet on this backend when a
>   non-default `virtualWidth`/`virtualHeight` is set. ⬜
> - **`DebugSimulateContextLoss`/`DebugRestoreContext`/`SetContextRecoveryEnabled`** — not
>   overridden (safe no-op defaults); this backend has no context-loss-recovery/CPU-shadow-copy
>   mechanism at all yet, unlike EasyGL's `metagl`-based one. ⬜
> - **Windows/macOS validation** — code paths only (see Platform scope above), not run on real
>   hardware yet. ⬜

---

## Why a real OpenGL 4 backend

- **EasyGL genuinely cannot do this.** EasyGL's context is OpenGL ES 3.0 (`SDL_GL_CONTEXT_PROFILE_ES`),
  chosen specifically because it doubles as the WebGL2 target for Emscripten builds (see
  `cmake/BackendSelection.cmake`'s own "EasyGL is the default on Linux and Emscripten (WebGL 2 =
  OpenGL ES 3.0)" comment). ES 3.0 and desktop GL 4.x are related but distinct APIs — no geometry/
  tessellation shaders, a narrower set of texture formats/compression, different (stricter) GLSL
  ES shading language rules, and no access to desktop-only `GL_ARB_*` extensions. A user or task
  that specifically wants real desktop OpenGL 4.x (e.g. to reach features ES doesn't have, or to
  match a specific driver/GPU debugging workflow that only understands desktop GL) cannot get that
  through `CNA_GRAPHICS_BACKEND=EASYGL` no matter what — a genuinely different backend is required.
- **Zero new third-party dependency**, same reasoning as `SDL_GPU`: the platform's own GL library
  (`libGL`/`opengl32`/`OpenGL.framework`, resolved via CMake's built-in `find_package(OpenGL)`)
  plus SDL3 (already vendored) is everything this backend links against. `GL4Loader.hpp`/`.cpp` is
  a small, hand-rolled loader for the handful of GL 1.2+ entry points a core-profile program needs
  — not a vendored copy of glad/GLEW/SDL's own bundled `SDL_opengl_glext.h`.
- **Consistent with the existing multi-backend architecture.** This is simply another
  `CNA_GRAPHICS_BACKEND` value following the exact same `IGraphicsBackend` contract every other
  backend already implements (`include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`).

This plan does **not** propose retiring any existing backend, least of all EasyGL.

---

## Naming conventions for this backend

| Item | Value |
| --- | --- |
| `CNA_GRAPHICS_BACKEND` value | `OPENGL4` |
| CMake option | `CNA_BACKEND_OPENGL4` |
| Compile definition | `CNA_BACKEND_OPENGL4` |
| Backend directory | `src/CNA/Internal/Backends/OpenGL4/`, `include/CNA/Internal/Backends/OpenGL4/` |
| CMake target | `cna_backend_graphics_opengl4` |
| Main class | `CNA::Internal::Backends::OpenGL4::OpenGL4GraphicsBackend` |
| GL loader | `CNA::Internal::Backends::OpenGL4::GL4` (`GL4Loader.hpp`/`.cpp`) |
| Task prefix | `GL4-` |
| CTest labels | `OpenGL4_Smoke`, `OpenGL4_Readback`, `OpenGL4_3D` (`ctest -R OpenGL4`) |

---

## Phase 1 — task table

| Task | Description | Status | Notes |
| --- | --- | --- | --- |
| `GL4-1` | CMake wiring: `CNA_BACKEND_OPENGL4` option, `BackendSelection.cmake`/`BackendLibraries.cmake` branches, `find_package(OpenGL REQUIRED)`, link `OpenGL::GL` + `SDL3::SDL3`. | ✅ | No new third-party dependency. |
| `GL4-2` | Hand-rolled GL 1.2+ loader (`GL4Loader.hpp`/`.cpp`) for buffers/VAOs/shaders/programs/`glActiveTexture`/blend/sampler-object entry points, via `SDL_GL_GetProcAddress`. | ✅ | All entry points named `gl4_glXxx` to avoid any ambiguity with the pre-1.2 functions linked directly against `libGL`. |
| `GL4-3` | Window/context lifecycle: real `SDL_GL_CONTEXT_PROFILE_CORE` context (4.1 minimum), `GetWindowInternal`/`GetRendererInternal` (null — no `SDL_Renderer`), `GetViewportSize`/`SetVirtualResolution`/`SetPresentationMode`/`SetSwapInterval`. | ✅ | Verified: `glGetString(GL_VERSION)` reports `4.5 (Core Profile) Mesa 25.2.8` on this dev machine. |
| `GL4-4` | `Clear`/`ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil`, correctly forcing `glDepthMask`/`glStencilMask` to full-write during the clear itself and restoring the tracked depth-write-enable state afterward (a depth/stencil clear must not be silently masked by a prior `SetDepthWriteEnabled(false)`). | ✅ | |
| `GL4-5` | `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled`. | ✅ | Real `glEnable`/`glDisable(GL_DEPTH_TEST\|GL_BLEND)` + `glDepthMask`. |
| `GL4-6` | `Texture2D` (`OpenGL4TextureBackend`): `glTexImage2D`/`glTexSubImage2D` upload, `UpdatePixels` (row-pitch-aware), `BindGL`. | ✅ | Single mip level only (`GL4-remaining` above). |
| `GL4-7` | `VertexBuffer`/`IndexBuffer` (`OpenGL4VertexBufferBackend`/`OpenGL4IndexBufferBackend`): VBO+VAO pair, stride-keyed attribute layout (16/20/24/32, matching `VertexPositionColor`/`Texture`/`ColorTexture`/`NormalTexture`), 16-bit index buffer. | ✅ | |
| `GL4-8` | `SpriteBatch` (`OpenGL4SpriteBatchBackend`): CPU-side per-quad vertex generation (position/rotation/origin/flip, matching `EasyGLSpriteBatchBackend`'s established math), one dynamic VBO/IBO flushed per texture change, alpha blending, sampler-object-driven filter/address-mode. | ✅ | Real bug found+fixed: `Begin()` used to reset `pendingFilter_`/`pendingAddressU_`/`pendingAddressV_`/`transform_` to defaults, but `SpriteBatch::Begin()` (the public class) calls `SetSamplerFilter`/`SetSamplerAddressMode`/`SetTransformMatrix` on the backend *before* calling `Begin()` — the reset silently discarded every non-default `SamplerState` a caller passed to `SpriteBatch::Begin()`, always rendering as Clamp regardless of the requested `TextureAddressMode`. Found by `OpenGL4_Readback`'s Wrap/Mirror checks (`AddressMode=Clamp` passed "by accident" — it matched the reset default). Fixed by not resetting those fields in `Begin()` at all, matching `EasyGLSpriteBatchBackend::Begin()`'s own precedent (only flips the `begun_` flag). |
| `GL4-9` | `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`: a `colored3d` GLSL 410 core program (`aPos`/`aColor` → `uWorldViewProj`), real depth-test occlusion. | ✅ | Real bug found+fixed: `GraphicsDevice::UpdateViewportFromWindow()` calls `IGraphicsBackend::SetViewport()` after every resize/at device creation; this backend didn't override it (inherited no-op default), so the real GL viewport was never set for the 3D draw path (only `SpriteBatch::FlushBatch()` set it, for its own draws) — every `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` call rendered nothing (readback saw plain background). Fixed by implementing `SetViewport()` (`glViewport`/`glDepthRange`, Y-flipped to XNA's top-left origin, mirroring `EasyGLGraphicsBackend::SetViewport`'s own fbH-based flip). Found by `OpenGL4_3D`'s pixel-readback checks. |
| `GL4-10` | `ReadBackbuffer` (`glReadPixels` + Y-flip, mirroring `EasyGLGraphicsBackend::ReadBackbuffer`'s own convention) — real pixel-level verification for every check above, not just "didn't throw". | ✅ | |
| `GL4-11` | `ApplySamplerState`: real GL sampler objects (`glGenSamplers`/`glBindSampler`/`glSamplerParameteri`), not texture-object-embedded state — Point/Linear/Anisotropic filter, Wrap/Clamp/Mirror address mode. | ✅ | |
| `GL4-12` | `GraphicsBackendCompileDefinitionTests.cpp`/`GraphicsBackendType.hpp` updated for the new backend (the latter was a genuine second registration point found only by actually attempting a full-library build — `getCurrentGraphicsBackendType()`'s `#error` fires if a backend defines its own `CNA_BACKEND_*` compile definition without a matching branch there). | ✅ | `ExactlyOneGraphicsBackendIsSelected` syntax-checked against this backend's compile definitions; full `CnaTests` link not attempted in this sandboxed session (see Verification methodology below). |

---

## Verification methodology

Mirrors the `SDL_GPU`/`WebGPU`/`D3D11` precedent: this backend's own dedicated CTest suite
(`ctest -R OpenGL4` — `OpenGL4_Smoke`, `OpenGL4_Readback`, `OpenGL4_3D`) is the validated
methodology for a real-window/GPU backend in this project, not a full unfiltered `CnaTests` run.
In this sandboxed dev environment, building the full `CnaTests` target hit pre-existing,
backend-independent gaps unrelated to this work (a `tools/audio/*_harness.cpp` target missing an
SDL3 include path, and a version-skew between this checkout's `Xnb` content readers and the
sibling `sharp-runtime` clone's `BinaryReader` — `ReadDecimal`/`ReadChar` — that a fresh
`sharp-runtime` checkout doesn't currently provide). Both reproduce identically under other
backends too and are out of scope for this plan; not fixed here. `GraphicsBackendCompileDefinitionTests.cpp`
was independently syntax-checked (`g++ -fsyntax-only`) against `CNA_BACKEND_OPENGL4`'s compile
definitions instead.

All three dedicated tests were run for real, under Xvfb (`SDL_VIDEODRIVER=x11`), on this dev
machine's Mesa/llvmpipe GL 4.5 core-profile implementation:

- `OpenGL4_Smoke` — 8/8 (window/context lifecycle, VertexBuffer/IndexBuffer round-trip incl.
  `SetDataWithOptions`/`SetData16WithOptions`, 60 frames of Clear+Present).
- `OpenGL4_Readback` — 10/10 (Clear visibility with no intervening Present, `SpriteBatch` partial
  coverage, alpha=0/50% blending, source-rectangle cropping, all three `TextureAddressMode` values).
- `OpenGL4_3D` — 4/4 (solid-color quad via `DrawPrimitives`, real depth-test occlusion proven both
  draw orders, `DrawIndexedPrimitives`).

---

## Active execution order — do this one task at a time

1. ~~`GL4-1`~~ – ~~`GL4-12`~~ — Phase 1 (infrastructure, 2D `SpriteBatch` vertical slice, `colored3d`
   3D with real depth-test proof) done and verified 2026-07-21, all ✅. See the task table above
   for the two real bugs found and fixed along the way.
2. **Next up (not yet started, no priority order implied):**
   - `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` + `textured3d`/`colored_textured3d`/
     `lit_textured3d` shader variants — unblocks `BasicEffect.TextureEnabled`/lighting, and in turn
     `AlphaTestEffect`/`DualTextureEffect`.
   - `RenderTarget2D` (FBO) — unblocks render-to-texture and, later, `EnvironmentMapEffect`'s
     env-map source once `TextureCube` exists.
   - `ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState` dynamic mapping — currently
     every 3D draw is hardcoded (whatever GL's own defaults are); a real `BlendState`/
     `DepthStencilState`/`RasterizerState` assigned on `GraphicsDevice` has no effect yet.
   - MSAA (`SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, ...)` at context creation, plus honoring
     `ApplyMultiSampleCount` for a runtime change).
   - Mipmap generation (`glGenerateMipmap`) for `CreateTexture`'s `mipLevels` request and
     `UpdatePixelsLevel`.

See the "Remaining work" section in the status banner above for the full, non-prioritized list of
everything else still open — do not treat the picks above as the only next options.
