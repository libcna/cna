# plan_igl.md — the IGL (facebook/igl) graphics renderer

CNA's 47th public renderer identity. Like `LLGL`, and unlike every renderer that names a native
graphics API, **`IGL` names a portable abstraction**: `igl::IDevice` fronts real OpenGL/OpenGL ES,
Vulkan and Metal implementations, and CNA picks which one this process uses rather than which one
this build contains.

Upstream: <https://github.com/facebook/igl>, pinned at **`v1.1.1`** (MIT). Local reusable checkout:
`~/deps/igl` (`CNA_IGL_ROOT`).

---

## 1. Status

**Branch:** `feature/igl`. **Nothing has been compiled yet** — the project owner deferred every
build until the machine is free (see §7). Every task below marked ✍️ is *written but unverified*;
no task moves to ✅ until it has actually been built and its test has actually run.

| Phase | What it covers | State |
|-------|----------------|-------|
| A | Identity registration and build integration | ✍️ written |
| B | Device bring-up and presentation | ✍️ written |
| C | Resources (textures, buffers, targets) | ✍️ written |
| D | 2D pipeline (`SpriteBatch`) | ✍️ written |
| E | 3D pipeline and stock effects | ✍️ written |
| F | Custom `ShaderEffect` | ✍️ written |
| G | Tests, docs and gates | ✍️ partial |
| H | Verification and hardening | ⬜ not started (blocked on the first build) |

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
| IGL-15 | Back-buffer MSAA | ✍️ | real on OpenGL (the platform GL visual); reported as 1 on Vulkan, whose swap-chain images are single-sample |

### Phase C — resources

| ID | Task | State | Notes |
|----|------|-------|-------|
| IGL-16 | `Texture2D` | ✍️ | RGBA8 and the mapped `SurfaceFormat` set, real mip levels, `HasDefinedMipLevel` from what was actually uploaded |
| IGL-17 | `TextureCube` / `Texture3D` | ✍️ | real `igl::TextureType::Cube` / `ThreeD` resources, per-face and per-box uploads |
| IGL-18 | Vertex / index buffers | ✍️ | lazily created, grown on demand, re-upload flushes the pending frame first |
| IGL-19 | Dynamic buffer pool | ✍️ | 3-frame ring for `SpriteBatch`/`DrawUser*`/uniforms |
| IGL-20 | `RenderTarget2D` | ✍️ | colour + optional depth/stencil, real MSAA with an IGL resolve attachment, mip regeneration after each pass, `GetData` via `copyBytesColorAttachment` |
| IGL-21 | `RenderTargetCube` | ✍️ | one shared cube image + one shared depth buffer (FNA's own shape), six per-face framebuffers |
| IGL-22 | MRT | ✍️ | 2–4 `RenderTarget2D` slots; a cube face in a multi-target set is refused by name |
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
| IGL-55 | Stock-effect parity tests | ⬜ | the per-effect batteries the LLGL/Magnum families carry (`*_basiceffect_*`, `*_dualtexture_*`, `*_environmentmapeffect_*`, `*_skinnedeffect_*`, `*_mrt_*`, `*_msaa_*`, `*_stencil_*`) |

### Phase H — verification and hardening (blocked on the first build)

| ID | Task | State | Notes |
|----|------|-------|-------|
| IGL-56 | First configure | ⬜ | `cmake -S . -B cmake-build-igl -DCNA_GRAPHICS_RENDERER=IGL -DCNA_IGL_ROOT=~/deps/igl` with ccache, `-j3` |
| IGL-57 | First compile of the renderer target | ⬜ | expect real diagnostics: this is ~4 500 lines written against IGL's headers without a single compile |
| IGL-58 | First frame | ⬜ | `Igl_Smoke` under `CNA_TEST_DISPLAY` |
| IGL-59 | Pixel conformance | ⬜ | `Igl_2D`, `Igl_3D`, `Igl_RenderTarget` |
| IGL-60 | Vulkan backend run | ⬜ | the same four tests with `CNA_IGL_BACKEND=vulkan` |
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
    examples/{CMakeLists.txt,igl_smoke_test.cpp,igl_2d_test.cpp,igl_3d_test.cpp,igl_rendertarget_test.cpp}
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

## 7. Why nothing is compiled yet

The project owner asked (2026-08-15) for the renderer to be written first and built later, because
the machine was busy compiling other work at the time. Every ✍️ row above is therefore *written but
unverified*, and the first build (IGL-56/IGL-57) should be expected to produce real compiler
diagnostics rather than a clean pass. Nothing in this plan claims otherwise.
