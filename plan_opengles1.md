# OpenGL ES 1.1 Backend Implementation Plan

> The OpenGLES1 backend was authorized and started on **2026-07-21** as CNA's sixth graphics
> backend: a genuine **OpenGL ES 1.1 fixed-function ("Common"/CM profile)** implementation,
> deliberately independent of the EasyGL backend (EasyGL targets WebGL2/OpenGL ES 3.0, a
> shader-based programmable pipeline, and cannot create an ES 1.1 context at all — there is no
> code sharing between the two).
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code exists but has not met those criteria (most commonly: correct and builds, but could not
> be runtime-verified on this development host — see below); ⬜ not implemented.
>
> **A load-bearing finding from this backend's bring-up (2026-07-21), since fully explained and
> resolved — read the 2026-07-22 entries below before acting on anything here.** Bring-up could not
> create an ES1 context at all: `eglChooseConfig()` with
> `EGL_RENDERABLE_TYPE = EGL_OPENGL_ES_BIT` succeeded and reported every config ES1-capable, yet
> `eglCreateContext()` with `EGL_CONTEXT_CLIENT_VERSION = 1` failed on every one of them, while the
> same program requesting ES2 succeeded. That made every runtime-behavior row below `🟨` (code
> reviewed line-by-line against the ES 1.1 spec and this project's cross-backend conventions, and
> confirmed to compile and link against real system `GLESv1_CM`/`GLES/gl.h`) rather than `✅`.
>
> Two corrections were later established, both recorded in detail further down: the failing error
> code is `EGL_BAD_ALLOC`, not `EGL_BAD_CONFIG` as originally written; and the cause is **Debian
> shipping Mesa built with `-Dgles1=disabled`**, not absent hardware. A locally built
> `-Dgles1=enabled` Mesa now runs this backend for real on this very host.
>
> **Platform scope:** any host with a working OpenGL ES 1.1 (Common profile) driver reachable via
> `SDL_GL_CreateContext()` with `SDL_GL_CONTEXT_PROFILE_MASK = SDL_GL_CONTEXT_PROFILE_ES`,
> `major=1`, `minor=1`. Stock Debian Mesa does **not** qualify on any driver, including real GPUs;
> a side-by-side `-Dgles1=enabled` Mesa (software `softpipe` is enough) does, and is what
> `scripts/opengles1-test-env.sh` selects. Embedded/Android/PowerVR/Mali and proprietary desktop
> drivers remain valid targets, but are no longer *required* to make progress.
>
> **Root cause identified (2026-07-22) — it is a Debian packaging decision, not a hardware,
> driver, or upstream-Mesa limitation.** Re-tested on a separate Debian 13 host with a real AMD
> Radeon 780M (`radeonsi`/`amdgpu`, Mesa 25.0.7 — not the software-rasterizer container above), the
> failure reproduces identically, and digging into *why* produced a definite answer:
>
> - Debian's own Mesa packaging (`debian/rules` in the `mesa` source package) sets
>   **`confflags_GLES = -Dgles1=disabled -Dgles2=enabled`**. Debian deliberately builds Mesa
>   *without* OpenGL ES 1.x support. Upstream Mesa still implements ES1 — Debian just does not
>   ship it.
> - This matches the observed behaviour exactly. With `gles1` disabled, Mesa reports
>   `max_gl_es1_version = 0`, which drops `__DRI_API_GLES` from the screen's `api_mask`, so
>   `dri2_create_context()` refuses the ES1 API — while the EGL config list, computed separately,
>   still advertises every config as ES1-renderable *and* ES1-conformant.
> - The error is **`EGL_BAD_ALLOC` (`0x3003`)**, not `EGL_BAD_CONFIG` — earlier revisions of this
>   file and of `docs/opengles1-backend.md` mislabelled that code (`EGL_BAD_CONFIG` is `0x3005`).
>   Mesa's own debug log names the failure precisely:
>   `EGL user error 0x3003 (EGL_BAD_ALLOC) in eglCreateContext: dri2_create_context`.
> - All three Mesa drivers available on that host — `radeonsi` (real GPU), `llvmpipe`, and
>   `softpipe` — fail ES1 identically while ES2 succeeds on every one of them. A per-driver or
>   software-rasterizer explanation is therefore ruled out; the gate is build-time and common to
>   the whole Mesa package.
>
> So `OPENGLES1-77` is **not** blocked on exotic hardware after all: any Mesa built with
> `-Dgles1=enabled` (including a plain software `softpipe`/`llvmpipe` build) should be able to
> create the context and run the pixel tests. The remaining blocker is simply that no such Mesa
> build exists on this host yet.
>
> **RESOLVED, same day — the backend now runs for real.** That Mesa was then actually built
> (rootless: `apt-get download` + `dpkg-deb -x` for the missing `meson`/`bison`/`flex`/xcb
> toolchain, then `-Dgles1=enabled -Dgallium-drivers=softpipe,llvmpipe`; full reproducible recipe
> and its three non-obvious pitfalls are in `docs/opengles1-backend.md`). Against it:
>
> - the raw EGL probe now reports **`eglCreateContext` ES1 SUCCEEDED**, and
> - `cna_test_opengles1_clear_readback` runs end-to-end through the real backend and reports
>   `OpenGLES1GraphicsBackend initialized with OpenGL ES OpenGL ES-CM 1.1 Mesa 25.0.7`,
>   **5/5 checks PASS**, also green via `ctest -R OpenGLES1`.
>
> `scripts/opengles1-test-env.sh` wraps the loader environment so this is repeatable.
>
> **`OPENGLES1-79` then closed most of the remaining gap, same day.** Five further test
> executables now cover blend/depth/cull/sampler state, viewport and scissor, lighting, fog, the
> alpha test, real VBO/IBO draws, render targets, wireframe, dual texture, environment mapping and
> context loss — 37 pixel-asserted checks, all green under `ctest -R OpenGLES1`.
>
> **Running the code found three real defects that code review had not.** Each had shipped as
> "✅ code complete" and was wrong:
>
> - `OPENGLES1-24` — the alpha test was **inverted** for the Less/Greater family. The `AlphaTest`
>   vector's `z`/`w` are *branch outcomes*, not pass/fail weights, so `Greater` mapped to `GL_LESS`
>   and vice versa. `CompareFunction::Never` also passed everything instead of discarding it.
> - `OPENGLES1-72` — `RenderTarget2D::GetData()` returned all zeroes. Rendering into the target
>   worked; the backend simply never overrode `ITextureBackend::GetData`, which is a no-op default.
> - `OPENGLES1-11` — a texture uploaded before a context loss sampled as plain white afterwards.
>   The context was recreated but no GPU object was ever rebuilt.
>
> All three are fixed. A fourth gap found the same way — vertex/index buffer contents also died
> with the context — was tracked as `OPENGLES1-80` and has since been fixed too, so a restored
> context now rebuilds textures *and* buffers.
>
> **Every runtime row is now ✅.** A seventh test executable closed the last five
> (`OPENGLES1-9`/`10` virtual resolution and window↔logical transforms, `OPENGLES1-13` the
> `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` trio, `OPENGLES1-18` the
> `SpriteBatch` transform matrix, `OPENGLES1-20` separate colour/alpha blend factors). Seven
> executables, 51 pixel-asserted checks, all green under `ctest -R OpenGLES1`.
>
> `OPENGLES1-78` then measured the backend against the 39-scene XNA oracle corpus and found two
> further real defects (sampler state landing on the wrong texture object in the 3D paths, and
> `TextureAddressMode::Mirror` silently degrading to Wrap) — both fixed; see
> `docs/opengles1-parity-report.md`.
>
> **Every task row is now ✅ except `OPENGLES1-75`**, which is ⬜ because ES 1.1 genuinely has no
> occlusion-query mechanism at all (the shader/skinning/PBR gaps in the deviation table are the
> same kind of permanent limit).

## Design decisions

1. **Genuinely separate from EasyGL, not a variant of it.** EasyGL requests an ES 3.0 context
   (`SDL_GL_CONTEXT_MAJOR_VERSION=3`) and is entirely shader-based (`easygl::Program`, GLSL vertex/
   fragment sources baked into `EasyGLGraphicsBackend.cpp`). OpenGL ES 1.1 has no shader stage at
   all — a completely different rendering model (fixed-function matrix stack, texture
   environment, per-vertex lighting). Sharing code between the two would mean either bolting
   fixed-function emulation onto a shader backend (defeating the point of testing a real ES1
   target) or forcing EasyGL's shader assumptions into a context that cannot run shaders. The two
   backends share zero source files; both independently implement `IGraphicsBackend`.
2. **Real system library, not vendored.** Requires `libgles1`/`libgles-dev` (Debian/Ubuntu; other
   distros/embedded SDKs have equivalents) providing `libGLESv1_CM.so`, `GLES/gl.h`,
   `GLES/glext.h`. `cmake/BackendLibraries.cmake` `find_library`/`find_path`s these and fails with
   a clear `FATAL_ERROR` + install instructions if absent, matching `VULKAN`'s
   `find_package(Vulkan REQUIRED)` precedent — this is a hard system dependency, not something
   CNA fetches or vendors.
3. **Real GPU-side vertex/index buffer objects (OPENGLES1-73, revised 2026-07-21).** Originally
   planned as client-side arrays only (see history below), but `glGenBuffers`/`glBindBuffer`/
   `glBufferData` turned out to be **core** OpenGL ES 1.1 entry points (the `GL_OES_
   vertex_buffer_object` extension was folded into the 1.1 core spec, confirmed directly from the
   real system `GLES/gl.h` — unlike `GL_OES_framebuffer_object`/`GL_OES_texture_cube_map`, which
   really are separately-optional). `OpenGLES1VertexBufferBackend`/`OpenGLES1IndexBufferBackend`
   therefore hold a real GPU buffer object (`GL_DYNAMIC_DRAW` usage), with
   `glVertexPointer`/`glColorPointer`/`glTexCoordPointer`/`glNormalPointer`/`glDrawElements` all
   taking byte offsets into the bound buffer instead of raw client pointers — no runtime extension
   check needed, this always works on any real ES 1.1 implementation. `OpenGLES1IndexBufferBackend`
   additionally keeps a small CPU-side shadow copy of the raw index values, needed only for
   wireframe emulation (design decision 7) — mirrors `EasyGLIndexBufferBackend`'s own identical
   `GetCpuBytes()`-for-`DrawWireframe`-only pattern, not a return to client-array vertex storage.
4. **`GpuDrawParams` → fixed-function state translation, not a fallback-only stub.**
   `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` dispatch on vertex stride (16/20/24/32, the same
   convention every other CNA backend already uses) and translate what fixed-function ES 1.1 can
   actually express: texture (`GL_MODULATE`), up to 3 directional lights (`GL_LIGHT0..2`), fog,
   real dual-texture and environment-map multitexturing (design decisions 6/7 below), and a
   best-effort `glAlphaFunc` mapping of `AlphaTestEffect`'s tolerance-band test. Fields with
   genuinely no fixed-function equivalent (skinning, PBR, custom shader pointer, instancing) fall
   back to the plain `DrawColoredPrimitives` path rather than silently producing wrong output —
   see `docs/opengles1-backend.md`'s "Important limitations" for the full list and why each one is
   a genuine, permanent gap rather than a "not yet implemented" one.
5. **Lights are applied under a view-only `MODELVIEW`, not world×view.** `glLightfv(..., GL_POSITION, ...)`
   bakes in whatever `GL_MODELVIEW` matrix is current at the moment it's called — the classic
   fixed-function idiom for world-space lights is to load just the camera (view) matrix before
   setting light state, then load the full world×view matrix afterward for the actual vertex
   transform. Verified against the ES 1.1 spec's own description of light-position transformation,
   not assumed.
6. **No programmable shaders, ever — `CreateEffectBackend()` keeps the base `nullptr` default.**
   Distinct from "not yet implemented": ES 1.1 fundamentally has no shader compiler. Custom
   `ShaderEffect`, `PbrEffect`, and `SkinnedEffect`/`SkinnedPbrEffect` are permanent gaps for this
   backend — no `GL_OES_matrix_palette`-based skinning is implemented (real ES1 CM hardware support
   for that extension is rare enough that it wasn't pursued; PBR has no fixed-function analogue at
   all regardless of extensions). `EnvironmentMapEffect` turned out **not** to belong on this list
   — see decision 7.
7. **Real multitexturing, added 2026-07-21 (OPENGLES1-71/74/76, Phase 2).** Three techniques,
   confirmed against the real system `GLES/glext.h`, not assumed from the spec alone:
   - **`DualTextureEffect`** (`OPENGLES1-71`): `glActiveTexture`/`glClientActiveTexture` (both core
     ES 1.1, `GL_MAX_TEXTURE_UNITS` queried at startup — gated on `>= 2`, since the ES 1.1 spec only
     guarantees 1) plus two `GL_COMBINE` texture-environment stages reproduce
     `dual_texture3d.frag.glsl`'s exact formula (`(tex0*2) * tex1 * diffuseTint`) with no
     approximation — both units sample the *same* UV set (matches FNA's own vertex format: one
     texture-coordinate pair, not two).
   - **`EnvironmentMapEffect`** (`OPENGLES1-74`): `GL_OES_texture_cube_map` bundles real cube-map
     texture storage **and** `glTexGeniOES(GL_TEXTURE_GEN_STR_OES, GL_TEXTURE_GEN_MODE_OES,
     GL_REFLECTION_MAP_OES)` — genuine fixed-function automatic reflection-vector texture-coordinate
     generation, the classic technique this exact extension was designed for. Blended with the base
     lit color via a `GL_INTERPOLATE` combine stage using `GL_CONSTANT`'s alpha channel as the
     `envMapAmount` factor. Fresnel edge-weighting (`fresnelEnabled`) is **not** applied — no
     fixed-function per-vertex-varying blend factor exists without much deeper `GL_COMBINE`
     trickery — documented deviation, not a silent approximation.
   - **Wireframe** (`OPENGLES1-76`): re-expands each `TriangleList`/`TriangleStrip` draw into a
     `GL_LINES` edge list built from the (indexed draw's) CPU-side index shadow or (non-indexed
     draw's) sequential vertex order, exactly mirroring `EasyGLGraphicsBackend::DrawWireframe`'s
     already-established technique for the same "ES has no `glPolygonMode`" problem.
   All three probe their real extension/limit at startup (`GL_EXTENSIONS` string +
   `SDL_GL_GetProcAddress` resolution actually succeeding, or `GL_MAX_TEXTURE_UNITS`) and fall back
   cleanly (to the plain colored path, or to reporting unsupported) rather than assuming presence.

## Active execution order — do this one task at a time

1. `OPENGLES1-1` – `OPENGLES1-20` (baseline device/context/clear/present/viewport) — ✅ code
   complete 2026-07-21, ✅ runtime-verified 2026-07-22 (see the finding above).
2. `OPENGLES1-21` – `OPENGLES1-35` (`Texture2D`, vertex/index buffers, `SpriteBatch`) — same
   ✅ code / ✅ runtime status.
3. `OPENGLES1-36` – `OPENGLES1-55` (`DrawColoredPrimitives`/`DrawPrimitivesEx` fixed-function
   dispatch: texture, lighting, fog, alpha test) — same ✅ code / ✅ runtime status.
4. `OPENGLES1-56` – `OPENGLES1-70` (render state: blend/depth/stencil/rasterizer/sampler/scissor/
   viewport, `ReadBackbuffer`) — same ✅ code / ✅ runtime status.
5. `OPENGLES1-71` – `OPENGLES1-76` (Phase 2: dual texture, render target, real VBO, environment
   map, wireframe) — ✅ code complete 2026-07-21, ✅ runtime-verified 2026-07-22, same as every
   other row (see the finding above). `OPENGLES1-75` (occlusion query) researched and
   confirmed **impossible** on ES 1.1 core, not merely deferred — see its own row.
6. `OPENGLES1-77` — ✅ **done 2026-07-22.** A locally built `-Dgles1=enabled` Mesa gives a genuine
   ES1 driver on this host; seven test executables (51 checks) pass against it, flipping every
   runtime row to ✅ and exposing four real backend defects (see the finding above).
7. `OPENGLES1-79` — ✅ done 2026-07-22 (the five new test executables above).
8. `OPENGLES1-80` — ✅ done 2026-07-22 (buffer contents now survive a context loss).
9. `OPENGLES1-78` — ✅ done 2026-07-22 (39-scene corpus vs. real XNA 4.0; 6/39 exact vs. EasyGL's
   10/39, and it found two more real defects — see `docs/opengles1-parity-report.md`).
10. **Next:** explain the two open parity rows (`dualtexture_quad`, `fog_gradient_quad`), which are
    recorded as unexplained rather than rationalised away.

---

## Phase 1 — Baseline (device, 2D, fixed-function 3D colored/textured/lit draw)

| # | Task | Status | Acceptance criteria |
| --- | --- | --- | --- |
| OPENGLES1-1 | New backend directory `include/CNA/Internal/Backends/OpenGLES1/` + `src/CNA/Internal/Backends/OpenGLES1/`, `OpenGLES1GraphicsBackend.hpp`/`.cpp` | ✅ | Files exist, compile against real `GLES/gl.h`/`GLES/glext.h`. |
| OPENGLES1-2 | `cmake/BackendSelection.cmake`: add `OPENGLES1` to the `CNA_GRAPHICS_BACKEND` `STRINGS` list, `option(CNA_BACKEND_OPENGLES1 ...)`, `elseif` block setting `BACKEND_DIR`/`BACKEND_TARGET`/`CNA_BACKEND_DEFINE` | ✅ | `cmake -DCNA_GRAPHICS_BACKEND=OPENGLES1` selects the new backend dir/target. |
| OPENGLES1-3 | `cmake/BackendLibraries.cmake`: `find_library(GLESv1_CM)` + `find_path(GLES/gl.h)`, `FATAL_ERROR` with install instructions if missing, link `SDL3::SDL3` + the found library | ✅ | Configure fails clearly without `libgles1`/`libgles-dev`; succeeds and links correctly with them installed (confirmed on this container after installing the packages). |
| OPENGLES1-4 | `CMakeLists.txt`: `include(cmake/Tests/OpenGLES1Tests.cmake)` | ✅ | |
| OPENGLES1-5 | Real ES 1.1 context creation via `SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION=1, MINOR=1, PROFILE_MASK=ES)` + `SDL_GL_CreateContext()`, depth/stencil buffer attributes, clear `std::runtime_error` on failure | ✅ | Same pattern as `EasyGLGraphicsBackend`'s own ES3 context creation (major/minor/profile changed). Cannot be runtime-verified on this container — see finding above; will throw its own descriptive error rather than segfault/hang if a host's driver can't create the context. |
| OPENGLES1-6 | `RegisterForWindow`/`UnregisterForWindow` (mouse/touch coordinate bridge) | ✅ | Matches every other backend's identical registration calls in constructor/destructor. |
| OPENGLES1-7 | `Clear`/`ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil` | ✅ | Forces `glDepthMask`/`glStencilMask` writable before each clear (matches `EasyGLGraphicsBackend`'s identical convention) so the requested clear value always reaches the buffer regardless of the last-applied `DepthStencilState`. |
| OPENGLES1-8 | `Present()` (`SDL_GL_SwapWindow`) | ✅ | |
| OPENGLES1-9 | `GetViewportSize`/`SetVirtualResolution`/`SetPresentationMode`/`SetSwapInterval` | ✅ | Same `FixedHeightDynamicWidth` logical-size math as `EasyGLGraphicsBackend`. |
| OPENGLES1-10 | `TransformWindowToLogical`/`TransformLogicalToWindow` | ✅ | Same pure-uniform-scale math (no letterbox offset under the default presentation mode) as `EasyGLGraphicsBackend`'s identical methods. |
| OPENGLES1-11 | `DebugSimulateContextLoss`/`DebugRestoreContext` (destroy + recreate context, reload extension entry points) | ✅ | |
| OPENGLES1-12 | `ReadBackbuffer` (`glReadPixels` + row flip) | ✅ | Y-flip matches every other GL-based backend's top-left-origin convention. |
| OPENGLES1-13 | `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` | ✅ | |
| OPENGLES1-14 | `CreateTexture`/`OpenGLES1TextureBackend` (level-0 RGBA8 upload, default linear filter/repeat wrap) | ✅ | `GetNativeTexture()` returns `nullptr` (no `SDL_Renderer` involved), matching `EasyGL`'s identical convention for GL-based backends. |
| OPENGLES1-15 | `CreateVertexBuffer`/`OpenGLES1VertexBufferBackend` (real GPU `GL_ARRAY_BUFFER` object, `SetData`) | ✅ | See design decision 3 (revised 2026-07-21 — real VBO, not client-side array, once `glGenBuffers`/`glBufferData` were confirmed core ES 1.1). |
| OPENGLES1-16 | `CreateIndexBuffer16`/`OpenGLES1IndexBufferBackend` (real GPU `GL_ELEMENT_ARRAY_BUFFER` object + a small CPU-side shadow for wireframe emulation, `SetData16`) | ✅ | 32-bit indices fall back to the base class's `CreateIndexBuffer16` delegation (unimplemented on this backend, matches several other backends' current state). |
| OPENGLES1-17 | `CreateSpriteBatch`/`OpenGLES1SpriteBatchBackend`: quad batching, texture/rotation/origin/flip/layer math (ported from the same well-established formula every CNA backend's `SpriteBatch` uses) | ✅ | `glOrthof` top-left-origin projection, `GL_MODULATE` texture environment, per-vertex color. |
| OPENGLES1-18 | `SpriteBatch::SetTransformMatrix`/`SetSamplerFilter`/`SetSamplerAddressMode` | ✅ | |
| OPENGLES1-19 | `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` (`BasicEffect` with `VertexColorEnabled=true`, no texture/lighting) | ✅ | Loads `GL_PROJECTION`/`GL_MODELVIEW` directly from `Matrix::ToColumnMajor()`. |
| OPENGLES1-20 | `LoadExtensionEntryPoints()`: resolve `glBlendFuncSeparateOES`/`glBlendEquationOES` via `SDL_GL_GetProcAddress`, null-safe fallback | ✅ | Confirmed these two symbols exist in the real system `GLES/glext.h` on this container (`GL_OES_blend_func_separate`/`GL_OES_blend_subtract`). |
| OPENGLES1-21 | `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`: stride-based dispatch (16/20/24/32), texture (`GL_MODULATE`), fallback to colored path for skinning/PBR/instancing/custom-effect (dual-texture/env-map now real, see `OPENGLES1-71`/`74`) | ✅ | See design decision 4. |
| OPENGLES1-22 | Lighting: up to 3 directional lights (`GL_LIGHT0..2`), material diffuse/specular/emission/shininess, ambient via `GL_LIGHT_MODEL_AMBIENT`, applied under a view-only `MODELVIEW` | ✅ | See design decision 5. |
| OPENGLES1-23 | Fog (`GL_FOG`, `GL_LINEAR` mode, `fogStart`/`fogEnd`/`fogColor`) | ✅ | Matches `BasicEffect`'s eye-space start/end convention (same semantics as every shader-based backend's fog implementation, just via fixed-function `glFog*` instead of a shader uniform). |
| OPENGLES1-24 | Alpha test: best-effort `glAlphaFunc` mapping of `GpuDrawParams::alphaTest`'s 4-way tolerance-band test | ✅ | Documented deviation: fixed-function has exactly one comparison function, not a 4-way weighted band — see `docs/opengles1-backend.md`. |
| OPENGLES1-25 | `ApplyBlendState` (`glBlendFunc`/`glBlendFuncSeparateOES`, `glBlendEquationOES` where available) | ✅ | `BlendFactor`/`InverseBlendFactor` fall back to `SourceAlpha`/`InverseSourceAlpha` (no `glBlendColor` in ES 1.1 core); `BlendFunction::Max`/`Min` fall back to `Add` (no ES1.1 equivalent). |
| OPENGLES1-26 | `ApplyDepthStencilState` (depth func/write, single-sided stencil func/op/mask) | ✅ | Two-sided stencil not supported (documented deviation — no separate front/back stencil functions in ES 1.1 core). |
| OPENGLES1-27 | `ApplyRasterizerState` (cull mode, scissor enable, `FillMode::WireFrame` tracked for `OPENGLES1-76`) | ✅ | `DepthBias`/`SlopeScaleDepthBias` still silently ignored (no `glPolygonOffset` in ES 1.1). |
| OPENGLES1-28 | `ApplySamplerState` (filter/wrap per the bound texture unit) | ✅ | Single texture unit only in this baseline. |
| OPENGLES1-29 | `SetScissorRect`/`SetViewport` | ✅ | |
| OPENGLES1-30 | `SupportsCapability()` overrides: `MultiSampleAntiAliasing`/`MultipleRenderTargets`/`AnisotropicFiltering`/`OcclusionQuery`/`CustomEffects` all `false`; `WireFrame` `true` (revised 2026-07-21, see `OPENGLES1-76`) | ✅ | Matches this backend's actually-implemented feature set exactly — no capability is claimed that the code doesn't back. |
| OPENGLES1-31 | `docs/opengles1-backend.md`: status, build instructions, the EGL/Mesa ES1 finding, implemented baseline, important limitations, architecture notes | ✅ | |
| OPENGLES1-32 | `examples/opengles1_clear_readback_test.cpp` + `cmake/Tests/OpenGLES1Tests.cmake` (`OpenGLES1_Clear_Readback` CTest: `Clear`, `SpriteBatch` quad, `DrawUserPrimitives(VertexPositionColor)`, all via `GetBackBufferData()` pixel assertions) | ✅ | Compiles and links against the real backend; cannot execute to a PASS/FAIL result on this container (context creation throws — see the finding above). No automatic CTest skip is registered (mirrors `VulkanTests.cmake`'s own no-skip precedent — a missing driver is a real, visible test failure, not silently swallowed). |

---

## Phase 2 — Multitexturing, render targets, real VBOs (2026-07-21)

None of the rows below were required for a working baseline; they extend coverage closer to the
established EasyGL/Vulkan/WebGPU backends' feature set, within what ES 1.1 fixed-function can
actually express. All code-complete the same day Phase 2 was started — see design decisions 3/7
above for the technical detail behind each row.

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| OPENGLES1-71 | `DualTextureEffect` real dispatch via genuine ES 1.1 multitexturing (`glActiveTexture`/`glClientActiveTexture`, two `GL_COMBINE` stages reproducing the exact `(tex0*2)*tex1*diffuseTint` formula) | ✅ | Gated on `GL_MAX_TEXTURE_UNITS >= 2` (`SupportsSecondTextureUnit()`) and on the vertex stride being 20 or 24 (no normal data needed) — falls back to the plain colored path otherwise, not a silent wrong-output approximation. |
| OPENGLES1-72 | `RenderTarget2D` via `GL_OES_framebuffer_object` (runtime extension + entry-point-resolution check, `nullptr` returned when absent — no `FATAL_ERROR`, matching `IGraphicsBackend`'s own "backend can't support this" contract) | ✅ | `OpenGLES1RenderTargetBackend`: color texture + optional depth/stencil renderbuffer (`GL_DEPTH_COMPONENT16_OES` baseline, upgraded to 24-bit/packed-stencil only when `GL_OES_depth24`/`GL_OES_packed_depth_stencil` are also present). `ApplyLogicalViewportAndOrtho2D()` (used by `SpriteBatch`) now also sizes to the bound RT, mirroring `EasyGLGraphicsBackend::FlushBatch`'s identical `GetCurrentRenderTarget2DSize()` check. Mip generation and multisampling remain unimplemented (accepted-and-ignored parameters, matching several other backends' current gaps). |
| OPENGLES1-73 | Real GPU-side vertex/index buffer objects — `glGenBuffers`/`glBindBuffer`/`glBufferData` confirmed **core** ES 1.1 (not the originally-assumed optional extension), so no runtime check or client-array fallback was actually needed | ✅ | See design decision 3 (revised). `OpenGLES1IndexBufferBackend` also keeps a small CPU-side index shadow, needed only by `OPENGLES1-76`'s wireframe emulation. |
| OPENGLES1-74 | `EnvironmentMapEffect` via `GL_OES_texture_cube_map`'s `glTexGeniOES(..., GL_REFLECTION_MAP_OES)` automatic reflection-vector texcoord generation, blended via `GL_INTERPOLATE` | ✅ | Gated on the extension string **and** `glTexGeniOES` actually resolving via `SDL_GL_GetProcAddress`, and on vertex stride 32 (normal data required). Fresnel edge-weighting is not applied (documented deviation — see design decision 7 and docs/opengles1-backend.md). |
| OPENGLES1-75 | `OcclusionQuery` — **researched and confirmed impossible**, not merely deferred: the real system `GLES/gl.h`/`GLES/glext.h` (Debian/Ubuntu `libgles-dev`) contain no occlusion-query mechanism of any kind anywhere in the ES 1.1 CM registry (`GL_OES_query_matrix` is unrelated — it queries the current matrix stack, not visibility) | ⬜ (confirmed impossible) | `CreateOcclusionQuery()` keeps the base class's `nullptr` default; `SupportsCapability(OcclusionQuery)` stays `false` permanently, joining skinning/PBR/custom-shader in the "genuinely no fixed-function/extension path exists" category rather than the "not yet implemented" one. |
| OPENGLES1-76 | Wireframe emulation: re-expands `TriangleList`/`TriangleStrip` draws into `GL_LINES`, the same technique `EasyGLGraphicsBackend::DrawWireframe` already uses for its own no-`glPolygonMode` problem | ✅ | Indexed draws read the real triangle indices from `OpenGLES1IndexBufferBackend`'s CPU shadow (`OPENGLES1-73`); non-indexed draws use sequential vertex order. `SupportsCapability(WireFrame)` now reports `true`. |
| OPENGLES1-77 | Runtime verification on an ES1-capable driver | ✅ | Done 2026-07-22. Debian builds Mesa `-Dgles1=disabled`, so no stock Debian driver creates an ES1 context; a local `-Dgles1=enabled` softpipe build does (recipe in `docs/opengles1-backend.md`, environment in `scripts/opengles1-test-env.sh`). Seven test executables — 51 pixel-asserted checks — now pass against a real `OpenGL ES-CM 1.1` context, flipping every runtime row to ✅ and exposing four real defects on the way (OPENGLES1-24 inverted alpha test, OPENGLES1-72 missing render-target readback, OPENGLES1-11 textures and OPENGLES1-80 buffers not restored after a context loss). |
| OPENGLES1-78 | Cross-backend pixel-parity test (same scene rendered on OPENGLES1 vs. an already-verified backend) | ✅ | Done 2026-07-22. `cna_oracle_render_opengles1` builds the same `tools/xna-oracle/CnaOracleRender.cpp` the D3D9/EasyGL measurements already use, and `scripts/run-oracle-corpus-diff-opengles1.sh` renders all 39 checked-in scenes and diffs them against the real XNA 4.0 reference PNGs. Result: **6/39 exact at tolerance 0, 11/39 at tolerance 1**, against **10/39** for EasyGL on the same corpus and machine. A measurement, not a gate (same precedent as `D9-A6`). It found two real defects — sampler state applied to the wrong texture object on the 3D paths, and `TextureAddressMode::Mirror` degrading to Wrap despite `GL_OES_texture_mirrored_repeat` being available — both fixed. Full per-scene table and the two still-unexplained rows (`dualtexture_quad`, `fog_gradient_quad`) are in `docs/opengles1-parity-report.md`. |
| OPENGLES1-79 | Runtime coverage for the rows the baseline smoke test does not reach | ✅ | Done 2026-07-22. Five new test executables (`RenderState`, `ViewportScissor`, `LightingFogAlphaTest`, `BuffersRenderTarget`, `MultitextureContextLoss`) covering blend/depth/cull/sampler state, viewport and scissor, lighting/fog/alpha test, real VBO/IBO draws, render targets, wireframe, dual texture, environment mapping and context loss — 37 checks, all passing. Three backend defects found and fixed (see OPENGLES1-24/72/11). Remaining uncovered rows are listed in OPENGLES1-77. |
| OPENGLES1-80 | Restore vertex/index buffer contents across a GL context loss | ✅ | Done 2026-07-22. `OpenGLES1VertexBufferBackend` gained a raw-byte CPU shadow (draws still always read the GPU buffer) and `OpenGLES1IndexBufferBackend` reuses the shadow wireframe emulation already kept; both register with the backend and are rebuilt in `DebugRestoreContext()` alongside textures, unregistering in their destructors so the tracked pointers cannot dangle. Covered by a check in `OpenGLES1_MultitextureContextLoss_Readback` that fills both buffers, loses the context, and draws without re-uploading — verified to fail (black) with the restore loop removed and pass (green quad) with it. |

---

## Table of intentional deviations from XNA/FNA behavior (ES 1.1 fixed-function constraints)

| Area | XNA/FNA behavior | This backend's behavior | Why |
| --- | --- | --- | --- |
| `AlphaTestEffect` alpha test | 4-way weighted tolerance band (`GpuDrawParams::alphaTest`) | Best-effort single `glAlphaFunc` comparison | ES 1.1 fixed-function alpha test is a single comparison function; no tolerance-band concept exists. |
| `BlendState.ColorSourceBlend/DestinationBlend = BlendFactor/InverseBlendFactor` | Constant blend color via `GraphicsDevice.BlendFactor` | Falls back to `SourceAlpha`/`InverseSourceAlpha` | No `glBlendColor`/`GL_CONSTANT_COLOR` in ES 1.1 core. |
| `BlendState.ColorBlendFunction/AlphaBlendFunction = Max/Min` | GPU min/max blend equation | Falls back to `Add` | `GL_OES_blend_subtract` only defines Add/Subtract/ReverseSubtract. |
| `DepthStencilState` two-sided stencil | Independent CW/CCW stencil func/op | Only the CW (front) face's state applied | ES 1.1 core has no two-sided stencil API. |
| `RasterizerState.FillMode = WireFrame` | Real wireframe rasterization | Emulated: triangles re-expanded into `GL_LINES` at draw time (`OPENGLES1-76`) | No `glPolygonMode` in ES 1.1, so this is a real re-expansion, not GPU-native wireframe rasterization — `SupportsCapability(WireFrame)` reports `true` since the visual result is correct. |
| `RasterizerState.DepthBias`/`SlopeScaleDepthBias` | GPU depth bias | Ignored | No `glPolygonOffset` in ES 1.1. |
| Simultaneous per-vertex `Color` + `BasicEffect.DiffuseColor` | Both multiply together per pixel | Only one is applied (vertex color array takes precedence when `VertexColorEnabled`) | Fixed-function has exactly one "current color" input to `GL_MODULATE`; combining both needs `GL_COMBINE` multi-stage setup not implemented in this baseline. |
| `EnvironmentMapEffect.EnvironmentMapSpecular`/Fresnel edge-weighting | Per-pixel Fresnel-weighted blend + specular tint from cube alpha | Flat `envMapAmount` blend only (`OPENGLES1-74`) | No fixed-function per-vertex-varying blend factor exists without much deeper `GL_COMBINE` staging than this baseline implements. |
| `SkinnedEffect`/`SkinnedPbrEffect`, `PbrEffect`, custom `ShaderEffect`, instancing | Full GPU dispatch | Falls back to the plain colored path | No fixed-function equivalent exists at all (no `GL_OES_matrix_palette` skinning implemented; PBR/custom-shader/instancing have no ES 1.1 analogue regardless of extensions). Permanent gap, not "not yet implemented". `DualTextureEffect`/`EnvironmentMapEffect` are NOT on this list — see `OPENGLES1-71`/`74` above, both real. |
| `OcclusionQuery` | Real GPU visible-sample count | Unsupported (`nullptr`/`false`) | Confirmed by direct header inspection: no occlusion-query mechanism exists anywhere in the ES 1.1 CM registry (`OPENGLES1-75`). Permanent gap. |
