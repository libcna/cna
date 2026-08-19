# NanoVG renderer

## Current status

`CNA_GRAPHICS_RENDERER=NANOVG` is a 2D vector-graphics renderer implemented through
[NanoVG](https://github.com/memononen/nanovg) (memononen/nanovg, pinned commit
`ce3bf745eb2d2dbc14a50bf2446783f691ac4353`, zlib license) — a small, widely used vector-graphics
library driven through its own compiled GLSL 1.10 shader pipeline (the **GL2 backend**,
`NANOVG_GL2_IMPLEMENTATION`) on top of a real desktop OpenGL 2.1 compatibility context CNA creates
and owns itself (via `PlatformGlContextOwner`, the same "own GL context, no EasyGL" shape
`OPENGL1`/`OPENGL2`/`OPENVG` already use).

    CNA -> NanoVgRenderer -> NanoVG (real nvgBeginPath/nvgFill/nvgImagePattern/... entry points,
           compiled GLSL shaders) -> OpenGL

See `nanovg-spike/README.md` for the standalone existence-gate proof (a real GLSL-shader-driven
path drawn and read back via `glReadPixels`, under Xvfb) that predates this renderer's CNA
integration, and `plan_nanovg.md` for the delivery task list and design decisions.

NanoVG is a 2D-only vector-graphics API: it has no 3D pipeline, no vertex/index buffer concept
reachable from CNA, and no caller-addressable depth/stencil surface (its own internal stencil
usage for anti-aliased stroke rendering is a private implementation detail). Every inherently-3D
`IGraphicsRenderer` pure virtual throws (`Unsupported3DGraphicsCallBehavior::Throw`, the shared
default) rather than being silently ignored or faked, and `Ensure3DSupported()` is overridden so
every modern `GraphicsDevice.Draw*`/`DrawIndexed*`/`DrawInstanced*`/`DrawUser*` entry point is
rejected before any vertex/index-buffer validation or sampler-state application runs.

## How it differs from OPENVG

`OPENVG` (ShivaVG) and `NANOVG` are CNA's two 2D-vector-on-real-GL renderers, but they occupy
genuinely different cells (`docs/renderer-registry.md`'s no-alias rule):

| | Pipeline model | Coordinate system |
|---|---|---|
| `OPENVG` | Fixed-function/immediate-mode GL (no shader compilation at all) | Y-up image space; every draw needs a device-flip transform |
| `NANOVG` | Compiled GLSL shaders, real VBOs | Top-left-origin, Y-down (matches XNA/Canvas2D directly) |

Two concrete consequences, both verified by tests:

- **No device-flip transform anywhere in `NanoVgSpriteBatchRenderer`.** NanoVG's own coordinate
  system already matches XNA's, the same reason `CanvasSpriteBatchRenderer` needs none either.
- **`BlendState.Additive` is genuinely supported.** NanoVG's `NVG_LIGHTER` composite operation is a
  real `(GL_ONE, GL_ONE)` `glBlendFuncSeparate` (`nanovg.c`'s own `nvg__compositeOperationState`);
  ShivaVG declares `VG_BLEND_ADDITIVE` but its `updateBlendingStateGL` has no case for it and
  silently falls back to ordinary alpha blending, so `OPENVG` must reject it. `NANOVG` accepts it
  and reports `GraphicsCapability::AdditiveBlending` true — the one genuine capability edge over
  `OPENVG`.

## NanoVG's own internal alpha premultiplication (a real, load-bearing implementation detail)

`nanovg_gl.h`'s fragment shader always premultiplies an RGBA image's sampled colour by its own
alpha before any blend stage runs:

    if (texType == 1) color = vec4(color.xyz*color.w, color.w);

`texType` is `1` whenever the image was **not** created with `NVG_IMAGE_PREMULTIPLIED` — true of
every `NanoVgTextureRenderer` (`ImageData`/`Texture2D::SetData` are always straight alpha, matching
every other CNA renderer's own convention). This is *why* `NVG_SOURCE_OVER` (`GL_ONE,
GL_ONE_MINUS_SRC_ALPHA`) correctly reproduces a straight-alpha "over" composite for
`AlphaBlend`/`NonPremultiplied` — premultiplied-then-over is the textbook-correct way to perform
the identical blend a straight-alpha renderer expresses as `(SrcAlpha, InvSrcAlpha)` — and it is
also *why* `NVG_LIGHTER` (`GL_ONE, GL_ONE`) correctly reproduces real XNA `BlendState.Additive`
(`SourceBlend=SourceAlpha, DestinationBlend=One`, per `modules/graphics/src/Xna/BlendState.cpp`):
the shader's own premultiply already applies the `SourceAlpha` factor, so the fixed-function
`(One, One)` blend on top of it is exactly equivalent.

The one place this premultiplication is directly OBSERVABLE rather than compensated-for is
`BlendState.Opaque` (`NVG_COPY`, `GL_ONE, GL_ZERO`): "copy" has no compensating blend factor, so a
genuinely translucent source drawn with `Opaque` shows the shader's premultiplied (alpha-attenuated)
colour, not the full un-multiplied source colour a straight-alpha renderer would show. There is no
way to avoid this within NanoVG's public API — moving the premultiply to the CPU side (uploading
already-premultiplied bytes with `NVG_IMAGE_PREMULTIPLIED` set) produces the identical final colour
by associativity, it only moves WHERE the multiply happens, not whether it happens. This is a real,
permanent, documented deviation for `Opaque` combined with a non-opaque-alpha source; `Opaque`
combined with a fully-opaque (alpha=255) source — the overwhelmingly common real usage — is
unaffected (multiplying by 1.0 is the identity).

## Presentation model

Ported directly from `OpenVgRenderer::ComputeLogicalViewportEXT()` (`NanoVgRenderer` has its own
copy, `NanoVgRenderer::ComputeLogicalViewportEXT()`), physical-pixel-based throughout, covering all
five `CnaPresentationMode` values (`Letterbox`, `Overscan`, `Stretch`, `NativeBackBuffer`,
`FixedHeightDynamicWidth`). `glViewport` places the current logical canvas onto the current
physical sub-rectangle; each `NanoVgSpriteBatchRenderer::Begin()` calls
`nvgBeginFrame(ctx, logicalWidth, logicalHeight, ratio)` scoped to the SAME logical size (NanoVG's
own `glnvg__renderViewport` builds an internal orthographic mapping from `[0,logicalWidth] x
[0,logicalHeight]` to NDC, independent of `devicePixelRatio` — that parameter only affects
anti-aliasing feather width, not the coordinate mapping, confirmed by reading `nanovg_gl.h`
directly), so the two combine to map sprites correctly under every presentation mode.

Unlike `OpenVgRenderer::SetScissorRect`, **no presentation-mode remapping is needed for the
scissor rectangle**: `nvgScissor` is expressed in the same logical coordinate space `nvgRect`/
`SpriteBatch` draws already use — NanoVG maps it internally the same way it maps path geometry —
so `NanoVgRenderer::SetScissorRect` stores its argument verbatim.

## Verified capability boundary

| CNA feature | NanoVG route | Notes |
|---|---|---|
| `Clear`, `Present` | Real `glClearColor`/`glClear` + `SDL_GL_SwapWindow` | |
| Presentation modes (`Letterbox`/`Overscan`/`Stretch`/`NativeBackBuffer`/`FixedHeightDynamicWidth`) | Real `glViewport` for the physical sub-rectangle + NanoVG's own internal logical-space ortho | All five modes implemented; pixel-tested for `NativeBackBuffer` (`nanovg_spritebatch_rotation_test`). |
| Custom `Viewport` | Real `glViewport`, raw pass-through (matches every sibling renderer) | |
| `ScissorRectangle` / `RasterizerState.ScissorTestEnable` | Real `nvgScissor`/no-op-when-disabled, applied once per `SpriteBatch.Begin()` | Independent of `SetScissorRect`, matching `OPENVG`'s own enable/rectangle separation. |
| `Texture2D` create/update | Real `nvgCreateImageRGBA`/`nvgUpdateImage` | Straight (non-premultiplied) RGBA8, top-row-first, tightly packed (NanoVG's own API has no stride parameter — `NanoVgTextureRenderer` repacks when the caller's stride differs). No row flip anywhere: NanoVG's Y-down image space already matches `ImageData`'s own convention. |
| `SpriteBatch` draw (all 3 overloads, rotation, origin, source rectangle, tint, `SpriteEffects` flip) | Real `nvgImagePattern` + a filled rectangle path (`nvgBeginPath`/`nvgRect`/`nvgFillPaint`/`nvgFill`) | NanoVG has no "draw image" primitive; a partial `sourceRectangle` needs no CPU-side sub-image copy (unlike `OPENVG`'s `vgCopyImage` workaround) — the pattern box is positioned purely algebraically, all inside the already-`nvgScale`d coordinate system. Verified by `nanovg_spritebatch_rotation_test`. |
| `SpriteBatch.SetTransformMatrix` | Real `nvgTransform` | Row-major XNA `Matrix` decomposed to a 2D affine, same `(a,b,c,d,e,f)` convention `OpenVgRenderer`/`CanvasRenderer` use. |
| Out-of-bounds `sourceRectangle` + `TextureAddressMode.Clamp` | **Real, automatic GPU `GL_CLAMP_TO_EDGE`** | Every `NanoVgTextureRenderer` is created with no `NVG_IMAGE_REPEATX`/`NVG_IMAGE_REPEATY` flag, which NanoVG's own `glnvg__renderTexture` maps to `GL_CLAMP_TO_EDGE` on both axes — no CPU-side edge-padding needed, unlike `OPENVG`. |
| `TextureAddressMode.Wrap`/`Mirror` combined with an out-of-bounds `sourceRectangle` | **Rejected** (throws) | NanoVG bakes repeat/clamp into the IMAGE at creation time (`NVG_IMAGE_REPEATX`/`Y`), not per draw — a genuinely different per-draw wrap mode cannot be honored without recreating the texture. |
| Backbuffer readback | Real `glReadPixels` against the same GL framebuffer NanoVG rendered into | Physical pixel coordinates. |
| `BlendState.Opaque` | Real `NVG_COPY` (`GL_ONE, GL_ZERO`) | Pixel-tested (`nanovg_blend_test`) with a fully-opaque source. **Known deviation** for a translucent source — see "NanoVG's own internal alpha premultiplication" above. |
| `BlendState.NonPremultiplied` | Real `NVG_SOURCE_OVER` | Pixel-tested against a computed expected composite; correct because of the shader's own premultiply (see above), not despite it. |
| `BlendState.AlphaBlend` | Mapped to `NVG_SOURCE_OVER` too | Produces the SAME visible pixel as `NonPremultiplied` — see "NanoVG's own internal alpha premultiplication" above for why this is exact, not approximate. Pixel-tested. |
| `BlendState.Additive` | **Real `NVG_LIGHTER`** (`GL_ONE, GL_ONE`) | Genuinely implemented and pixel-EXACT (not approximate) — see "NanoVG's own internal alpha premultiplication" above. Pixel-tested. |
| Arbitrary custom `BlendState` combinations | **Rejected** (throws) | No generic blend-factor/equation model exists. Tested. |
| `BlendState.ColorWriteChannels` | **Rejected when non-default** (throws) | NanoVG's own stencil-then-color two-pass fill implementation (`nanovg_gl.h`'s `glnvg__fill`/`glnvg__convexFill`) unconditionally calls `glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE)` before every color pass, so an externally-set write mask cannot survive a single draw — verified empirically (a mask wrapped around a whole `SpriteBatch` batch was silently undone). Rejecting is the honest choice; silently ignoring it would be a capability lie. Tested. |
| `BlendState.MultiSampleMask` | Intentionally ignored (not honored, not rejected) | This renderer never creates a multisample-capable GL context (`GraphicsCapability.MultiSampleAntiAliasing` is `false`), so a coverage mask can never have any observable effect here. |
| `RasterizerState.CullMode` | Accepted for every value | 2D quads are never back-face culled by any CNA renderer regardless of value. |
| `RasterizerState.FillMode.WireFrame` | **Rejected** (throws) | No unfilled-polygon draw path exists. `SupportsCapability(WireFrame)` reports `false`. |
| `RasterizerState.DepthBias`/`SlopeScaleDepthBias` | **Rejected when non-zero** (throws) | No depth buffer exists for either to bias against. |
| `DepthStencilState` | **`DepthStencilState.None` is accepted; any meaningfully-enabled depth or stencil state, or a non-zero `ReferenceStencil`, is rejected** (throws) | Matches ordinary `SpriteBatch.Begin()` usage and normal `GraphicsDevice` construction. `SupportsDepthStencil()` reports `false`. |
| Modern 3D `Draw*`/`DrawIndexed*`/`DrawInstanced*`/`DrawUser*` entry points | **Rejected by `Ensure3DSupported()`** (Throw policy) or safely no-op'd (WarnAndStub policy) | Verified through the public `GraphicsDevice` API (`nanovg_unsupported_3d_behavior_test`). |
| Render targets (`RenderTarget2D`/`RenderTargetCube`) | **Unsupported** (`CreateRenderTarget2D` returns `nullptr`, the shared default) | NanoVG's own off-screen-framebuffer helper (`nanovg_gl_utils.h`'s `NVGLUframebuffer`) is deliberately out of this renderer's scope — see `plan_nanovg.md`'s "Known limitations". `GraphicsDevice::SetRenderTarget(RenderTarget2D*)` throws `System::NotSupportedException` transactionally, matching every renderer with no real render-target storage. |
| Custom `Effect` in `SpriteBatch` | **Rejected** (throws) | No caller-addressable programmable shader stage exists on this renderer (NanoVG's own GLSL pipeline is fixed, internal, and not exposed to CNA's `Effect` system). |
| `GraphicsCapability.ThreeD`, `DepthStencilBuffer`, `MultipleRenderTargets`, `OcclusionQuery`, `CustomEffects`, `CompiledEffects`, `Texture3D`, `MultiSampleAntiAliasing`, `AnisotropicFiltering`, `WireFrame`, `Instancing`, `MultiStreamVertexInput`, `StencilBuffer` | All report `false` | See `NanoVgRenderer::SupportsCapability`'s own comment. |
| `GraphicsCapability.AdditiveBlending` | Reports **`true`** | The one genuine capability edge over `OPENVG` — see above. |
| Swap interval | Real `SDL_GL_SetSwapInterval`, with a fallback chain (requested -> 1 -> 0) | |

## Dependency and build

`cmake/ThirdPartyNanoVG.cmake` fetches NanoVG's source at the pinned commit via `FetchContent` and
compiles its backend-agnostic core (`nanovg.c`, which itself pulls in the vendored `fontstash.h`/
`stb_image.h` with their own `..._IMPLEMENTATION` macros) into a small static library,
`cna_thirdparty_nanovg`. The GL2 render backend (`nanovg_gl.h`) is deliberately **not** compiled
there: it calls `gl*` entry points unqualified rather than loading them itself (unlike a real
loader such as GLAD), so `modules/renderers/nanovg/src/NanoVgGl.cpp` declares file-scope
function-pointer shims for the ~28 post-GL-1.1 entry points NanoVG's GL2 backend calls, resolves
them through the platform's GL loader (`LoadPlatformGlProcAddress`, the same one
`OpenGL2Renderer.cpp` uses for its own shader functions), and only then `#include`s `nanovg_gl.h`
with `NANOVG_GL2_IMPLEMENTATION` — see that file's own header comment for exactly why
`GL_GLEXT_PROTOTYPES` is never defined there. `nanovg-spike/README.md` documents the standalone
proof this mechanism works before it was integrated into CNA.

## Supported platforms

Desktop Linux, Windows, and macOS only (`cmake/RendererSelection.cmake`'s own platform gate) —
NanoVG's GL2 backend needs a real desktop OpenGL 2.x+ context (GLSL 1.10 shaders), which neither
WebGL (Emscripten) nor a GLES-only mobile target can create. Native validation actually performed
in this environment is **Linux + Xvfb (X11 GLX) only**; Windows (MSVC)/macOS were NOT compiled or
run here.

## Test status

Real windowed/pixel behavior is covered by `modules/renderers/nanovg/examples/` (this project's
established split for GPU/window-creating tests — pure-function pieces live in
`modules/renderers/nanovg/tests/` instead):

| Test | Coverage |
|---|---|
| `nanovg_smoke_test` | Vertical slice: Clear, SpriteBatch draw, readback. |
| `nanovg_spritebatch_rotation_test` | Decisive rotation/origin geometry oracle (`NativeBackBuffer`). |
| `nanovg_blend_test` | `Opaque`/`AlphaBlend`/`NonPremultiplied`/**`Additive`** against a computed expected composite, deterministic rejection of both a custom `BlendState` and a non-default `ColorWriteChannels`. |
| `nanovg_unsupported_3d_behavior_test` | Throw/WarnAndStub policy across every inherently-3D entry point, `AdditiveBlending`/`Texture3D` capability honesty. |

Plus `NanoVgBlendStateMapping.*` (`modules/renderers/nanovg/tests/`, the pure
`BlendStateToNvgCompositeOperation` mapping function, no window/GL context needed).

**Exact commands run** (this environment): `Xvfb :0 -screen 0 1280x1024x24 &`, then
`DISPLAY=:0 ctest --test-dir cmake-build-nanovg -R NanoVg --output-on-failure` (9/9 pass) and
`DISPLAY=:0 ctest --test-dir cmake-build-nanovg -j4` (the complete `CnaTests` corpus).

## Known cross-renderer test gaps

Running the full `CnaTests` corpus against `-DCNA_GRAPHICS_RENDERER=NANOVG` surfaces the same two
categories of pre-existing shared-test gap `docs/openvg-renderer.md` already documents for
`OPENVG` — neither is a NANOVG-specific defect, both are generic gaps in shared test files that
assumed every renderer provides 3D/render-target/cube-texture storage:

* **`RenderTarget2D`/`TextureCube` genuinely unsupported.** Fixed the same way `OPENVG` was: added
  `NanoVg` to the small number of shared-test allowlists that already gate on a renderer's real
  "no storage" behavior (`GraphicsDeviceValidationTests.cpp`'s `SetRenderTargets_*`/
  `SetRenderTarget_SingleOverload_MatchesArrayOverloadRejection`, and every `CubeStorageSupported()`
  helper across `TextureCubeTests.cpp`, `Texture3DTextureCubeContentTypeReaderTests.cpp`,
  `XnbBuiltInReaderRegistrationTests.cpp` and `CnjCapabilityMatrixTests.cpp`). Also added explicit
  `NanoVg` arms to `GraphicsDeviceCapabilityTests.cpp` (`ExpectedCapabilities()`, `IsTwoDimensionalOnly()`,
  the `WireFrame` expectation chain), `GraphicsBackendCategoryTests.cpp`/`GraphicsBackendMaturityTests.cpp`
  (`TranslationLayer`/`Experimental`), `GraphicsRendererTypeTests.cpp` (`ExpectedNameFor()`) and
  `GraphicsRendererCompileDefinitionTests.cpp` — the same per-renderer-arm registration discipline
  every renderer addition in this repository follows.

* **Pre-existing "content pipeline assumes a 3D pipeline" gap, not introduced by NANOVG.** The same
  ~100 tests across `Model`/`Cnj`/`Gltf`/`SkinnedModel`-loading suites `docs/openvg-renderer.md`
  already names (`GltfCameras`, `GltfConformanceL6`, `GltfModelShape`, `GltfSceneGraphBones`,
  `GltfSkinSpaces`, `GltfRigidAnimation`, `GltfStrideAndBuffer`, `CnaGltfConformanceL*`, ...)
  construct a real `VertexBuffer` (directly, or transitively through `Model` loading) with no
  `GraphicsCapability::ThreeD` guard. `NanoVgRenderer::CreateVertexBuffer` honestly throws
  (`HandleUnsupported3DCall`) under the default `Throw` policy — the exact same call every other
  2D-only renderer already in this registry makes for the identical entry point. Left deliberately
  unmodified for the same reason `OPENVG`'s own audit left it unmodified: a genuine, useful
  follow-up, but a general content-pipeline/2D-renderer-family gap outside this renderer's own
  minimal-shared-change scope.

* **Unrelated, non-graphics flakiness/environment gaps observed in the same full-corpus run.**
  `ModuleLinkClosure_*` (expects a Makefiles-generator `link.txt`, fails under this environment's
  Ninja generator), `CApiCoverageMatrix`/`CApiHeaderCompatibility` (need Doxygen 1.9.8+ and a
  `-std=c23`-capable GCC, neither installed here), `VibrateControllerTests` (SEGFAULT, unrelated to
  graphics), and a couple of `ENet*` networking tests — all independent of which graphics renderer
  is selected, confirmed by the identical failure shape `docs/openvg-renderer.md` already records.

## Known limitations

- **No render targets.** NanoVG's own off-screen-framebuffer helper (`nanovg_gl_utils.h`'s
  `NVGLUframebuffer`) was deliberately left out of this renderer's initial scope (`plan_nanovg.md`)
  — a real follow-up, not attempted here.
- **No custom `Effect`/shader stage.** NanoVG's GLSL pipeline is fixed and internal; there is no
  mechanism to inject a caller-supplied shader into it.
- **Per-draw texture filter/wrap mode is not independently switchable.** NanoVG bakes both
  `NVG_IMAGE_NEAREST` and `NVG_IMAGE_REPEATX`/`Y` into the image at CREATION time, not per draw —
  unlike XNA's `SamplerState`, which is chosen per `SpriteBatch.Begin()` independent of which
  texture is drawn. Every `NanoVgTextureRenderer` is therefore created linear-filtered and
  clamp-addressed; `SetSamplerFilter` is a documented no-op, and `Wrap`/`Mirror` is rejected only
  when it would actually matter (an out-of-bounds `sourceRectangle`).
- **`BlendState.Opaque` on a translucent source shows alpha-attenuated colour, not the full
  un-multiplied source colour.** A real, permanent consequence of NanoVG's own fragment shader
  always premultiplying an RGBA image's colour by its own alpha (upstream behavior, not a CNA
  choice) — see "NanoVG's own internal alpha premultiplication" above. Unaffected for a
  fully-opaque (alpha=255) source, the overwhelmingly common real usage.
- **`BlendState.ColorWriteChannels` cannot be honored at all and is rejected when non-default.**
  NanoVG's own stencil-based fill implementation unconditionally resets `glColorMask` to
  all-channels-enabled before every color pass — verified empirically, not a CNA design choice.
