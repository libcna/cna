# plan_igl.md — the IGL (facebook/igl) graphics renderer

CNA's 48th public renderer identity, of the 49 that exist today (it was the 47th when this plan was
written; `TINYGL` landed before it in `scripts/check_renderer_identities.py`'s own order, and
`PIXIJS` after). Like `LLGL`, and unlike every renderer that names a native
graphics API, **`IGL` names a portable abstraction**: `igl::IDevice` fronts real OpenGL/OpenGL ES,
Vulkan and Metal implementations, and CNA picks which one this process uses rather than which one
this build contains.

Upstream: <https://github.com/facebook/igl>, pinned at **`v1.1.1`** (MIT). Local reusable checkout:
`~/deps/igl` (`CNA_IGL_ROOT`).

---

## 1. Status

**Branch:** `feature/igl`. **Audited and repaired on 2026-08-17, Vulkan row order repaired on
2026-08-18** (see IGL-60, IGL-65, IGL-66, IGL-67 and the rewritten IGL-7): the renderer builds, runs
and passes its whole registered suite -- `ctest -R Igl` is **65/65**, comprising 34 host-portable
unit cases in `CnaTests` and 31 example tests against Mesa llvmpipe (OpenGL/GLX) and lavapipe
(Vulkan) under Xvfb.

Run explicitly on **both** backends (all 26 example binaries x 2, not only the ones the ctest
registration forces onto Vulkan), the picture is:

* **OpenGL/GLX: 26 of 26 binaries pass, every check.**
* **Vulkan: 23 of 26.** `Igl_2D` and `Igl_Msaa` now pass here -- both were casualties of the row
  order defects IGL-60/IGL-67 record, which were fixed together on 2026-08-18. The three that
  remain are the custom-`ShaderEffect` tests (`Igl_ShaderEffectTexture3D`, `Igl_Instancing`,
  `Igl_SpriteBatchShaderEffect`), and they fail for a **single, specific and documented** reason
  rather than the vague one this section used to give: each of them sets effect PARAMETERS, which
  design decision 8 refuses on Vulkan by name because loose (non-block) uniforms do not exist in
  Vulkan GLSL. That gap is IGL-43's own, not a defect in the effect path.

  The custom-effect path itself **does** work on Vulkan, and is now verified there:
  `Igl_CustomEffectBackend` compiles, binds and draws with a uniform-free custom `ShaderEffect` on
  both backends and asserts the same pixels from each, checking that both stages really ran. It is
  deliberately uniform-free so that it can tell "the effect path is broken" apart from "parameters
  are not implemented" -- which the three older tests cannot.

  Its two shader sources differ because they must, and that is the finding it pins: SPIR-V requires
  an explicit `layout(location = ...)` on every user input and output, the varyings between stages
  included, and desktop GLSL 4.10 does not; nor do the two backends accept the same `#version`
  (llvmpipe reports GL 4.5, so `#version 460` is unavailable on the OpenGL side). **An application
  targeting both IGL backends has to supply two shader sources.** When it does not, CNA now says so:
  the compile error carries glslang's own line-and-reason diagnostic, captured from IGL's log, plus
  a sentence naming the requirement -- where it previously said only "glslang_shader_parse()
  failed". The process no longer aborts (IGL-68 fixed that); this makes what it reports instead
  actionable.

Earlier history, still accurate: the first build happened on 2026-08-16 (configure, compile, first
frame and the pixel-conformance tests on OpenGL/GLX). The Vulkan backend was brought up in the same
period against Mesa lavapipe, and three real bugs were found there -- a back-buffer BGRA/RGBA
channel swap and a `NormalizedZRange` misreport (both fixed), plus `Igl_2D`'s own failure, which
later sessions narrowed to a *readback* defect rather than a rendering one and 2026-08-18 finally
root-caused and fixed. See IGL-60.

| Phase | What it covers | State |
|-------|----------------|-------|
| A | Identity registration and build integration | ✅ verified on both backends |
| B | Device bring-up and presentation | ✅ verified on both backends. Includes the window contract this phase always claimed and never had until the 2026-08-17 audit: the descriptor's window kind, pre-window GL framebuffer request and GL-context service now all follow the resolved backend (IGL-7, IGL-7b) |
| C | Resources (textures, buffers, targets) | 🔶 `Texture2D`/`RenderTarget2D`/depth/`RenderTarget2D.GetData` (`Igl_RenderTarget`, both backends), full 2- and 4-slot MRT plus the cube-face-in-MRT refusal (`Igl_Mrt`/`Igl_Mrt4`/`Igl_MrtCubeRefuse`, both backends), `TextureCube`/`RenderTargetCube` (`Igl_EnvironmentMapEffect`/`Igl_RenderTargetCube`, both backends) and `Texture3D` (`Igl_ShaderEffectTexture3D`, OpenGL only) verified. The surface-format boundary and every transfer's byte arithmetic are verified on both backends (`Igl_SurfaceFormat`, IGL-65). Open: uploading into a target reads back flipped on Vulkan (IGL-67); back-buffer readback beyond the smoke test's own use remains untested |
| D | 2D pipeline (`SpriteBatch`) | 🔶 verified on OpenGL (`Igl_2D`); on Vulkan the sprites are known to render correctly but the readback cannot confirm it (IGL-60) |
| E | 3D pipeline and stock effects | 🔶 every stock effect (`BasicEffect` through `PbrEffect`, plus fog, stencil, MSAA and instancing) has at least one verified test, and all of them except MSAA and instancing now pass on **both** backends; each is a single scenario, not a combinatorial battery (see IGL-55) |
| F | Custom `ShaderEffect` | 🔶 core compile/uniform/texture path verified on OpenGL/GLX (`Igl_ShaderEffectTexture3D`, `Igl_SpriteBatchShaderEffect`); on Vulkan a desktop-GL-flavoured custom shader aborts inside IGL's own glslang wrapper (see the run summary above), which is a harder limit than this plan previously recorded |
| G | Tests, docs and gates | ✅ 61/61 registered tests pass; the five platform boundary gates and the three renderer-registry gates all run clean (including `sdl_classify.py --check`, which the SDL2 work has since fixed) |
| H | Verification and hardening | 🔶 IGL-56-59, IGL-64, IGL-65 and IGL-66 done; IGL-60, IGL-67 and the three not-implementable rows (IGL-61/62/63) open |

Legend: ✅ done and verified · 🔶 partial · ⬜ not started (or, where the row says so, not
implementable at the pinned IGL version) · ✍️ code written, not yet exercised.

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
| IGL-1 | Add the `IGL` / `Igl` public identity | ✅ | `GraphicsRendererType.hpp` (enum, `getCurrentGraphicsRendererType`, name), `scripts/check_renderer_identities.py`. The count that change made (46 → 47) has since moved twice; the gate now derives it, and IGL is the 48th of 49 identities |
| IGL-2 | Classify the backend | ✍️ | `GraphicsBackendCategory` = `TranslationLayer`, `GraphicsBackendMaturity` = `Experimental` |
| IGL-3 | CMake selection | ✍️ | `cmake/RendererSelection.cmake`: option, STRINGS, explicit-selection chain, `RENDERER_DIR`/`RENDERER_TARGET` mapping |
| IGL-4 | Third-party integration | ✍️ | `cmake/ThirdPartyIGL.cmake`: pinned `v1.1.1`, `CNA_IGL_ROOT` escape hatch, pruned dependency bootstrap, backend options |
| IGL-5 | Module skeleton | ✍️ | `modules/renderers/igl/{CMakeLists.txt,include,src,examples,tests}` |
| IGL-6 | Backend selection | ✍️ | `IglRendererSelection.{hpp,cpp}`, `CNA_IGL_BACKEND` |
| IGL-7 | Window render intent | ✅ | **This was never actually wired up until 2026-08-17, despite being marked ✍️ ("written, not compiled") from the start.** The two helpers this row names (`RendererBackendNeedsOpenGLWindow`/`…VulkanWindow`) existed and were unit-tested from commit `4f67967` onwards, but nothing in production ever called them: that commit does not touch `GraphicsDevice.cpp` at all (`git show --stat 4f67967`), and the later runtime-renderer port (`645e184`) wrote `.windowKind = RendererWindowKind::OpenGL` into `IglRendererDescriptor.cpp` as a constant. So `CNA_IGL_BACKEND=vulkan` built a Vulkan device on a window created with an OpenGL render intent, and `AreWindowKindsCompatible` compared a kind the window did not have — which is what decides whether a fallback candidate may reuse it (plan_runtimerenderer.md design decision 8). Now `IglRendererDescriptor.cpp` derives `windowKind`, `glFramebuffer` and `needsGlContext` from `Detail::ResolveRendererBackendForWindow()`, the same cached answer `IglPlatformSurface` builds the device from — the shape `BgfxRendererDescriptor.cpp` already uses for the same reason (RTR-P10-9). The pre-window resolution is a non-throwing wrapper because the generated registry publishes the compiled-in set from a static initializer, where a bad `CNA_IGL_BACKEND` would otherwise terminate the process instead of failing by name at device creation. Covered by `IglRendererSelectionTests.cpp`'s six new window/framebuffer cases, including one that reads the registered descriptor back and compares it against the resolved backend |
| IGL-7b | Pre-window OpenGL framebuffer request | ✅ | The same regression's other half: the descriptor carried no `glFramebuffer` at all, so the window was created with the platform's default visual and IGL's own 24/8/double-buffer request (`IglPlatformSurface.cpp`'s `RequestedGlContext`) arrived after GLX had already fixed the visual — the exact failure mode `GraphicsRendererDescriptor::glFramebuffer`'s own comment describes (a 0-bit stencil buffer makes every `DepthStencilState::StencilEnable` a silent no-op). The descriptor now states `{24, 8, doubleBuffered, wantsMultiSample}` for the OpenGL backend and an all-zero request for Vulkan, and `RequestedGlContext` reads those same values rather than restating them, so the two statements about one window cannot drift |
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
| IGL-16 | `Texture2D` | 🔶 | RGBA8 and the mapped `SurfaceFormat` set, real mip levels, `HasDefinedMipLevel` from what was actually uploaded. The `SurfaceFormat` half was rebuilt on 2026-08-17 (see IGL-65); `Color` is verified by every pixel test in this plan and the other supported formats by `Igl_SurfaceFormat`'s contract-level round trips, but no *public* `Texture2D` is anything but `Color` under this renderer, because `ClassifySurfaceFormatEXT` deliberately defers rather than promoting (IGL-65) |
| IGL-17 | `TextureCube` / `Texture3D` | 🔶 | real `igl::TextureType::Cube` / `ThreeD` resources, per-face and per-box uploads. `TextureCube` construction, per-face `SetData` and sampling verified by `Igl_EnvironmentMapEffect`; `Texture3D` construction and per-box `SetData` now verified by `Igl_ShaderEffectTexture3D` (sampled through a real custom shader and read back via the back buffer, since `IglTexture3DRenderer::GetData()` intentionally has no readback path of its own -- see `IglResources.cpp`) |
| IGL-18 | Vertex / index buffers | ✍️ | lazily created, grown on demand, re-upload flushes the pending frame first |
| IGL-19 | Dynamic buffer pool | ✍️ | 3-frame ring for `SpriteBatch`/`DrawUser*`/uniforms |
| IGL-20 | `RenderTarget2D` | 🔶 | colour + optional depth/stencil, real MSAA with an IGL resolve attachment, mip regeneration after each pass, `GetData` via `copyBytesColorAttachment`. Colour+depth path verified by `Igl_RenderTarget`; the real-MSAA path was verified this session by `igl_msaa_test.cpp` (`Igl_Msaa`) -- and found a genuine bug on the way (see IGL-15's note). Mip regeneration remains ✍️ |
| IGL-21 | `RenderTargetCube` | 🔶 | one shared cube image + one shared depth buffer (FNA's own shape), six per-face framebuffers. Verified this session (`Igl_RenderTargetCube`) and found a real bug on the way: `IglRenderTargetCubeRenderer::GetData()` built its `igl::TextureRangeDesc` with the plain `new2D(x, y, w, h)` constructor, which carries no face index -- unlike `SetData()`, which already used the face-aware `newCubeFace(...)`. Since every face's framebuffer attaches the SAME shared cube colour image (there is nothing per-framebuffer to distinguish which face a read means), every `GetData()` call silently read face 0 (PositiveX) regardless of which face was actually requested -- a real test with two differently-cleared faces (magenta on PositiveX, cyan on PositiveY) read PositiveX's colour back for BOTH faces. Fixed by switching to `newCubeFace(x, y, w, h, face)`, matching `SetData()`'s own already-correct pattern |
| IGL-22 | MRT | ✅ | 2–4 `RenderTarget2D` slots; a cube face in a multi-target set is refused by name. `igl_mrt_test.cpp` (`Igl_Mrt`) verified the 2-slot case: a single `BasicEffect` draw with 2 `RenderTarget2D`s bound genuinely reaches both simultaneously (not just the first slot), and releasing the set restores the back buffer untouched. The full 4-slot count (`igl::IGL_COLOR_ATTACHMENTS_MAX`) is also verified (`igl_mrt4_test.cpp`, `Igl_Mrt4`): all 4 simultaneously bound `RenderTarget2D`s receive the same draw's colour, and releasing the set again restores the back buffer -- rules out an off-by-one in the attachment-count plumbing silently dropping a slot beyond 2. The refuse-a-cube-face-in-a-multi-target-set path is now verified too (`igl_mrt_cube_refuse_test.cpp`, `Igl_MrtCubeRefuse`): mixing a `RenderTargetBinding(cube, face)` into a multi-target set throws (`IglRenderer.cpp`'s own by-name refusal, "a RenderTargetCube face cannot take part in a multi-render-target bind on this renderer"), and the device keeps rendering normally afterward -- the exception does not leave it corrupted |
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
| IGL-29 | Vertex declarations | 🔶 | full `VertexDeclaration` → `igl::VertexInputStateDesc`, plus the four built-in strides for the legacy no-declaration route. Verified this session via `VertexPositionTexture`/`VertexPositionNormalTexture`/`VertexPositionNormalTextureSkinned`/`VertexPositionNormalTangentTexture` declarations across the stock-effect tests. Found and fixed a real, non-IGL-specific bug: `modules/graphics/src/Xna/VertexBuffer.cpp`'s `SetData(const VertexPositionNormalTangentTexture*, ...)` and the `...TextureSkinned` sibling called `renderer_->SetData(...)` directly instead of going through the shared `UploadValidatedData()` helper every other typed `SetData` overload uses, so `SetVertexDeclaration()` was never called for either type -- IGL's own `IglVertexBufferRenderer::HasDeclaration()` stayed false and the draw fell through to the legacy 4-built-in-stride route, which does not recognise the PBR types' 48/68-byte strides and throws by name. Fixed both overloads to route through `UploadValidatedData()`, matching every other vertex type's own pattern. **Regression check:** a full, unfiltered `ctest --test-dir cmake-build-igl` (not just the `Igl` label) surfaced several failures; every one was traced to a cause unrelated to this fix rather than assumed clean: `ModuleLinkClosure_probe_*` fails only under the Ninja generator this session's builds used (`SKIP: link.txt not found (non-Makefiles generator?)`, a Makefiles-only test script); `AlphaTestReferenceScalingTest`/`CnaInputTests`/`GameEventSemanticsGoldenTest` failed only because that particular unfiltered invocation didn't export `SDL_VIDEODRIVER=x11 DISPLAY=:0` (confirmed passing once set); `StrictXnaApiSurfaceCheck_Compile_Run`'s SEGFAULT is a pre-existing static-destructor crash in `Microsoft::Devices::Detail::PlatformVibrateBackend` at process exit (confirmed via `gdb -batch -ex run -ex bt`), nothing to do with graphics; `CnaPlatformWindowTests`' one failing sub-test is SDL's `dummy` video driver being unable to create an OpenGL-capable window, which any OpenGL-requiring renderer (not IGL-specific) hits identically under that driver. None reproduce a change in behaviour caused by this fix |
| IGL-30 | Multi-stream input | 🔶 | real: `igl::VertexAttribute::bufferIndex`; `GraphicsCapability::MultiStreamVertexInput` reports true. Two simultaneous streams (a per-vertex quad plus a per-instance offset stream) verified this session via `Igl_Instancing`; a 3-or-more-stream draw remains ✍️ |
| IGL-31 | Instancing | 🔶 | per-instance streams via `VertexSampleFunction::Instance` + `sampleRate`. Verified this session (`igl_instancing_test.cpp`, `Igl_Instancing`): a custom `ShaderEffect` reads a genuine per-instance vertex stream (a `TexCoord1`-slot NDC offset, instance-rate-sampled) and 3 `DrawInstancedPrimitives` instances render at 3 correctly-separated, correctly-coloured on-screen positions, with the gap beside the leftmost instance confirmed to stay the clear colour (ruling out "every instance drew at the same, unoffset position" as a false pass). **Known architectural limit, documented rather than worked around:** IGL's vertex attribute locations are assigned from a fixed usage→slot table (one location per `VertexElementUsage`, `IglConversions.hpp`'s `ToVertexAttributeSlot`), so the classic D3D9 hardware-instancing shape used by `easygl_instancedmodel_shader_test.cpp` -- a per-instance 4x4 matrix packed as 4 separate `BlendWeight0..3` elements needing 4 *distinct* attribute locations -- cannot be expressed here: a second stream re-declaring an already-claimed usage is silently skipped (`IglDraw.cpp`'s `if ((attributeMask & VertexAttributeBit(slot)) != 0) continue;`), not given its own location. This test's otherwise-unused-slot (`aTexCoord1`) offset design sidesteps that limit rather than fixing it; a real per-instance *transform matrix* through a custom `ShaderEffect` is not currently possible on this renderer without an architecture change (assigning custom-effect attribute locations by declaration order instead of by fixed usage slot, matching how sampler names were already made effect-scoped for IGL-42..45) |
| IGL-32 | `BasicEffect` | ✍️ | three directional lights, per-vertex and per-pixel, ambient/emissive/specular, `PreferPerPixelLighting` |
| IGL-33 | `AlphaTestEffect` | ✍️ | XNA's own weight rule, `discard` on a negative weight |
| IGL-34 | `DualTextureEffect` | ✍️ | overlay × 2 |
| IGL-35 | `EnvironmentMapEffect` | 🔶 | cube reflection lerp, Fresnel weighting, env-map specular. Verified this session (`Igl_EnvironmentMapEffect`): `EnvironmentMapAmount=1` genuinely replaces the surface colour with the sampled cubemap, proving `mix(baseColor, envColor, amount)` and real `TextureCube` sampling both work. Fresnel weighting is now also verified (`igl_environmentmapeffect_fresnel_test.cpp`, `Igl_EnvironmentMapEffectFresnel`, porting `bgfx_environmentmapeffect_fresnel_test.cpp`/`easygl_environmentmapeffect_fresnel_test.cpp`'s Task 396 derivation, cross-checked against the real XNA `EnvironmentMapEffect.fx` HLSL vendored at `modules/renderers/directx9/src/shaders/xna/EnvironmentMapEffect.fx`): a grazing-angle camera reduces the Fresnel-weighted blend to the flat cube colour (128,128,128), a head-on camera fully suppresses the env map down to `baseColor = texture * (EmissiveColor, no active lights)` = (100,50,25) -- both pass, confirming `IglShaderLibrary.cpp`'s `amount *= pow(max(1-abs(dot(eye,normal)),0), uAmbientColor.w)` term and the `ambientColor.w`/`specularColor.w` FresnelFactor/EnvironmentMapAmount packing (`IglDraw.cpp`'s `FillEffectUniforms`) both work correctly on real hardware. **Test-design pitfall found and fixed while writing this test** (not a renderer bug): `EyePosition` is `Invert(View).Translation` (`EnvironmentMapEffect.cpp`), the world origin for an identity View; the "grazing" case's quad must be genuinely coplanar with that origin (z=0) for `eyeVector` to have no Z component the way the derivation assumes -- an earlier draft placed the quad at z=0.5 (this test family's usual margin against clipping under an identity projection, e.g. `igl_fog_test.cpp`), which put the quad nearly head-on to the origin-camera instead of coplanar with it, and the grazing check failed with the head-on answer until the quad was moved back to z=0. Env-map specular itself remains ✍️ |
| IGL-36 | `SkinnedEffect` | 🔶 | 72-bone std140 block, `weightsPerVertex` honoured. Identity-palette skinning (weight 1.0 on bone 0, the default 72 identity matrices) verified this session (`Igl_SkinnedEffect`) -- proves the bone/weight/index vertex attributes and the bone uniform upload all reach the shader and produce a correctly-positioned, lit mesh. A real, non-identity bone transform is now also verified (`igl_skinnedeffect_translation_bone_test.cpp`, `Igl_SkinnedEffectTranslationBone`, matching `bgfx_skinnedeffect_translation_bone_test.cpp`'s own derivation): bone 0 set to `Matrix::CreateTranslation(0.5, 0, 0)` shifts a left-half-authored quad to render centred on screen, not at its authored position -- proving `cnaBones.uBones[0]` genuinely transforms the vertex rather than being ignored or applied as identity regardless of its uploaded value. `weightsPerVertex` > 1 (multi-bone blending) remains ✍️ |
| IGL-37 | `PbrEffect` | 🔶 | GGX metallic-roughness over the same three lights; normal/MR/emissive/occlusion maps. The core BRDF verified this session (`Igl_PbrEffect`) against an independently hand-derived analytic value (91,91,91) at the one pixel where every BRDF dot product is exactly 1 -- see `vulkan_pbreffect_handderived_test.cpp` for the full derivation this test's scene and expected value are taken from. Found and fixed a real, renderer-agnostic bug on the way (see IGL-29's note): `VertexBuffer::SetData(const VertexPositionNormalTangentTexture*, ...)` never propagated the vertex declaration, so IGL (the first renderer this session to actually exercise that specific typed overload rather than the raw-stride `SetDataRaw` route every other family's own PBR test used) threw "neither a VertexDeclaration nor a stride this renderer recognises". `NormalMap`, `EmissiveFactor` and `OcclusionMap` are now also verified (`igl_pbreffect_maps_test.cpp`, `Igl_PbrEffectMaps`), porting `opengl2_pbreffect_test.cpp`'s differential-comparison checks B/C/D/E (compare two renders that should/shouldn't differ, rather than deriving an exact expected colour for a perturbed tangent-space normal): a 90-degree-tilted `NormalMap` under an off-axis light changes the lit result vs. a flat-normal baseline; `EmissiveFactor` alone (no lights, no ambient) still lights the surface; a black `OcclusionMap` darkens an ambient-only render below the no-map baseline; null `Texture`/`NormalMap`/`OcclusionMap` fall back to white/flat-normal defaults without crashing. All 5 checks passed on the first run, no bugs found. `MetallicRoughnessMap` (a texture-driven metallic/roughness value, as opposed to the flat `MetallicFactor`/`RoughnessFactor` floats both this test and the BRDF test use) and non-trivial lighting angles beyond the BRDF test's single analytically-tractable point remain ✍️ |
| IGL-38 | Fog | ✍️ | FNA's `fogVector` view-space formulation |
| IGL-39 | Depth / stencil state | ✍️ | full two-sided stencil, standalone `ReferenceStencil` |
| IGL-40 | Rasterizer state | ✍️ | cull, fill, scissor, depth bias / slope-scale bias |
| IGL-41 | Clip-space depth | ✍️ | `z' = 2z − w` only when `getNormalizedZRange() == NegOneToOne` |

### Phase F — custom effects

| ID | Task | State | Notes |
|----|------|-------|-------|
| IGL-42 | `ShaderEffect` compilation | ✅ | `ShaderStagesCreator::fromModuleStringInput`, version-directive adaptation, real compile errors. Verified on OpenGL/GLX by `igl_shadereffect_texture3d_test.cpp` (`Igl_ShaderEffectTexture3D`) and, since 2026-08-18, on **Vulkan** by `igl_custom_effect_backend_test.cpp` (`Igl_CustomEffectBackend_Vulkan`): a custom effect compiles, binds and draws there, with both stages proved to have run. That test is deliberately uniform-free, because effect PARAMETERS are a separate Vulkan gap (IGL-43) and the three older custom-effect tests cannot separate the two. **Authoring requirement, now reported rather than merely true:** SPIR-V needs an explicit `layout(location = N)` on every user input and output including the varyings, and `layout(set = N, binding = N)` on samplers; a shader written for the OpenGL backend generally needs its own Vulkan variant. A failure now carries glslang's own line-and-reason text, captured from IGL's log, plus that requirement in words -- previously the whole message was "glslang_shader_parse() failed" |
| IGL-43 | Effect parameters | 🔶 | `bindUniform` on OpenGL; explicit refusal on Vulkan (design decision 8), because loose non-block uniforms do not exist in Vulkan GLSL and IGL's Vulkan encoder leaves `bindUniform` unimplemented. The `Int`/`Float3` uniforms `Igl_ShaderEffectTexture3D` sets (`VolumeSampler`, `coord`) are verified reaching the shader on OpenGL; float/vec2/vec4/mat4/array variants remain ✍️. **This is the only reason the three custom-effect tests fail on Vulkan** -- the effect path itself is verified there (IGL-42). Closing it means packing a custom effect's parameters into a uniform buffer whose std140 layout CNA defines and the author's shader declares, which is a change to the CNAEXT `ShaderEffect` contract, not a bug fix; not attempted |
| IGL-44 | Effect textures | 🔶 | `SetTexture` for 2D, cube and volume textures. Found and fixed a real, previously-undiscovered architectural bug this session (see below) that left every custom-effect texture unit unbindable; `Texture3D`'s path is now verified end-to-end via `Igl_ShaderEffectTexture3D`. 2D/cube custom-effect textures share the same fixed code path so the fix covers them too, but neither has its own dedicated custom-effect test yet |
| IGL-45 | `SpriteBatch.Begin(effect)` | 🔶 | custom effect drives the sprite pipeline. Now verified end-to-end (`igl_spritebatch_shadereffect_test.cpp`, `Igl_SpriteBatchShaderEffect`): a custom `ShaderEffect` bound via `SpriteBatch.Begin(sortMode, blend, sampler, depthStencil, rasterizer, effect)` tints a white sprite green through its own `tint` uniform (not XNA's per-sprite colour parameter, left white so a pass proves the CUSTOM shader ran, not the ordinary vertex-colour path every other 2D test covers) -- both checks pass. This is a genuinely different call site from the 3D path `igl_shadereffect_texture3d_test.cpp` exercises (`IglRenderer::DrawSpriteBatchEXT`'s own `PipelineKey`/`AcquirePipeline`/`ApplyCustomEffectUniforms` calls), and confirms the IGL-42..45 sampler-map fix reaches it too: the sprite's own texture is bound by the *shared* `BindEffectResources()` path (not the custom effect's `SetTexture()` -- SpriteBatch owns which texture a sprite draws with), so the effect still had to declare `SetUniformInt("SpriteTexture", 0)` for that unit-0 bind to resolve under the custom pipeline's now-effect-scoped sampler map, exactly like the 3D path's own convention |

**IGL-42/43/44 bug, found and fixed this session:** the first test to exercise a custom `ShaderEffect`
with its own texture sampler (`igl_shadereffect_texture3d_test.cpp`, written to also be the only way
to verify `Texture3D` sampling -- `IglTexture3DRenderer::GetData()` intentionally refuses readback, so
sampling one through a real shader and reading the rendered pixel is the only proof route) rendered
solid black instead of the expected red/blue volume slices, with `Sampler uniform (uTexture0/uEnvMap/
...) not found in shader` and `Unable to find sampler location` logged. Root cause: `AcquirePipeline()`
(`IglPipelineCache.cpp`) unconditionally built `igl::RenderPipelineDesc::fragmentUnitSamplerMap` from
the **built-in uber-shader's fixed sampler names** (`GetSamplerUniformName(unit)` → `uTexture0`,
`uEnvMap`, ...) for every pipeline, including ones compiled from a custom `ShaderEffect`'s own GLSL
(which declares whatever sampler names the game wrote, e.g. `VolumeSampler`). IGL's OpenGL backend
resolves samplers by name at pipeline-creation time (`RenderPipelineState::compilePipeline`, upstream
`igl/opengl/RenderPipelineState.cpp`) and caches the result in `unitSamplerLocationMap_`; a name that
does not exist in the custom shader resolves to no location, so every later `encoder.bindTexture(unit,
...)` call for that unit fails inside `bindTextureUnit()` (`Result::Code::RuntimeError, "Unable to find
sampler location"`) and the texture is never actually bound anywhere the shader can read it -- silently,
since `ApplyCustomEffectUniforms()` in `IglDraw.cpp` does not check the encoder's bind result.

Fix: `AcquirePipeline()` gained an optional `const IglEffectRenderer* customEffect` parameter (default
`nullptr`, so every other call site is unaffected). When non-null, `fragmentUnitSamplerMap` is built
from that effect's own recorded uniforms instead of the built-in names: every `Int`-typed uniform is
read back (`IglEffectRenderer::UniformValue::data` stores an int's bytes bit-exact, decoded here via
`std::memcpy`) as `{samplerName: unit}`, matching the pre-existing, EasyGL-shared convention that a
custom shader declares which sampler occupies which texture unit through `SetUniformInt(samplerName,
unit)` -- the same call a game already makes to select a unit in GLSL's own binding model. Both
`AcquirePipeline()` call sites in `IglDraw.cpp` (the general 3D draw path and the `SpriteBatch`
custom-effect path) now pass their already-in-scope `IglEffectRenderer*` through. Cache correctness is
preserved without changes to `PipelineKey`: a custom effect's `programId` (unique per compiled program)
is already part of the key, and the built-in shader always uses `programId == 0`, so a custom-effect
pipeline and a built-in one can never collide in the cache regardless of which sampler map either was
built with.

**Known residual, not a regression:** `BindEffectResources()` unconditionally tries to bind a dummy
texture to all 7 built-in texture units (`Texture0`, `Texture1`, `EnvironmentMap`, `NormalMap`,
`MetallicRoughnessMap`, `EmissiveMap`, `OcclusionMap`) on every draw, including custom-effect draws
whose pipeline's sampler map now only contains the units the effect itself declared. The unused units'
`bindTexture` calls fail the same "Unable to find sampler location" way and are silently skipped
(`IGL_LOG_INFO_ONCE`-deduplicated to a single log line per process, confirmed harmless: `coord`/
`VolumeSampler` — the only slot the test's shader actually reads — bind and sample correctly, and both
`Igl_ShaderEffectTexture3D` checks pass). Not fixed, since doing so would mean `BindEffectResources`
tracking which units a bound custom effect's own pipeline actually maps, which is more invasive than
this task's scope; documented here rather than silently left unexplained.

### Phase G — tests, docs and gates

| ID | Task | State | Notes |
|----|------|-------|-------|
| IGL-46 | Host-portable unit suite | ✅ | `IglRendererSelectionTests.cpp` (backend selection, window kind, the pre-window framebuffer request and the registered descriptor's agreement with the resolved backend) and `IglSurfaceFormatTests.cpp` (the format boundary and the transfer byte arithmetic). Both now guard on `CNA_RENDERER_IGL || CNA_RENDERER_PRESENT_IGL`, matching RTR-P9-9 -- with only the identity macro they compiled to nothing in a multi-renderer build where IGL is present but not the default |
| IGL-47 | Smoke test | ✅ | `igl_smoke_test.cpp`; verified on both backends (IGL-58, IGL-60) |
| IGL-48 | 2D pixel test | 🔶 | `igl_2d_test.cpp`; passes on OpenGL/GLX (IGL-59), still failing on Vulkan for the readback reason IGL-60 records |
| IGL-49 | 3D + depth test | ✅ | `igl_3d_test.cpp`; verified on both backends (`Igl_3D`, `Igl_3D_Vulkan`) |
| IGL-50 | Render-target test | ✅ | `igl_rendertarget_test.cpp`; verified on both backends (`Igl_RenderTarget`, `Igl_RenderTarget_Vulkan`) |
| IGL-51 | Capability doc | ✅ | `docs/igl-renderer.md`, rewritten on 2026-08-17: its "not yet compiled" status banner had survived three sessions of real builds, and it now carries the surface-format boundary IGL-65 established |
| IGL-52 | Registry docs | ✅ | `CLAUDE.md`, `README.md`, `docs/renderer-registry.md` |
| IGL-53 | Feature-matrix row | ⬜ | `docs/graphics-renderer-feature-matrix.md` — deliberately left until the matrix can be filled in from a real run, not from intent. This session produced that real run (IGL-56-59, IGL-55), but the matrix's own stated bar is "broad enough for a meaningful row-by-row comparison" against the established DX9-12 columns, and it explicitly excludes LLGL and WebGPU for having a narrower verified surface than that -- IGL's own verified surface (fully verified on OpenGL/GLX; on Vulkan, 20 of 24 example binaries pass, with IGL-60 and three Vulkan-only limits recorded in §1 and §4) is narrower still. Still ⬜ as of the 2026-08-17 audit, and deliberately: the audit widened what is *verified* on Vulkan considerably, but a matrix row would have to describe both backends as one column, which this renderer cannot honestly do while a custom `ShaderEffect` aborts the process on one of them. Revisit once IGL-60 and the Vulkan custom-effect limit close, matching the same bar LLGL/WebGPU are held to |
| IGL-54 | Platform boundary gates | ✅ | `tools/platform/*.py --check`. All five now pass clean, `sdl_classify.py --check` included -- it used to fail on 22 unclassified `SDL_*` identifiers unrelated to this renderer, and the SDL2 platform work has since classified them (985 identifiers across 16 areas). The three renderer-registry gates (`check_renderer_identities.py`, `check_runtime_renderer_discipline.py`, `check_renderer_target_discipline.py`) pass too, which is what confirms the reworked descriptor still owns exactly one descriptor unit and one family-namespaced factory |
| IGL-55 | Stock-effect parity tests | 🔶 | the per-effect batteries the LLGL/Magnum families carry (`*_basiceffect_*`, `*_dualtexture_*`, `*_environmentmapeffect_*`, `*_skinnedeffect_*`, `*_mrt_*`, `*_msaa_*`, `*_stencil_*`). Three added and verified this session on the OpenGL/GLX backend: `igl_alphatesteffect_test.cpp` (`Igl_AlphaTestEffect`) proves `AlphaTestEffect`'s `CompareFunction`/`ReferenceAlpha` discard path genuinely runs -- a passing quad (alpha=255 > reference 128) stays visible, a failing one (alpha=0) is discarded and the clear colour shows through; `igl_dualtextureeffect_test.cpp` (`Igl_DualTextureEffect`) proves both texture units and `DiffuseColor` reach the shader (`texture0 * texture2 * 2.0 * diffuse`, with a solid grey `texture2` chosen so its factor cancels the shader's own doubling, matching the same derivation `vulkan_dualtextureeffect_combined_test.cpp` uses); `igl_fog_test.cpp` (`Igl_Fog`) proves `BasicEffect`'s `fogVector` view-space formulation genuinely blends towards the fog colour with distance (three quads at increasing depth shade from nearly-red through a 50/50 blend to nearly-white, matching the exact `saturate(z)`-reduced formula by construction). The fog test's first draft found and fixed its own bug (not a renderer bug): it placed geometry at negative object-space z (-0.1 to -0.9), which is also raw clip-space z since `igl_3d_test.cpp`'s convention keeps `Projection` identity too -- this renderer clips to a `[0, 1]` z range (matching every existing `igl_*_test.cpp`'s positive-depth convention), not OpenGL's traditional `[-1, 1]`, so the whole scene was silently clipped away. Switched to positive z (0.05/0.5/0.95) with `fogStart=0, fogEnd=-1` (reducing the formula to plain `saturate(z)`) once that was understood. `igl_stencil_test.cpp` (`Igl_Stencil`) proves `DepthStencilState`'s stencil test is real, not bypassed: one draw stamps stencil=5 across the left half only, a second full-screen draw is gated on `StencilFunction::Equal`/`ReferenceStencil=5` -- the stamped half turns green (stencil matched) and the unstamped half stays the clear colour (stencil mismatch correctly rejects it, which is what actually proves the gate works rather than every fragment always passing). `igl_mrt_test.cpp` (`Igl_Mrt`) proves the render pass genuinely carries 2 simultaneous colour attachments: one `BasicEffect` draw with 2 `RenderTarget2D`s bound writes to both, and releasing the MRT set leaves the back buffer untouched. `igl_msaa_test.cpp` (`Igl_Msaa`) proves real MSAA resolves correctly into a `RenderTarget2D` -- a diagonal edge is a hard step without MSAA and a genuinely blended mid-tone pixel with it -- and found a real upstream-adjacent readback bug along the way (see IGL-15's note; not a test bug this time, a renderer bug with a real fix). `igl_environmentmapeffect_test.cpp` (`Igl_EnvironmentMapEffect`) proves `EnvironmentMapAmount=1` genuinely replaces the surface colour with a real `TextureCube` sample. `igl_skinnedeffect_test.cpp` (`Igl_SkinnedEffect`) proves identity-palette bone skinning (the 72-bone std140 uniform block, per-vertex `BlendWeight`/`BlendIndices` attributes) reaches the shader and renders correctly. `igl_pbreffect_test.cpp` (`Igl_PbrEffect`) proves the core GGX/Smith/Fresnel BRDF matches an independently hand-derived analytic value -- and found a real, renderer-agnostic bug in the shared `VertexBuffer::SetData` overload for PBR vertex types along the way (see IGL-29's note). Every stock effect this plan tracks (`BasicEffect` through `PbrEffect`) now has at least one verified pixel-conformance test. **Milestone: all 7 buckets from this row's original list (`*_basiceffect_*` through `*_stencil_*`) now have at least a first-cut test** -- still 🔶 rather than ✅ because each is a single scenario, not the LLGL/Magnum families' own much larger combinatorial batteries (compare-function sweeps, fresnel/specular variants, multi-light configurations, MRT beyond 2 slots, MSAA beyond one sample count). The 2026-08-17 audit re-ran every one of these on the **Vulkan** backend for the first time, which is where the two new Vulkan-only findings in §1 come from: all of them pass there except `Igl_Msaa` (3/4) and the three custom-`ShaderEffect` tests (which abort inside IGL's glslang wrapper) |

### Phase H — verification and hardening (blocked on the first build)

| ID | Task | State | Notes |
|----|------|-------|-------|
| IGL-56 | First configure | ✅ | `cmake -S . -B cmake-build-igl -DCNA_GRAPHICS_RENDERER=IGL -DCNA_IGL_ROOT=~/deps/igl` with ccache. Found and fixed a real cmake bug: `IGL_WITH_SAMPLES=ON` (used to steer IGL's `IGL_PLATFORM_LINUX_USE_EGL` define to the GLX path) also unconditionally pulled in `add_subdirectory(samples/desktop)` and the glfw/bc7enc/meshoptimizer/tinyobjloader/ktx-software third-party subdirectories CNA deliberately never fetches (design decision 7), so configure failed outright. Fixed by leaving `IGL_WITH_SAMPLES` OFF and instead filtering the stale `IGL_PLATFORM_LINUX_USE_EGL=1` out of `IGLLibrary`'s `COMPILE_DEFINITIONS`/`INTERFACE_COMPILE_DEFINITIONS` properties directly and appending the corrected `=0` (a naive second `target_compile_definitions` append was tried first and silently lost — CMake did not emit it last on the generated command line, so the compiler kept IGL's own `=1`; verified with a standalone repro before settling on the property-filter fix) |
| IGL-57 | First compile of the renderer target | ✅ | `cna_renderer_igl` and all four example binaries build clean. Real bugs found and fixed: (1) `IGL_TEXTURE_SAMPLERS_MAX`/`IGL_VERTEX_ATTRIBUTES_MAX`/`IGL_BUFFER_BINDINGS_MAX`/`IGL_COLOR_ATTACHMENTS_MAX` used unqualified where IGL declares them in `namespace igl` (not as macros) — qualified with `igl::` in `IglRenderer.hpp`/`.cpp`, `IglEffectRenderer.cpp`, `IglDraw.cpp`, `IglPipelineCache.cpp`; (2) `IglSpriteBatchRenderer.cpp` used unqualified `bytecs` inside `CNA::Internal::Renderers::Igl`, which only sees it via `Microsoft::Xna::Framework`'s own `using SharpRuntime::bytecs;` — replaced the four-byte `Color(...)` construction with `Color::White`; (3) the renderer never defined the `CNA::Internal::Renderers::CreateGraphicsRenderer` factory function every other family provides (link error) — added it to `IglRenderer.cpp` under `#ifdef CNA_RENDERER_IGL`, matching `LlglRenderer.cpp`'s shape; (4) `igl_3d_test.cpp` called `VertexPositionColor::VertexDeclaration` (should be `getVertexDeclarationStatic()`), `effect.getCurrentTechniqueProperty().getPassesProperty()` (return is a pointer, needs `->`), and `effect.setVertexColorEnabledProperty(true)` (the real member is the public field `VertexColorEnabled`, matching `llgl_3d_test.cpp`'s usage); (5) `igl_rendertarget_test.cpp` default-constructed a `std::vector<Color>` but `Color` has no default constructor — gave it an explicit fill colour, matching the `std::vector<Color> pixels(N, Color(...))` pattern used elsewhere |
| IGL-58 | First frame | ✅ | `Igl_Smoke` passes 8/8 checks under Xvfb + Mesa llvmpipe (`SDL_VIDEODRIVER=x11`, `CNA_TEST_DISPLAY=:0`). Found and fixed a real bug on the way: `GraphicsDevice::createRenderer()` only populated `args.glContext` for a `#if defined(...)` list of context-backed renderers that omitted `CNA_RENDERER_IGL`, so `IglPlatformSurface.cpp`'s `RequirePlatformGlContext(args.glContext, "IGL")` always saw a null service and threw `PlatformNotSupportedException` before a window ever rendered a frame, regardless of backend selection logic being otherwise correct. Added `CNA_RENDERER_IGL` to that list (harmless for the Vulkan backend, which never reads the field) |
| IGL-59 | Pixel conformance | ✅ | `Igl_2D`, `Igl_3D`, `Igl_RenderTarget` all pass (verified both standalone and via `ctest -R Igl`). `Igl_3D`/`Igl_RenderTarget` passed unmodified; `igl_2d_test.cpp` had a genuine test bug (not a renderer bug) — it drew a non-premultiplied half-alpha tint with `BlendState::AlphaBlend`, which is XNA's *premultiplied* preset (`One`/`InverseSourceAlpha`), so a non-premultiplied source legitimately blends to a different result than the test expected (the IGL renderer's `(127,0,255,255)` was the mathematically correct answer for that blend state). Switched the draw to `BlendState::NonPremultiplied`, matching the same distinction already documented in `llgl_2d_test.cpp` |
| IGL-60 | Vulkan backend run | ✅ | Brought up against Mesa lavapipe. Four real bugs found and fixed across three sessions. **Bug A -- back-buffer red/blue swap**: the Vulkan swapchain's physical format is whatever the window system offers (`BGRA_UNorm8` on Linux/X11/Mesa) and `copyBytesColorAttachment` copies raw bytes with no reordering; fixed with `SwapRedBlueIfBgrOrdered()`, checking the ACTUAL reported format rather than hardcoding "swap on Vulkan". Pinned by `Igl_VulkanBackBufferBgr`. **Bug B -- `NormalizedZRange` misreported**: `igl::IDevice::getNormalizedZRange()` defaults to `NegOneToOne` and neither IGL backend overrides it, which is right for OpenGL and wrong for Vulkan; `FillEffectUniforms` now checks `IsVulkanBackend()` directly instead of trusting the API's own report. This alone took `Igl_3D` from 0/3 to 3/3 and `Igl_RenderTarget` from 2/6 to 6/6. **Bug C -- `Igl_2D` rendered nothing visible**: root-caused on 2026-08-18 and fixed. It was never a `SpriteBatch` rendering defect: `ReadBackbuffer()` converted the requested rectangle's y to a BOTTOM-first origin, which is what `glReadPixels` wants and the opposite of what `vkCmdCopyImageToBuffer` wants, and IGL passes the value straight through without reconciling the two (exactly as it does for the scissor rectangle, where `ApplyScissor` already converted for this same reason). The two conventions coincide only when the rectangle IS the whole attachment -- which is precisely why an earlier session's full-surface diagnostic copy could see both sprites at the correct place and colour while every 1x1 sample of the same frame read its mirror row and found the clear colour, and why the "read the whole surface and crop" workaround that session tried fixed `Igl_2D` and broke `Igl_RenderTarget_Vulkan`: it changed the flip parity rather than the origin. The discriminator the earlier investigation looked for was not a property of the SCENE at all, it was full-rectangle versus sub-rectangle. `Igl_2D` now passes 5/5 on Vulkan and `Igl_Msaa` 4/4. **Bug D**: the off-screen render-target Y flip was applied on both backends -- see IGL-67, fixed in the same change. Measured by `Igl_ReadbackOrientation` on both backends |
| IGL-61 | Occlusion queries | ⬜ **not implementable at `v1.1.1`** | Re-verified against the pinned source on 2026-08-17, rather than restated: `grep -rl "OcclusionQuery\|createQuery\|IQuery" ~/deps/igl/src/igl/` matches nothing at all -- there is no query object, no `IDevice` factory for one and no encoder call to begin or end one, on any backend. `SupportsCapability(OcclusionQuery)` reports false and the query completes immediately with 0 rather than spinning forever. Nothing to implement here until upstream adds an API; this row stays open deliberately rather than being closed as "done" |
| IGL-62 | Sampler LOD bias | ⬜ **not implementable at `v1.1.1`** | Re-verified against `igl/SamplerState.h` on 2026-08-17: `SamplerStateDesc` carries `mipLodMin`, `mipLodMax` and `maxAnisotropic` and no LOD-bias field of any kind. CNA records the requested bias and does not apply it, which is what `docs/igl-renderer.md` says. Same disposition as IGL-61 |
| IGL-63 | Cube-target MSAA | ⬜ **not implementable at `v1.1.1`** | Re-verified against `igl/Framebuffer.h` on 2026-08-17: `FramebufferDesc::AttachmentDesc` is one `{texture, resolveTexture}` pair per attachment, and a cube framebuffer attaches the whole cube image (the face is selected by the `TextureRangeDesc` of a later operation, not by the attachment), so there is nowhere to state which face a resolve targets. Applied count reported as 1. Same disposition as IGL-61 |
| IGL-64 | AUDIT/NEXT entries | ✅ | `NEXT.md` now carries an IGL section listing this renderer's genuinely open items (IGL-53, IGL-60, IGL-65's deliberate non-promotion, IGL-66, and the three upstream gaps above); `docs/igl-renderer.md` no longer claims the renderer has never been built |
| IGL-65 | SurfaceFormat boundary and transfer arithmetic | ✅ | **A full audit on 2026-08-17 found the format layer wrong in three independent ways, all fixed.** (1) *Row pitch.* `IglRenderer::CreateTexture` passed `width * 4` as the row pitch of every upload regardless of `ImageData::surfaceFormat`, whose texel is 1, 2, 4, 8 or 16 bytes -- so a narrower format read past the end of the caller's vector and a wider one had every row sliced short; the same `w * h * 4` assumption sized the `dataLength` guards of `IglTextureCubeRenderer::SetData`, `IglTexture3DRenderer::SetData`, `IglRenderTargetCubeRenderer::SetData`/`GetData` and `IglRenderTargetRenderer::GetData`, the last of which would have overrun a caller's buffer by 12 bytes per texel on a `Vector4` target. All of it now goes through `IglSurfaceFormats.{hpp,cpp}`, which delegates to the shared `Texture::GetFormatSizeEXT`/`GetBlockSizeSquaredEXT` metadata rather than adding another format table (the same shape `Fna3dSurfaceFormats` already established), and every upload path refuses a short row or a short buffer by name instead of reading past it. (2) *`Rgba64`.* It was mapped to `igl::TextureFormat::RGBA_UInt32`: CNA's `Rgba64` is an 8-byte R16G16B16A16 unsigned-normalized texel (`PackedVector::Rgba64` packs `r \| g<<16 \| b<<32 \| a<<48`), while `RGBA_UInt32` is a 16-byte R32G32B32A32 *integer-sampled* one -- twice the size and a different sampler type. IGL v1.1.1 has no 16-bit-per-channel RGBA format at all, so the honest outcome of the three the audit allowed is the third: the format is classified unsupported and refused by name. (3) *Silent substitution.* Every other unmapped ordinal fell through to `RGBA_UNorm8`, so a caller asking for `Dxt5` or `NormalizedByte4` silently got an 8-bit RGBA texture. Each format was decided from the layout XNA's own packed-vector types define against what IGL maps it to on **both** backends, and a format the two backends disagree about is refused rather than made backend-dependent -- `Bgr565` (XNA packs R5G6B5; IGL's `B5G6R5_UNorm` is the reverse, and its OpenGL backend's `toFormatDescGL` returns `false` for it outright), `Bgra5551` (XNA packs A1R5G5B5; IGL has only B5G5R5A1/R5G5B5A1), `Bgra4444` (XNA packs A4R4G4B4; IGL's `ABGR_UNorm4` is R4G4B4A4 on OpenGL and B4G4R4A4 on Vulkan), `Rgba1010102` (IGL's `RGB10_A2_UNorm_Rev` is XNA's layout on OpenGL and A2R10G10B10 on Vulkan), `Alpha8` (`VK_FORMAT_UNDEFINED` on Vulkan; the GL_ALPHA family is not in the 4.1 core profile this renderer asks for), `Dxt1`/`Dxt3`/`Dxt5`/`Dxt5Srgb` (IGL has no BC1-3), `NormalizedByte2`/`NormalizedByte4` (IGL has no SNorm format), and `Bc7`/`Bc7Srgb` (IGL *has* the format, but this renderer has no compressed-block upload path -- every transfer goes through `ITexture::upload`, which moves linear rows -- so promoting it would claim a route that does not exist). The set that survives is `Color`, `ColorBgraEXT`, `ColorSrgbEXT`, `ByteEXT`, `UShortEXT`, `Rg32`, `Single`/`Vector2`/`Vector4` and `HalfSingle`/`HalfVector2`/`HalfVector4`/`HdrBlendable`. (4) *Capability reporting.* `IglRenderer` now overrides `ClassifySurfaceFormatEXT`/`ClassifyRenderTargetFormatEXT` to answer `Unsupported` for the refused set and `Defer` for the rest. **Deliberately narrowing, not widening**: `Defer` keeps the framework's own `Texture::ValidateFormat` rule (`Color` only, as for 47 of the 49 renderers) in charge of what a public `Texture2D` may be, because promoting a format is a public-API change that would need per-format end-to-end verification of upload, sampling AND readback, not just of storage. What the promotion would take is recorded in NEXT.md rather than half-done here. Covered by `IglSurfaceFormatTests.cpp` (host-portable, 12 cases) and `Igl_SurfaceFormat` (a real device, one upload/readback round trip per byte class) |
| IGL-67 | Vulkan stored and read render-target rows three different ways | ✅ | Found by `Igl_SurfaceFormat_Vulkan`, root-caused and fixed on 2026-08-18 -- and **not** by the candidate fix this row previously named, which measurement showed would have been wrong. Three defects, each of which hid the other two by cancelling against them. (1) `IglRenderer::SubmitDraw` applied an extra Y flip for every off-screen target on BOTH backends. IGL's Vulkan encoder already flips the viewport height so clip +Y runs up the screen on both -- but "up the screen" is a different direction through image memory in each, because a GL texture's row 0 sits at t=0 (the bottom) while a Vulkan image's row 0 is the top. So the flip is what puts a GL target's rows in `SetData` order and what took a Vulkan target's rows OUT of it; it is now OpenGL-only, as is the matching front-face winding reversal in `IglPipelineCache`. (2) `igl::vulkan::Framebuffer::copyBytesColorAttachment` passes `flipImageVertical = true` unconditionally while its OpenGL counterpart is a plain `glReadPixels` that reverses nothing; undone once, for `RenderTarget2D` and for `RenderTargetCube` faces, by the new `FlipRowsInPlace`. (3) `ReadBackbuffer`'s row origin -- see IGL-60 Bug C. **The obvious fix was measurably wrong:** uploading into a target and then SAMPLING it was ALREADY correct on Vulkan, so flipping rows inside `UpdatePixels` would have broken the sampler in order to satisfy the readback. This row was right to refuse to guess it. The evidence that settled it is `Igl_ReadbackOrientation`, which uses no draw call for the part that matters -- a readback flip and a projection flip cancel exactly, which is why every rendering-based pixel test the renderer had was blind to all three |
| IGL-68 | IGL turned a reported error into a process trap | ✅ | Also found while running the suite for IGL-65, and the reason `ShaderEffectTest.CloneReturnsIndependentShaderEffectWithSameSource` took the whole `CnaTests` binary down under this renderer (exit `133`, ~1,100 tests never reached). IGL's debug builds answer an internal failure by logging it **and** raising `SIGTRAP` (`igl::_IGLDebugBreak`, reached from every `IGL_DEBUG_ABORT`/`IGL_SOFT_ASSERT`) -- the right default for IGL's own samples, the wrong one for an embedder, because the very same call sites also fill in the `igl::Result` this renderer already checks and reports by name. A `ShaderEffect` whose GLSL does not compile is the plain case: `verifyResult` records "failed to compile vertex shader", hands back a null module, and then traps before `IglEffectRenderer` can raise the exception IGL-42 promises. `IglRenderer`'s constructor now calls `igl::setDebugBreakEnabled(false)` once per process. Only the *break* is disabled -- IGL still logs every such failure at error level and CNA still checks every `Result` and throws by name, so nothing is swallowed |
| IGL-69 | Four shared, renderer-keyed test tables had no IGL arm (and one had an ODR bug) | ✅ | Shared suites choose their EXPECTED OUTCOME from a per-renderer table; four of them never gained an `Igl` entry, so an IGL build was asserted against the default profile and failed on behaviour this plan documents as correct. (1) `GraphicsDeviceCapabilityTests.cpp`'s `ExpectedCapabilities()` default claims occlusion-query support, which IGL-61 records IGL v1.1.1 cannot have -- IGL now answers `{multipleRenderTargets = true, occlusionQuery = false, customEffects = true}`. (2) `InstancedDrawMultiStreamTests.cpp`'s final `else` asserts the renderer implements *no* instanced path and must refuse a mixed-stream draw; IGL implements one (IGL-31), so it has its own measured-outcome arm asserting acceptance -- deliberately NOT added to that file's `MultiStreamOracle()` pixel set, since nothing has measured this renderer's mixed-frequency multi-stream *result*. (3) `TextureCubeTests.cpp` treated cube storage and cube readback as one set; IGL falsifies that premise (IGL-17: it owns cube pixels, and exposes readback only through `IFramebuffer`, which a plain `TextureCube` has none of), so level-0 readback is now its own predicate. (4) `Texture3DTextureCubeRenderTargetTests.cpp`'s `RenderTargetCubeAcceptsSetData()` asserts a refusal IGL deliberately does not make -- `IglRenderTargetCubeRenderer::SetData` seeds a face with a real upload (IGL-21). **An ODR bug surfaced while fixing (3):** `CubeStorageSupported`/`CubeLevel0ReadbackSupported`/`CubeMipReadbackSupported` were `inline` at namespace scope in BOTH `TextureCubeTests.cpp` and `modules/content/tests/.../Texture3DTextureCubeContentTypeReaderTests.cpp`, with different bodies -- so the linker kept one arbitrarily and the first edit to the graphics table changed nothing at all. Given internal linkage, so each suite's table is its own. Verified: 97/97 across the four suites plus every `*Igl*` suite |
| IGL-70 | A custom `ShaderEffect` failure said nothing useful | ✅ | `igl::Result::message` carries only "glslang_shader_parse() failed"; the line number, the offending declaration and the reason all go to IGL's own log, where a caller cannot reach them. A `ShaderEffect` author was left unable to tell a missing `layout(location = ...)` apart from a typo. `IglEffectRenderer::CompileProgram` now installs an IGL log handler around the one compile call, folds the captured error text into `compileError_`, and on Vulkan appends the SPIR-V authoring requirement in words. The handler forwards everything to whatever handler was installed before, so IGL's own logging is unchanged, and it is installed and restored around a single call on the thread that owns the device |
| IGL-66 | Renderer shutdown leaves a dangling IGL context reference | ✅ | Found while running the suite for IGL-65: every example test that renders anything exits `133` (`SIGTRAP`) *after* printing all its PASS lines, on `IGL_SOFT_ASSERT(refCount_ == 0, "Dangling IContext reference left behind")` in `igl::opengl::IContext::~IContext`. Root cause: `dummyFlatNormal2D_` was added to `IglRenderer` by GLTF-374 (`0978620`) without a matching `reset()` in `~IglRenderer`, and every IGL OpenGL resource holds a reference to the context, so one surviving texture is enough. Confirmed pre-existing by re-running the same test on a stashed tree. Fixed by releasing it beside its two siblings, in the same "destroy while the device is still alive" block |

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
| `Texture3D`/`TextureCube` readback | IGL exposes readback through `IFramebuffer`, not `ITexture`, and a plain volume or cube texture owns no framebuffer (IGL-17) | `GetData` returns false and the XNA layer raises `Texture3D::GetData: this graphics renderer cannot read a volume texture back to the CPU at the requested mip level`. Two shared content-reader tests (`Texture3DTextureCubeContentTypeReaderTest`) require that readback and therefore fail under this renderer -- a pre-existing consequence of this row, not a defect in the reader |
| IGL's own debug trap | IGL's debug builds raise `SIGTRAP` from the same call sites that fill in the `igl::Result` CNA checks, so a reported error became an unrecoverable trap (IGL-68) | `IglRenderer`'s constructor disables the break once per process; the error is still logged by IGL and still raised by CNA as an exception naming the failure |
| Non-`Color` surface formats in the public API | IGL genuinely stores 13 of XNA's 27 formats (IGL-65 lists which, and why each of the other 14 is refused), but promoting one to the public API means promising upload, sampling AND readback for it, and only storage is verified | `ClassifySurfaceFormatEXT`/`ClassifyRenderTargetFormatEXT` answer `Unsupported` for what IGL cannot store and `Defer` for the rest, so the framework's own `Color`-only rule still decides; the supported set is reachable through the renderer contract and is tested there |
| Custom `ShaderEffect` source on Vulkan | Wider than the loose-uniform row above, and only observed once this session ran the custom-effect tests on Vulkan: a shader written in desktop-GL GLSL is rejected by `glslang_shader_parse()`, and `igl::glslang::compileShader` responds with `IGL_ABORT` rather than an error code | the **process aborts**; CNA has no error to report and no way to know in advance which sources glslang will accept, so a custom effect for the Vulkan backend must be written as Vulkan GLSL |
| `RenderTarget2D` MSAA resolve on Vulkan | `Igl_Msaa`'s blended-edge check fails on lavapipe while passing on llvmpipe; not yet isolated to CNA, IGL or the driver | the resolve produces a hard edge; `GetMultiSampleCount()` still reports the applied count truthfully |
| A custom `ShaderEffect`'s parameters on Vulkan | Loose non-block uniforms do not exist in Vulkan GLSL and IGL leaves `bindUniform` unimplemented there (IGL-43) | Refused by name at draw time, not drawn with stale values. The effect itself compiles, binds and draws (IGL-42) |
| Custom-effect pipelines and the 7 built-in texture units | A custom `ShaderEffect`'s pipeline only maps the sampler names/units the effect itself set via `SetUniformInt`; `BindEffectResources()` still unconditionally tries every built-in unit (`Texture0` through `OcclusionMap`) on every draw | the unused units' dummy-texture binds fail inside IGL and are silently skipped (`IGL_LOG_INFO_ONCE`'d to one harmless log line per process); the units the effect actually declared bind and sample correctly |

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
        IglSurfaceFormats.hpp     (the SurfaceFormat boundary and the transfer byte arithmetic)
    src/
        IglConversions.hpp        (family-private XNA-ordinal → IGL-enum translation)
        IglDraw.cpp               (every draw route, uniform fill, sprite submission)
        IglEffectRenderer.cpp     (custom ShaderEffect)
        IglPipelineCache.cpp      (vertex input / shader / pipeline / sampler / depth-stencil caches)
        IglPlatformSurface.cpp    (device bring-up; the only TU with X11/GLX/Vulkan includes)
        IglRenderer.cpp           (lifecycle, frame model, clears, state, capabilities)
        IglResources.cpp          (textures, buffers, targets, dynamic pool, queries)
        IglRendererDescriptor.cpp (the pre-construction contract: window kind, framebuffer, services)
        IglRendererSelection.cpp
        IglShaderLibrary.cpp      (the generated GLSL)
        IglSurfaceFormats.cpp
        IglSpriteBatchRenderer.cpp
    examples/{CMakeLists.txt,igl_smoke_test.cpp,igl_2d_test.cpp,igl_3d_test.cpp,igl_rendertarget_test.cpp,
        igl_alphatesteffect_test.cpp,igl_dualtextureeffect_test.cpp,igl_fog_test.cpp,igl_stencil_test.cpp,
        igl_mrt_test.cpp,igl_msaa_test.cpp,igl_environmentmapeffect_test.cpp,igl_skinnedeffect_test.cpp,
        igl_pbreffect_test.cpp,igl_rendertargetcube_test.cpp,igl_shadereffect_texture3d_test.cpp,
        igl_instancing_test.cpp,igl_skinnedeffect_translation_bone_test.cpp,
        igl_environmentmapeffect_fresnel_test.cpp,igl_pbreffect_maps_test.cpp,
        igl_spritebatch_shadereffect_test.cpp,igl_mrt4_test.cpp,igl_mrt_cube_refuse_test.cpp,
        igl_vulkan_backbuffer_bgr_test.cpp,igl_surfaceformat_test.cpp}
    tests/CNA/Internal/Renderers/Igl/{IglRendererSelectionTests.cpp,IglSurfaceFormatTests.cpp}
modules/graphics/tests/Microsoft/Xna/Framework/Graphics/GraphicsDeviceCapabilityTests.cpp   (Igl arm)
modules/graphics/tests/Microsoft/Xna/Framework/Graphics/InstancedDrawMultiStreamTests.cpp   (Igl arm)
docs/igl-renderer.md
plan_igl.md
NEXT.md
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

---

## 8. The 2026-08-17 audit

A full audit of this renderer against this plan, on a fresh machine that had to build IGL from
scratch. Four real defects, and one lesson that generalises past this renderer.

**The lesson: "written, not yet compiled" is not evidence that anything was written.** IGL-7 (the
window's render intent) carried the ✍️ marker from the day the renderer landed, and the marker was
read by two later sessions -- and by the runtime-renderer port -- as "this exists, it just has not
run yet". It did not exist. `git show --stat 4f67967` does not list `GraphicsDevice.cpp`; the two
helper functions the row names were written, unit tested, and called by nothing. The port then
wrote a constant `RendererWindowKind::OpenGL` into the new descriptor, which looks exactly like a
faithful port of a decision nobody had made. **A plan row should say which file makes the decision
and a gate or test should be able to see it happen** -- `IglRendererSelectionTests.cpp` now reads
the registered descriptor back and compares it against the resolved backend, which is a claim that
cannot rot the same way.

The four defects, in the order they were found:

1. **The window contract (IGL-7, IGL-7b).** A constant window kind, and no pre-window framebuffer
   request at all. `CNA_IGL_BACKEND=vulkan` therefore built a Vulkan device on an OpenGL-intent
   window, and `CNA_IGL_BACKEND=opengl` got whatever visual the platform defaulted to -- in
   practice one with no stencil bits, which makes every `DepthStencilState.StencilEnable` a silent
   no-op on a backend whose visual is fixed at window-creation time.
2. **The surface-format layer (IGL-65).** `width * 4` as the row pitch of every upload; `w * h * 4`
   as the size guard of five more transfer paths; `Rgba64` mapped to a texel twice its size with
   integer sampling; and every other unrepresentable format silently substituted with RGBA8.
3. **Shutdown (IGL-66).** `~IglRenderer` never released the flat-normal dummy texture GLTF-374
   added, so every example test aborted at process exit on IGL's own dangling-context assert --
   *after* printing all of its passes, which is why nothing had noticed.
4. **Vulkan render-target uploads (IGL-67).** Found by the new format test being run on both
   backends deliberately. Left open, with the reason for not guessing at a fix recorded in its row.
5. **A reported error became a process trap (IGL-68).** IGL's debug builds raise `SIGTRAP` from the
   same call sites that fill in the `igl::Result` CNA already checks, so an invalid custom shader
   killed the whole `CnaTests` binary instead of raising the exception IGL-42 promises.
6. **Four shared test tables had no IGL arm (IGL-69).** The capability suite asserted an
   occlusion-query capability this renderer documents it cannot have; the instancing suite asserted
   that it implements no instanced path at all; the cube suite conflated storage with readback; and
   the render-target-cube contract asserted a `SetData` refusal this renderer deliberately does not
   make. Fixing the third exposed an ODR bug in the test corpus that had been silently discarding
   edits to one of those very tables.

Two of those six -- IGL-66 and IGL-68 -- were only visible because the suite was run at all, and
both of them *hid* their own evidence: each aborts the process at or after the point where the
checks had already printed PASS. Exit code, not console output, is the thing to read.

What the audit did **not** do, deliberately: promote the supported non-`Color` formats to the public
API. The renderer now knows exactly which formats it can store, and could report them -- but a
promotion promises upload, sampling and readback, and only storage is verified. §4's own row says
so, rather than the code claiming more than the tests measure.
