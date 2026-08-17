# plan_igl.md — the IGL (facebook/igl) graphics renderer

CNA's 47th public renderer identity. Like `LLGL`, and unlike every renderer that names a native
graphics API, **`IGL` names a portable abstraction**: `igl::IDevice` fronts real OpenGL/OpenGL ES,
Vulkan and Metal implementations, and CNA picks which one this process uses rather than which one
this build contains.

Upstream: <https://github.com/facebook/igl>, pinned at **`v1.1.1`** (MIT). Local reusable checkout:
`~/deps/igl` (`CNA_IGL_ROOT`).

---

## 1. Status

**Branch:** `feature/igl`. **The first build has now happened** (2026-08-16, a fresh machine):
configure, compile, first frame and the four example/conformance tests all pass on the OpenGL/GLX
backend against Mesa llvmpipe. Phases A-G are verified on that one backend. The Vulkan backend was
also attempted against Mesa lavapipe: device bring-up and the smoke test are real and pass, but the
three pixel-conformance tests found a genuine, unresolved rendering-correctness bug (see IGL-60) --
**the Vulkan backend is not yet considered working**, only "boots and clears." The rest of Phase H
is still open. Tasks below still marked ✍️ are code paths that exist but have not yet been
individually exercised (e.g. MRT, the stock 3D effects beyond `BasicEffect`'s colour-only path,
custom `ShaderEffect`).

| Phase | What it covers | State |
|-------|----------------|-------|
| A | Identity registration and build integration | ✅ verified (OpenGL/GLX) |
| B | Device bring-up and presentation | ✅ verified (OpenGL/GLX) |
| C | Resources (textures, buffers, targets) | 🔶 `Texture2D`/`RenderTarget2D`/depth/`RenderTarget2D.GetData` (`Igl_RenderTarget`) and 2-slot MRT (`Igl_Mrt`) verified; cube/3D textures, RenderTargetCube, back-buffer readback beyond the smoke test's own use untested |
| D | 2D pipeline (`SpriteBatch`) | ✅ verified (`Igl_2D`) |
| E | 3D pipeline and stock effects | 🔶 `BasicEffect` colour-only + depth test (`Igl_3D`), `AlphaTestEffect` discard (`Igl_AlphaTestEffect`), `DualTextureEffect` (`Igl_DualTextureEffect`) verified; `EnvironmentMapEffect`/`SkinnedEffect`/`PbrEffect`/fog/instancing still ✍️ |
| F | Custom `ShaderEffect` | ✍️ written, unverified |
| G | Tests, docs and gates | 🔶 the four example tests pass; platform boundary gates ran clean for this work |
| H | Verification and hardening | 🔶 IGL-56/57/58/59 done; IGL-60-64 open |

Legend: ✅ done and verified · ✍️ code written, not yet compiled · 🔶 partial · ⬜ not started.

---

## 2. Design decisions

1. **Backend is fixed per process, not probed.** `Detail::ResolveRendererBackend()` resolves
   `CNA_IGL_BACKEND` (`auto` | `opengl`/`gl`/`glx` | `vulkan`/`vk`) against the backends compiled in,
   caches the answer, and never falls back. It cannot probe: `GraphicsDevice` needs the answer
   *before* the window exists (to choose its render intent), a native window cannot be both
   OpenGL- and Vulkan-capable, and a probe that failed afterwards could not undo that choice.
2. **OpenGL first in the default preference.** Not a maturity judgement about IGL — IGL's OpenGL
   backend is the one that can *adopt* the GL context CNA's own `IPlatformGlContext` already creates
   for the window (`igl::opengl::glx::Context`'s adopting constructor). Its other Linux GLX
   constructor opens its own display and leaves the drawable at `None`, which cannot present.
3. **Lazily opened render pass, not a deferred command list.** A clear ends the open pass and starts
   a new one carrying the clear as a load action; a draw joins the open pass, opening one that loads
   previous contents when none is; `Present()` closes it, presents and submits. This expresses
   exactly the load/store semantics a Vulkan-shaped API needs without CNA re-implementing a command
   buffer of its own (the shape `LlglRenderer` needed).
4. **One uber-shader per vertex layout, not one per effect permutation.** Lighting, fog, texturing,
   alpha test, dual texture, env mapping, skinning and PBR are *uniforms* (`CnaEffect.uFlags`), not
   `#define`s. The pipeline cache therefore grows with the vertex layouts a game actually uses
   instead of with the product of every effect switch. Only the attribute mask and the colour
   attachment count are real shader variants, because Vulkan rejects a shader input with no matching
   vertex attribute and requires the fragment outputs to match the render pass.
5. **Presentation lives in the projection, not the viewport.** IGL's two backends do not agree on
   what a non-zero viewport origin means (its Vulkan encoder computes `y = height - y` with a
   negative height), so this renderer always binds a full-surface viewport and folds XNA's
   `Viewport` rectangle and the letterbox/overscan rectangle into a clip-space scale/offset matrix.
   Clipping is done by the scissor, whose Y origin *is* converted per backend.
6. **Off-screen targets render Y-flipped.** Both backends present a bottom-left origin (IGL's Vulkan
   encoder flips the viewport height to match OpenGL), so a target rendered naively would sample and
   read back upside down. Rendering it flipped stores its rows top-first, which makes
   `RenderTarget2D.GetData()` and sampling agree with an uploaded `Texture2D`. The pipeline's
   front-face winding is reversed to match, so `CullClockwiseFace` culls the same triangles either
   way.
7. **Only the dependencies the library targets need are fetched.** IGL's own `deploy_deps.py`
   downloads every entry of `bootstrap-deps.json` (glfw, imgui, tracy, ktx-software,
   gfxreconstruct, …) — over a gigabyte for targets CNA never links.
   `cmake/ThirdPartyIGL.cmake` drives the same `third-party/bootstrap.py` with an explicit
   `-n <name>` list: `glm fmt glslang SPIRV-Headers` plus `volk vma` when the Vulkan backend is on.
8. **Loose `ShaderEffect` uniforms are OpenGL-only, and say so.** Non-block uniforms do not exist in
   Vulkan GLSL and IGL's Vulkan encoder deliberately leaves `bindUniform` unimplemented. A
   `ShaderEffect` with parameters is refused by name on the Vulkan backend rather than drawn with
   defaults.

---

## 3. Task list

### Phase A — identity and build integration

| ID | Task | State | Notes |
|----|------|-------|-------|
| IGL-1 | Add the `IGL` / `Igl` public identity | ✍️ | `GraphicsRendererType.hpp` (enum, `getCurrentGraphicsRendererType`, name), `scripts/check_renderer_identities.py` (46 → 47) |
| IGL-2 | Classify the backend | ✍️ | `GraphicsBackendCategory` = `TranslationLayer`, `GraphicsBackendMaturity` = `Experimental` |
| IGL-3 | CMake selection | ✍️ | `cmake/RendererSelection.cmake`: option, STRINGS, explicit-selection chain, `RENDERER_DIR`/`RENDERER_TARGET` mapping |
| IGL-4 | Third-party integration | ✍️ | `cmake/ThirdPartyIGL.cmake`: pinned `v1.1.1`, `CNA_IGL_ROOT` escape hatch, pruned dependency bootstrap, backend options |
| IGL-5 | Module skeleton | ✍️ | `modules/renderers/igl/{CMakeLists.txt,include,src,examples,tests}` |
| IGL-6 | Backend selection | ✍️ | `IglRendererSelection.{hpp,cpp}`, `CNA_IGL_BACKEND` |
| IGL-7 | Window render intent | ✍️ | `GraphicsDevice.cpp` asks `RendererBackendNeedsOpenGLWindow`/`…VulkanWindow` before the window is created |
| IGL-8 | Identity gates | ✍️ | the four `modules/graphics/tests/**` identity suites, `cmake/Tests/ModuleProbes.cmake` link-closure forbid lists |

### Phase B — device bring-up and presentation

| ID | Task | State | Notes |
|----|------|-------|-------|
| IGL-9 | OpenGL device on GLX | ✍️ | adopt the platform `IPlatformGlContext`'s context via `glXGetCurrentContext`, wrap in `igl::opengl::glx::Context`/`Device` |
| IGL-10 | Vulkan device | ✍️ | `igl::vulkan::HWDevice::createContext/queryDevices/create` from the X11 native handle |
| IGL-11 | Swap surface and framebuffer | ✍️ | `createTextureFromNativeDrawable`/`…Depth`, one `IFramebuffer` re-pointed per frame with `updateDrawable` |
| IGL-12 | Frame model | ✍️ | lazily opened render pass; clears as load actions; `Present()` = end pass + present + submit |
| IGL-13 | Presentation policies | ✍️ | all five `CnaPresentationMode` values, resize, `GetDefaultViewportRect`, `TransformWindowToLogical`/`…LogicalToWindow` |
| IGL-14 | Swap interval | ✍️ | honoured on OpenGL; Vulkan reports `false` rather than pretending |
| IGL-15 | Back-buffer MSAA | ✍️ | real on OpenGL (the platform GL visual); reported as 1 on Vulkan, whose swap-chain images are single-sample. Still unverified for the back buffer itself, but `igl_msaa_test.cpp` verified the sibling `RenderTarget2D` MSAA path this session and found a real bug: upstream IGL v1.1.1's OpenGL `Framebuffer::copyBytesColorAttachment` reads `colorAttachments[0]`'s own texture via `glReadPixels` with no check for a resolve texture (unlike its Vulkan counterpart, which does check), so reading back a multisampled `RenderTarget2D` failed with `GL_INVALID_OPERATION` -- OpenGL refuses `glReadPixels` against a multisample-attached framebuffer outright. Fixed in `IglRenderTargetRenderer::GetData()` (`IglResources.cpp`): when the target is multisampled, build a small ad-hoc single-attachment framebuffer around the already-resolved `color_` texture (populated by the end-of-pass `igl::StoreAction::MsaaResolve`, see design decision 3) and read from that instead of `framebuffer_` directly |

### Phase C — resources

| ID | Task | State | Notes |
|----|------|-------|-------|
| IGL-16 | `Texture2D` | ✍️ | RGBA8 and the mapped `SurfaceFormat` set, real mip levels, `HasDefinedMipLevel` from what was actually uploaded |
| IGL-17 | `TextureCube` / `Texture3D` | ✍️ | real `igl::TextureType::Cube` / `ThreeD` resources, per-face and per-box uploads |
| IGL-18 | Vertex / index buffers | ✍️ | lazily created, grown on demand, re-upload flushes the pending frame first |
| IGL-19 | Dynamic buffer pool | ✍️ | 3-frame ring for `SpriteBatch`/`DrawUser*`/uniforms |
| IGL-20 | `RenderTarget2D` | 🔶 | colour + optional depth/stencil, real MSAA with an IGL resolve attachment, mip regeneration after each pass, `GetData` via `copyBytesColorAttachment`. Colour+depth path verified by `Igl_RenderTarget`; the real-MSAA path was verified this session by `igl_msaa_test.cpp` (`Igl_Msaa`) -- and found a genuine bug on the way (see IGL-15's note). Mip regeneration remains ✍️ |
| IGL-21 | `RenderTargetCube` | ✍️ | one shared cube image + one shared depth buffer (FNA's own shape), six per-face framebuffers |
| IGL-22 | MRT | 🔶 | 2–4 `RenderTarget2D` slots; a cube face in a multi-target set is refused by name. `igl_mrt_test.cpp` (`Igl_Mrt`) verified the 2-slot case: a single `BasicEffect` draw with 2 `RenderTarget2D`s bound genuinely reaches both simultaneously (not just the first slot), and releasing the set restores the back buffer untouched -- 3/4 slots and the refuse-a-cube-face path remain ✍️ |
| IGL-23 | Back-buffer readback | ✍️ | `ReadBackbuffer` through the swap framebuffer, presentation-rect aware, nearest-sampled |
| IGL-24 | Render-target orientation | ✍️ | design decision 6; `igl_rendertarget_test.cpp` is the discriminating test |

### Phase D — 2D

| ID | Task | State | Notes |
|----|------|-------|-------|
| IGL-25 | `SpriteBatch` geometry | ✍️ | rotation, origin, source rect, `SpriteEffects` flips, layer depth in Z |
| IGL-26 | Batching | ✍️ | flush on texture / sampler / transform / effect change, on `End()`, and per-quad in `SpriteSortMode::Immediate` |
| IGL-27 | Sampler and blend state | ✍️ | per-batch `SamplerState`; `BlendState` through the pipeline's own blend attachment |
| IGL-28 | `ColorMatrixEffect` | ✍️ | CNA's fixed 2D colour-matrix path, in the uber shader |

### Phase E — 3D and stock effects

| ID | Task | State | Notes |
|----|------|-------|-------|
| IGL-29 | Vertex declarations | ✍️ | full `VertexDeclaration` → `igl::VertexInputStateDesc`, plus the four built-in strides for the legacy no-declaration route |
| IGL-30 | Multi-stream input | ✍️ | real: `igl::VertexAttribute::bufferIndex`; `GraphicsCapability::MultiStreamVertexInput` reports true |
| IGL-31 | Instancing | ✍️ | per-instance streams via `VertexSampleFunction::Instance` + `sampleRate` |
| IGL-32 | `BasicEffect` | ✍️ | three directional lights, per-vertex and per-pixel, ambient/emissive/specular, `PreferPerPixelLighting` |
| IGL-33 | `AlphaTestEffect` | ✍️ | XNA's own weight rule, `discard` on a negative weight |
| IGL-34 | `DualTextureEffect` | ✍️ | overlay × 2 |
| IGL-35 | `EnvironmentMapEffect` | ✍️ | cube reflection lerp, Fresnel weighting, env-map specular |
| IGL-36 | `SkinnedEffect` | ✍️ | 72-bone std140 block, `weightsPerVertex` honoured |
| IGL-37 | `PbrEffect` | ✍️ | GGX metallic-roughness over the same three lights; normal/MR/emissive/occlusion maps |
| IGL-38 | Fog | ✍️ | FNA's `fogVector` view-space formulation |
| IGL-39 | Depth / stencil state | ✍️ | full two-sided stencil, standalone `ReferenceStencil` |
| IGL-40 | Rasterizer state | ✍️ | cull, fill, scissor, depth bias / slope-scale bias |
| IGL-41 | Clip-space depth | ✍️ | `z' = 2z − w` only when `getNormalizedZRange() == NegOneToOne` |

### Phase F — custom effects

| ID | Task | State | Notes |
|----|------|-------|-------|
| IGL-42 | `ShaderEffect` compilation | ✍️ | `ShaderStagesCreator::fromModuleStringInput`, version-directive adaptation, real compile errors |
| IGL-43 | Effect parameters | ✍️ | `bindUniform` on OpenGL; explicit refusal on Vulkan (design decision 8) |
| IGL-44 | Effect textures | ✍️ | `SetTexture` for 2D, cube and volume textures |
| IGL-45 | `SpriteBatch.Begin(effect)` | ✍️ | custom effect drives the sprite pipeline |

### Phase G — tests, docs and gates

| ID | Task | State | Notes |
|----|------|-------|-------|
| IGL-46 | Host-portable unit suite | ✍️ | `IglRendererSelectionTests.cpp`: backend selection + generated shader shape |
| IGL-47 | Smoke test | ✍️ | `igl_smoke_test.cpp` |
| IGL-48 | 2D pixel test | ✍️ | `igl_2d_test.cpp` |
| IGL-49 | 3D + depth test | ✍️ | `igl_3d_test.cpp` |
| IGL-50 | Render-target test | ✍️ | `igl_rendertarget_test.cpp` |
| IGL-51 | Capability doc | ✍️ | `docs/igl-renderer.md` |
| IGL-52 | Registry docs | ✍️ | `CLAUDE.md`, `README.md`, `docs/renderer-registry.md` |
| IGL-53 | Feature-matrix row | ⬜ | `docs/graphics-renderer-feature-matrix.md` — deliberately left until the matrix can be filled in from a real run, not from intent |
| IGL-54 | Platform boundary gates | ⬜ | `tools/platform/*.py --check` after the first successful configure |
| IGL-55 | Stock-effect parity tests | 🔶 | the per-effect batteries the LLGL/Magnum families carry (`*_basiceffect_*`, `*_dualtexture_*`, `*_environmentmapeffect_*`, `*_skinnedeffect_*`, `*_mrt_*`, `*_msaa_*`, `*_stencil_*`). Three added and verified this session on the OpenGL/GLX backend: `igl_alphatesteffect_test.cpp` (`Igl_AlphaTestEffect`) proves `AlphaTestEffect`'s `CompareFunction`/`ReferenceAlpha` discard path genuinely runs -- a passing quad (alpha=255 > reference 128) stays visible, a failing one (alpha=0) is discarded and the clear colour shows through; `igl_dualtextureeffect_test.cpp` (`Igl_DualTextureEffect`) proves both texture units and `DiffuseColor` reach the shader (`texture0 * texture2 * 2.0 * diffuse`, with a solid grey `texture2` chosen so its factor cancels the shader's own doubling, matching the same derivation `vulkan_dualtextureeffect_combined_test.cpp` uses); `igl_fog_test.cpp` (`Igl_Fog`) proves `BasicEffect`'s `fogVector` view-space formulation genuinely blends towards the fog colour with distance (three quads at increasing depth shade from nearly-red through a 50/50 blend to nearly-white, matching the exact `saturate(z)`-reduced formula by construction). The fog test's first draft found and fixed its own bug (not a renderer bug): it placed geometry at negative object-space z (-0.1 to -0.9), which is also raw clip-space z since `igl_3d_test.cpp`'s convention keeps `Projection` identity too -- this renderer clips to a `[0, 1]` z range (matching every existing `igl_*_test.cpp`'s positive-depth convention), not OpenGL's traditional `[-1, 1]`, so the whole scene was silently clipped away. Switched to positive z (0.05/0.5/0.95) with `fogStart=0, fogEnd=-1` (reducing the formula to plain `saturate(z)`) once that was understood. `igl_stencil_test.cpp` (`Igl_Stencil`) proves `DepthStencilState`'s stencil test is real, not bypassed: one draw stamps stencil=5 across the left half only, a second full-screen draw is gated on `StencilFunction::Equal`/`ReferenceStencil=5` -- the stamped half turns green (stencil matched) and the unstamped half stays the clear colour (stencil mismatch correctly rejects it, which is what actually proves the gate works rather than every fragment always passing). `igl_mrt_test.cpp` (`Igl_Mrt`) proves the render pass genuinely carries 2 simultaneous colour attachments: one `BasicEffect` draw with 2 `RenderTarget2D`s bound writes to both, and releasing the MRT set leaves the back buffer untouched. `igl_msaa_test.cpp` (`Igl_Msaa`) proves real MSAA resolves correctly into a `RenderTarget2D` -- a diagonal edge is a hard step without MSAA and a genuinely blended mid-tone pixel with it -- and found a real upstream-adjacent readback bug along the way (see IGL-15's note; not a test bug this time, a renderer bug with a real fix). `EnvironmentMapEffect`, `SkinnedEffect` and `PbrEffect` are still ⬜ -- not attempted |

### Phase H — verification and hardening (blocked on the first build)

| ID | Task | State | Notes |
|----|------|-------|-------|
| IGL-56 | First configure | ✅ | `cmake -S . -B cmake-build-igl -DCNA_GRAPHICS_RENDERER=IGL -DCNA_IGL_ROOT=~/deps/igl` with ccache. Found and fixed a real cmake bug: `IGL_WITH_SAMPLES=ON` (used to steer IGL's `IGL_PLATFORM_LINUX_USE_EGL` define to the GLX path) also unconditionally pulled in `add_subdirectory(samples/desktop)` and the glfw/bc7enc/meshoptimizer/tinyobjloader/ktx-software third-party subdirectories CNA deliberately never fetches (design decision 7), so configure failed outright. Fixed by leaving `IGL_WITH_SAMPLES` OFF and instead filtering the stale `IGL_PLATFORM_LINUX_USE_EGL=1` out of `IGLLibrary`'s `COMPILE_DEFINITIONS`/`INTERFACE_COMPILE_DEFINITIONS` properties directly and appending the corrected `=0` (a naive second `target_compile_definitions` append was tried first and silently lost — CMake did not emit it last on the generated command line, so the compiler kept IGL's own `=1`; verified with a standalone repro before settling on the property-filter fix) |
| IGL-57 | First compile of the renderer target | ✅ | `cna_renderer_igl` and all four example binaries build clean. Real bugs found and fixed: (1) `IGL_TEXTURE_SAMPLERS_MAX`/`IGL_VERTEX_ATTRIBUTES_MAX`/`IGL_BUFFER_BINDINGS_MAX`/`IGL_COLOR_ATTACHMENTS_MAX` used unqualified where IGL declares them in `namespace igl` (not as macros) — qualified with `igl::` in `IglRenderer.hpp`/`.cpp`, `IglEffectRenderer.cpp`, `IglDraw.cpp`, `IglPipelineCache.cpp`; (2) `IglSpriteBatchRenderer.cpp` used unqualified `bytecs` inside `CNA::Internal::Renderers::Igl`, which only sees it via `Microsoft::Xna::Framework`'s own `using SharpRuntime::bytecs;` — replaced the four-byte `Color(...)` construction with `Color::White`; (3) the renderer never defined the `CNA::Internal::Renderers::CreateGraphicsRenderer` factory function every other family provides (link error) — added it to `IglRenderer.cpp` under `#ifdef CNA_RENDERER_IGL`, matching `LlglRenderer.cpp`'s shape; (4) `igl_3d_test.cpp` called `VertexPositionColor::VertexDeclaration` (should be `getVertexDeclarationStatic()`), `effect.getCurrentTechniqueProperty().getPassesProperty()` (return is a pointer, needs `->`), and `effect.setVertexColorEnabledProperty(true)` (the real member is the public field `VertexColorEnabled`, matching `llgl_3d_test.cpp`'s usage); (5) `igl_rendertarget_test.cpp` default-constructed a `std::vector<Color>` but `Color` has no default constructor — gave it an explicit fill colour, matching the `std::vector<Color> pixels(N, Color(...))` pattern used elsewhere |
| IGL-58 | First frame | ✅ | `Igl_Smoke` passes 8/8 checks under Xvfb + Mesa llvmpipe (`SDL_VIDEODRIVER=x11`, `CNA_TEST_DISPLAY=:0`). Found and fixed a real bug on the way: `GraphicsDevice::createRenderer()` only populated `args.glContext` for a `#if defined(...)` list of context-backed renderers that omitted `CNA_RENDERER_IGL`, so `IglPlatformSurface.cpp`'s `RequirePlatformGlContext(args.glContext, "IGL")` always saw a null service and threw `PlatformNotSupportedException` before a window ever rendered a frame, regardless of backend selection logic being otherwise correct. Added `CNA_RENDERER_IGL` to that list (harmless for the Vulkan backend, which never reads the field) |
| IGL-59 | Pixel conformance | ✅ | `Igl_2D`, `Igl_3D`, `Igl_RenderTarget` all pass (verified both standalone and via `ctest -R Igl`). `Igl_3D`/`Igl_RenderTarget` passed unmodified; `igl_2d_test.cpp` had a genuine test bug (not a renderer bug) — it drew a non-premultiplied half-alpha tint with `BlendState::AlphaBlend`, which is XNA's *premultiplied* preset (`One`/`InverseSourceAlpha`), so a non-premultiplied source legitimately blends to a different result than the test expected (the IGL renderer's `(127,0,255,255)` was the mathematically correct answer for that blend state). Switched the draw to `BlendState::NonPremultiplied`, matching the same distinction already documented in `llgl_2d_test.cpp` |
| IGL-60 | Vulkan backend run | 🔶 | attempted this session against Mesa lavapipe. Found and fixed a real crash: `IglDynamicBufferPool` sized every chunk (vertex, index, *and* uniform) to a shared 256 KiB `kDynamicChunkBytes`, but Vulkan only guarantees `maxUniformBufferRange >= 65536` and IGL's Vulkan backend asserts a uniform buffer never exceeds the device's actual limit (lavapipe reports exactly the guaranteed minimum) -- `IglResources.cpp` now gives uniform-typed chunks their own 64 KiB `kDynamicUniformChunkBytes` cap. After that fix `Igl_Smoke` passes 8/8 on Vulkan (device bring-up, buffers, texture creation, 60 frames of Clear+Present all real). `Igl_2D`, `Igl_3D` and `Igl_RenderTarget` do not: every pixel readback across all three returns the *last* colour drawn/cleared that frame, uniformly across the whole surface, as if only the final operation's effect reached the image IGL reads back from (independent of which pixel or which test) -- a real rendering-correctness bug, not a test bug this time, and not yet root-caused. It sits in `copyBytesColorAttachment` and/or CNA's render-pass load/store handling being exercised the same way on both backends (the offending code is backend-agnostic in `IglRenderer::BeginPass`/`EndPass`), so the divergence is somewhere in the Vulkan-specific execution/synchronization path this plan has not yet traced through (`IglPlatformSurface.cpp`'s Vulkan device/queue bring-up, or upstream IGL's `VulkanStagingDevice`). Left unresolved rather than guessed at; the OpenGL/GLX backend (IGL-56-59) is the one this plan currently calls verified. **Follow-up investigation (same session):** ran `Igl_2D` with `VK_LAYER_KHRONOS_validation` enabled (`config.enableValidation = true`, `vulkan-validationlayers` installed for this) -- **zero validation errors or warnings**, meaning the Vulkan command stream itself is spec-valid; the bug is not a raw synchronization/barrier misuse the layer can see. Ruled out by reading IGL's own `v1.1.1` source rather than guessing: (1) a "double swap-chain acquire" theory (`IglRenderer::SubmitFrame` calls `AcquireSurfaceTextures()` a second time right before `present()`) -- `VulkanSwapchain::getCurrentVulkanTexture()` guards re-acquisition behind its own `getNextImage_` flag, so a second call within one frame is a no-op and returns the same image; (2) `CommandQueue::submit`'s `endOfFrame`/`present` parameter being silently ignored -- confirmed it is unused in IGL's Vulkan `CommandQueue::submit`, but the submission itself still happens unconditionally either way; (3) `VulkanStagingDevice::getImageData2D` (the actual CPU readback) does insert a `VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT`-sourced barrier and does `immediate_->wait(...)` on its own copy before touching host memory, which looks correct on paper. What was *not* ruled out and is the leading remaining suspect: `Framebuffer::copyBytesColorAttachment` passes `vkTex.getVulkanTexture().image_.imageLayout_` (IGL's own *tracked* last-known layout of the swap-chain image) as the barrier's `oldLayout`, and if CNA's render-pass bookkeeping leaves that tracked value stale relative to the image's real current layout after `EndPass()`, the resulting layout-transition barrier describes a transition that never actually happened -- silent data corruption rather than a validation error is a real possibility on some drivers for exactly this class of bug. Next step for whoever picks this up: instrument or breakpoint `image_.imageLayout_` immediately before each `copyBytesColorAttachment` call across the three failing tests, and compare against what the render pass actually left the image in |
| IGL-61 | Occlusion queries | ⬜ | IGL exposes none at `v1.1.1`; `SupportsCapability(OcclusionQuery)` reports false. Revisit if upstream adds one |
| IGL-62 | Sampler LOD bias | ⬜ | `igl::SamplerStateDesc` has no LOD-bias field; recorded but not applied. Upstream gap, documented |
| IGL-63 | Cube-target MSAA | ⬜ | IGL's `FramebufferDesc` cannot express a multisampled cube attachment with a per-face resolve; reported as 1 |
| IGL-64 | AUDIT/NEXT entries | ⬜ | after IGL-59 |

---

## 4. Known limitations (already honest in code)

| Area | Limitation | Reported as |
|------|------------|-------------|
| Occlusion queries | IGL has no query object on any backend at `v1.1.1` | `SupportsCapability(OcclusionQuery) == false`; the query completes immediately with 0 |
| Back-buffer MSAA on Vulkan | IGL's swap-chain images are single-sample | `GetMultiSampleCount()` returns 0 |
| Swap interval on Vulkan | Present mode is fixed when the swap chain is created | `SetSwapInterval` returns false |
| Wireframe on Vulkan | Needs `fillModeNonSolid`, which IGL does not request | `SupportsCapability(WireFrame) == false` on Vulkan |
| `ShaderEffect` parameters on Vulkan | Loose uniforms do not exist in Vulkan GLSL | the draw throws by name |
| Sampler LOD bias | No field in `igl::SamplerStateDesc` | recorded, not applied |
| Cube render-target MSAA | Not expressible in `igl::FramebufferDesc` | applied count reported as 1 |
| Wayland | Only an X11 native window is wired up | the constructor throws by name |

---

## 5. Files

```text
cmake/ThirdPartyIGL.cmake
cmake/RendererSelection.cmake                      (IGL arm)
modules/core/include/CNA/GraphicsRendererType.hpp  (Igl)
modules/core/include/CNA/GraphicsBackendCategory.hpp
modules/core/include/CNA/GraphicsBackendMaturity.hpp
modules/graphics/src/Xna/GraphicsDevice.cpp        (window render intent)
modules/renderers/igl/
    CMakeLists.txt
    include/CNA/Internal/Renderers/Igl/
        IglPlatformSurface.hpp
        IglRenderer.hpp
        IglRendererSelection.hpp
        IglShaderLibrary.hpp
    src/
        IglConversions.hpp        (family-private XNA-ordinal → IGL-enum translation)
        IglDraw.cpp               (every draw route, uniform fill, sprite submission)
        IglEffectRenderer.cpp     (custom ShaderEffect)
        IglPipelineCache.cpp      (vertex input / shader / pipeline / sampler / depth-stencil caches)
        IglPlatformSurface.cpp    (device bring-up; the only TU with X11/GLX/Vulkan includes)
        IglRenderer.cpp           (lifecycle, frame model, clears, state, capabilities)
        IglResources.cpp          (textures, buffers, targets, dynamic pool, queries)
        IglRendererSelection.cpp
        IglShaderLibrary.cpp      (the generated GLSL)
        IglSpriteBatchRenderer.cpp
    examples/{CMakeLists.txt,igl_smoke_test.cpp,igl_2d_test.cpp,igl_3d_test.cpp,igl_rendertarget_test.cpp,
        igl_alphatesteffect_test.cpp,igl_dualtextureeffect_test.cpp,igl_fog_test.cpp,igl_stencil_test.cpp,
        igl_mrt_test.cpp,igl_msaa_test.cpp}
    tests/CNA/Internal/Renderers/Igl/IglRendererSelectionTests.cpp
docs/igl-renderer.md
plan_igl.md
```

---

## 6. Build

```bash
cmake -S . -B cmake-build-igl \
  -DCNA_GRAPHICS_RENDERER=IGL \
  -DCNA_IGL_ROOT="$HOME/deps/igl" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
cmake --build cmake-build-igl -j3 --target cna_renderer_igl
```

`CNA_IGL_ROOT` points at the reusable `~/deps/igl` checkout; without it the configure fetches the
pinned tag itself. `CNA_IGL_BUILD_BACKEND_VULKAN` defaults to whether `find_package(Vulkan)`
succeeds. Select the backend at run time with `CNA_IGL_BACKEND=opengl|vulkan|auto`.

---

## 7. First build (2026-08-16)

The project owner asked (2026-08-15) for the renderer to be written first and built later, because
the machine was busy compiling other work at the time. On a fresh machine the next day, the first
build ran to completion: configure, compile, `Igl_Smoke`'s first real frame, and all three pixel
conformance tests (`Igl_2D`, `Igl_3D`, `Igl_RenderTarget`) passed on the OpenGL/GLX backend against
Mesa llvmpipe software rendering (no hardware GPU in this environment). Five real bugs turned up on
the way from "written" to "verified" -- see the IGL-56 through IGL-59 rows in §3 for exactly what
they were and how each was fixed; none of them were cosmetic, and all five were things no amount of
re-reading the source against IGL's headers would have caught without an actual compile and an
actual frame. `~/deps/igl` (a fresh clone of `v1.1.1`) and the machine's system OpenGL/Vulkan/X11
dev packages were the only environment setup this required; see §6 for the exact commands.

`IglRendererSelectionTests.cpp` (IGL-46, the host-portable unit suite) needed its own fix once the
full `CnaTests` binary was built for the first time: it wrote `using CNA::...::VertexAttributeSlot;`
(and the same for `TextureUnit`/`UniformBufferBinding`), but all three are namespaces of constants in
`IglShaderLibrary.hpp`, not enums -- a using-*declaration* cannot name a namespace. Replaced with
namespace aliases (`namespace VertexAttributeSlot = CNA::...::VertexAttributeSlot;`) so the existing
`VertexAttributeSlot::Position`-style call sites keep working. All 15 `*Igl*` tests in `CnaTests`
pass now.

The Vulkan backend (IGL-60) was also attempted this session -- see that row in §3 for the crash it
found and fixed (a uniform buffer chunk sized past Vulkan's guaranteed `maxUniformBufferRange`) and
the separate, still-open rendering-correctness bug it did not resolve. Phase C/E's remaining
untested surface (cube/3D textures, MRT, the stock effects beyond `BasicEffect`'s colour-only path,
custom `ShaderEffect`) remains ✍️ written-but-unverified exactly as before -- only the paths the four
example tests actually exercise, on the OpenGL/GLX backend, are now ✅.
