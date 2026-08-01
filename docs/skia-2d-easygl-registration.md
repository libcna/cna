# Skia registration of reusable EasyGL 2D contracts

SKIA-106 reconciles its original checklist with the live Skia suite. Most requested contracts were
already covered by Skia-native or shared fixtures while their implementation tasks were completed.
Eleven genuinely backend-neutral EasyGL sources now also compile and run unchanged under Skia,
so source reuse and the broader edge-case matrices reinforce rather than replace each other.

## Live registration map

| SKIA-106 contract | Skia CTests | Reuse status |
|---|---|---|
| Textured quad | `Skia_TexturedQuad_Readback` | Exact `easygl_textured_quad_test.cpp` source |
| Rotation, source rectangles, layer depth | `Skia_EasyGL_SpriteBatch_Rotation`, `SourceRect`, `LayerDepth`; `Skia_SpriteBatch_Rotation`, `SourceRect`, `SourceRectLinear`, `LayerDepth` | Three exact EasyGL sources plus broader shared public fixtures |
| Sprite fonts | `Skia_EasyGL_SpriteFont_SingleGlyph`; `Skia_SpriteFont_SingleGlyph`, `MultiGlyphSpacing`, `Newline`, `DefaultChar`, `Effects` | Exact single-glyph source plus shared atlas/metrics fixtures |
| Sampler modes | `Skia_EasyGL_TextureAddressMode`, `TextureAddressMode_Mirror`; `Skia_TextureFilter_PointVsLinear`, `Minification`, `TextureAddressAxes`, `SpriteBatch_SamplerTransition` | Two exact address-mode sources plus 2D SpriteBatch filter/transition matrices |
| Blend presets | `Skia_BlendState_Opaque`, `AlphaBlend`, `NonPremultiplied`, `Additive` | 2D SpriteBatch equivalents with source-alpha-labelled pixels |
| Scissor | `Skia_SpriteBatch_RasterizerState`, `Skia_RenderTarget2D_Scissor` | Shared 2D clip and target-local leakage checks |
| Clear | `Skia_EasyGL_ClearOverloads`; `Skia_PresentationModes`, `Skia_Presentation_Edge`, `Skia_3D_Refusal` | Exact overload source plus empty-frame, failed-draw and absent-attachment policy |
| Render target | `Skia_EasyGL_RenderTarget2D_Readback`; `Skia_RenderTarget2D_Golden`, `Usage`, `Switch`, `SampleAfterUnbind`, `Readback` | Exact source plus shared golden and raster lifecycle fixtures |
| Resize | `Skia_EasyGL_BackbufferReadbackDimension`; `Skia_Resize_Presentation`, `WindowLifecycle`, `PresentationModes`, `DisplayScale` | Exact grow/shrink readback source plus presenter/virtual-resolution fixtures |
| Disposal | `Skia_EasyGL_DisposedResource`; `Skia_Texture2D_Dispose`, `DisposedGuards`, `DoubleDispose`, `RenderTarget2D_Lifetime` | Exact source plus shared wrappers and backend lifetime fixture |
| Readback | `Skia_TexturedQuad_Readback`, `Skia_EasyGL_RenderTarget2D_Readback`, `Skia_EasyGL_BackbufferReadbackDimension`; `Skia_Texture2D_GetDataContract`, `GetDataTransferRange`, `GetBackBufferData_ActiveTarget`, `AfterRtUnbind` | Exact backbuffer/target/dimension sources plus transfer-range and row-order matrices |

All listed tests are display tests except the source/audit helpers elsewhere in the suite. They are
registered through `cna_register_skia_display_test`, so CTest supplies the selected X11/Xvfb
display explicitly. The suite never relies on an implicit real display.

## Exact-source boundary

The exact-source registrations comprise textured-quad readback; rotation, source-rectangle and
layer-depth SpriteBatch behavior; one SpriteFont glyph; Wrap/Clamp and Mirror addressing; Clear
overloads; RenderTarget2D readback; disposed-resource guards; and grow/shrink backbuffer readback.
The few direct `SetDepthTestEnabled(false)` calls in otherwise-2D sources were replaced with
`DepthStencilState::None`, which expresses the same absence of depth work on 3D backends and is a
valid state on a deliberately 2D-only backend. EasyGL and Skia now compile each source from the
same path.

The grow/shrink fixture exposed an X11 timing defect rather than a test portability issue:
`SDL_SetWindowSize` may complete asynchronously, so FixedHeightDynamicWidth could derive a new
raster width from the old output size. Skia now calls SDL's documented `SDL_SyncWindow` barrier
before recreating its backbuffer. This makes PresentationParameters, raster extent and immediate
rectangle-less readback agree after both directions of resize; a synchronization timeout remains
non-fatal and the existing dynamic refresh path can still converge later.

Several historically named EasyGL tests are not exact-source reusable even though the public
contract they validate has a 2D realization. For example, EasyGL's point/linear and blend-preset
fixtures use `DualTextureEffect`/`BasicEffect` plus `DrawUserPrimitives`, and its scissor fixture
draws BasicEffect triangles. Registering those files under Skia would test the rejected 3D route,
not the 2D sampler/blend/clip contract. The live Skia tests in the table exercise the same public
2D outcome without omitting any asserted behavior.

This distinction also explains the migration-matrix categories: a row may have a direct 2D Skia
route through an equivalent public fixture even when an EasyGL-only implementation happens to use
3D geometry. A mixed fixture whose assertions inherently require depth, model, stock-effect,
vertex/index, cube/volume sampling or another 3D result remains classified `3d`.
