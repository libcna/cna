# Software (CPU rasterizer) graphics renderer

## Status

The Software renderer is a **CPU-only rasterizer graphics renderer**, verified 2026-07-13. Select it
with:

```bash
cmake -S . -B cmake-build-software \
  -DCNA_GRAPHICS_RENDERER=SOFTWARE \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-software -j
```

No extra dependencies are needed — like `HEADLESS`, this renderer never touches SDL's video
subsystem, OpenGL, Vulkan, or any GPU library. It only needs the same SDL3/SDL3_image/SDL3_mixer
and `../sharp-runtime` checkout every other renderer already requires.

## What this renderer is for (and isn't)

Every other CNA renderer needs a real window and a real GPU context to run at all. `HEADLESS`
solves that for testing game *logic* by never touching a GPU either — but it never renders a real
pixel; `ReadBackbuffer()` just reports the last `Clear()` color for every pixel.

Software is different: it actually **rasterizes real triangles** into a CPU-owned RGBA8
framebuffer, entirely in software (a real edge-function rasterizer, real perspective-correct
attribute interpolation, a real per-pixel depth test). `GraphicsDevice::GetBackBufferData()`/
`ReadBackbuffer()` return genuinely correct pixels — no GPU, window, or display server involved at
any point. That makes it useful for:

- **Deterministic pixel tests** that need no GPU driver, display server, or Xvfb at all — unlike
  the existing EasyGL/BGFX/Vulkan golden-image tests (see `docs/graphics-renderer-feature-matrix.md`),
  which all need a real GPU context even under Xvfb.
- **Server/CI environments** with no GPU whatsoever.
- **Verifying basic XNA primitive/effect behavior** (does a triangle with these vertices, this
  effect, this blend state produce the pixels you'd expect) as a fast, portable reference.
- **Cross-renderer diagnostics**: comparing a real GPU renderer's output against this renderer's
  independently-implemented rasterizer can help localize whether a rendering bug is in shared
  code, a specific GPU renderer, or expected-but-undocumented behavior (see "Cross-renderer
  diagnostic" below, `plan_software.md` `SOFTWARE-61`/`SOFTWARE-84`).

**What it proves:** "this triangle, with this effect state, this texture, and this blend mode,
produces these exact pixels" — a real, independently-derived rendering result, not a fake one.
**What it is not:** a real-time gameplay renderer. There is no SIMD, no multithreading, no
tiling/binning — correctness and determinism are the goals, not speed (see `plan_software.md`
design decision 1).

## Writing a Software test

Like `HEADLESS`, a Software test is a normal `Game` subclass — the only difference is the renderer
selected at CMake configure time. See `modules/renderers/software/examples/software_smoke_test.cpp`,
`modules/renderers/software/examples/software_rasterizer_test.cpp`, and `modules/renderers/software/examples/software_effects_test.cpp` for full working
examples. The pattern:

```cpp
class MyPixelTest : public Game
{
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color::Black, 1.0f);

        // ... build a VertexBuffer, apply a BasicEffect, draw ...
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;   // real XNA default is false -- opt in explicitly
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);

        // Real, correct pixels -- no GPU involved at all.
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        // assert on pixel.getRProperty()/getGProperty()/getBProperty()/getAProperty()

        Exit();
    }
};
```

A reminder that both real games and this renderer's own tests need to remember: `BasicEffect`'s
`VertexColorEnabled` defaults to `false` in real XNA/FNA — a plain `BasicEffect` with no explicit
opt-in ignores vertex colors entirely (this renderer faithfully reproduces that, and it's exactly
what caused `Software_Rasterizer`'s tests to briefly fail while `DrawPrimitivesEx` was first wired
up — see `plan_software.md` `SOFTWARE-50`'s notes for the full story).

## Cross-renderer diagnostic (SOFTWARE-61/84)

`modules/graphics/examples/cross_renderer_diagnostic_scene.cpp` renders one simple, fully unlit (vertex-color-only,
no lighting) triangle and dumps the resulting 64x64 RGBA8 backbuffer to a raw file given as
`argv[1]`. It is deliberately renderer-agnostic (no `#ifdef`) and is built once per renderer that
needs it. `modules/graphics/examples/cross_renderer_diagnostic_compare.cpp` (built as `cna_diag_compare`, no
CNA/SHARP_RUNTIME dependency) diffs two such dumps and reports the max/mean per-channel absolute
difference, failing if the max exceeds a given tolerance.

This is **not** a single automated `ctest` — `CNA_GRAPHICS_RENDERER` is a compile-time choice, so a
single build only ever links one renderer; comparing two needs two separate builds' dumps. Run it
by hand (or from a script) like this, comparing `SOFTWARE` against `OPENGLES3`:

```bash
# 1. Software's dump (built as cna_diag_software in a SOFTWARE-configured build dir)
cmake --build cmake-build-software --target cna_diag_software cna_diag_compare
./cmake-build-software/cna_diag_software /tmp/software.rgba

# 2. EasyGL's dump (built as cna_diag_easygl in an OPENGLES3-configured build dir, needs a real
#    display -- a real desktop session or Xvfb)
cmake --build cmake-build-debug --target cna_diag_easygl
SDL_VIDEODRIVER=x11 DISPLAY=:0 ./cmake-build-debug/cna_diag_easygl /tmp/easygl.rgba

# 3. Compare (tolerance defaults to 40 if omitted)
./cmake-build-software/cna_diag_compare /tmp/software.rgba /tmp/easygl.rgba 40
```

Verified 2026-07-13: `SOFTWARE` vs. `OPENGLES3` on this canonical scene gives a max per-channel
diff of 1 (mean 0.139) — effectively identical, the residual being ordinary rounding noise, not a
real rendering discrepancy. The comparator was also checked against a deliberately corrupted dump
(one channel of one pixel flipped) to confirm it actually fails when the images genuinely differ,
rather than always passing.

## Known limitations (2026-07-13)

- **`TriangleList` only.** `TriangleStrip`/`LineList`/`LineStrip`/`PointListEXT` all throw a clear
  "only TriangleList is supported in v1" error rather than silently misrendering.
- **Vertex strides 16/20/24/32/48/52/56/68.** These are the complete canonical CNA table,
  including rigid PBR, coloured-skinned and skinned-PBR records. Tangents are consumed at their
  declared offsets but remain shading-inert because this CPU renderer does not evaluate a
  tangent-space PBR normal map. Any other stride throws a clear "unsupported vertex stride" error.
- **An unbound optional base texture is white.** `PbrEffect`, `SkinnedPbrEffect` and
  `SkinnedEffect` deliberately keep their textured program selected with no base map; SOFTWARE
  preserves the vertex/factor colour in that case, matching the white fallback used by native
  shader renderers. A missing second DualTexture map or environment cube remains a clear error.
- **No per-light diffuse lighting, no fog.** `BasicEffect`'s `EnableDefaultLighting()`/fog
  properties (and the equivalent lighting inputs on `EnvironmentMapEffect`/`SkinnedEffect`) have no
  effect on this renderer's output — only `VertexColorEnabled`, `TextureEnabled`/`Texture`, and
  `DiffuseColor`/`Alpha` are read; the "lit" base color is always just
  `vertexColor*diffuseColor*texture0`, with no per-light `NdotL` sum, ambient, or emissive term.
- **`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect` are supported** (`SOFTWARE-82`),
  minus the lighting caveat above:
  - `DualTextureEffect`: real second-texture sampling, FNA's own
    `color.rgb*=2; color *= overlay*diffuse` formula. Both textures reuse the same UV (this
    renderer has no genuine 2-UV vertex format — an established simplification, matching this
    codebase's own Vulkan `dual_texture3d` shaders' precedent).
  - `EnvironmentMapEffect`: real 6-face RGBA8 cube map storage (`CreateTextureCube` now returns a
    working renderer, no mipmaps) and a real reflection vector (`reflect(-eyeVector, worldNormal)`)
    sampled against it, with Fresnel edge-weighting and the specular-tint term. The normal is
    transformed by `World` directly rather than the mathematically-correct
    `WorldInverseTranspose` — exact for uniform-scale/no-shear `World` matrices, a real
    simplification for non-uniform scale. Cube sampling is nearest-neighbor only (no cross-face
    bilinear filtering at cube seams), unlike the bilinear 2D texture sampling below.
  - `SkinnedEffect`: real per-vertex bone-transform blending (up to 4 weighted bones,
    `WeightsPerVertex`-gated) applied to the vertex position before the standard
    World\*View\*Projection transform.
- **No MRT, no MSAA, no mipmapping, no 3D textures, no render-target cube maps.**
  `CreateRenderTargetCube`/`CreateTexture3D` still return `nullptr` (the shared `IGraphicsRenderer`
  default — this renderer doesn't override them); only plain (non-render-target) `TextureCube`s are
  real.
- **Only two blend modes are distinguished**: `Opaque` (exact preset match: `colorSrcBlend=One`,
  `colorDstBlend=Zero`) and a single simplified "over" alpha-composite formula for everything else
  (`AlphaBlend`/`NonPremultiplied`/`Additive`-ish presets all get treated the same way). This is a
  real, deliberate v1 simplification (`plan_software.md` design decision 7), not a full
  blend-equation interpreter.
- **Bilinear texture sampling always on** (`SOFTWARE-80`) — standard half-texel-offset bilinear
  with clamp-to-edge at the boundaries, but no mipmapping and no real texture address modes
  (`Wrap`/`Clamp`/`Mirror` all behave the same: UVs are simply clamped to the texture's own
  bounds). Not gated by `SamplerState.Filter`.
- **Backface culling respects `RasterizerState.CullMode`** (`SOFTWARE-81`) — `None`/
  `CullClockwiseFace`/`CullCounterClockwiseFace` are all honored, including by
  `SpriteBatch`'s own quads (matching real FNA, whose `SpriteBatch` defaults to
  `CullCounterClockwise` rather than `CullNone`).
- **Real near-plane polygon clipping** (`SOFTWARE-83`) — a triangle crossing the near plane
  (`clip.W <= ~0`) is split into 1-2 new triangles at the clip plane (interpolating position and
  color/UV together), rather than the whole triangle being discarded. Clipping still happens at
  the camera's eye plane specifically (`clip.W <= ~0`), not at the projection's configured near
  clip distance — a vertex clipped there necessarily lands at an enormous (but finite, correct)
  screen position after the perspective divide.
- **`SpriteBatch` honors a custom `GraphicsDevice.Viewport`** (`REMED-GFX-073`) — sprite
  coordinates are viewport-local (sprite `(0,0)` = the viewport's top-left), the result is placed at
  `Viewport.X/Y`, and pixels outside the viewport rectangle are clipped, matching real XNA/FNA and
  the GPU renderers' GFX-072 contract. The default full-target viewport is byte-identical to the
  previous behavior. Two related gaps remain (their own tasks): the **3D** raster path still maps NDC
  over the full framebuffer and ignores a custom viewport's origin/size/depth-range
  (`REMED-GFX-079`), and `ScissorRectangle` is not yet honored on any path
  (`SetScissorRect` is a no-op — `REMED-GFX-080`). `Viewport.MinDepth/MaxDepth` are stored but not
  consumed (the sprite path uses `layerDepth` directly).
- **Custom `ShaderEffect` (arbitrary GLSL/HLSL/WGSL source) compiles but doesn't actually execute**
  — mirrors `HEADLESS-16`'s own precedent exactly: the source is accepted without compiling, and
  only effects whose `FillGpuDrawParams()` output matches this renderer's fixed `BasicEffect`-subset
  pixel-shading path will render correctly.
- **`Present()` is a no-op**; there is no way to visually inspect a Software-rendered frame on
  screen in this renderer's current form. An opt-in "blit the CPU framebuffer to a real window"
  mode is a reasonable future addition (`plan_software.md` design decision 3) but isn't needed for
  this renderer's actual value proposition (deterministic, GPU-free pixel tests).

See `plan_software.md` for the full task-by-task status and design rationale.
