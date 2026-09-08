# Software (CPU rasterizer) graphics renderer

## Status

The Software renderer is a **CPU-only rasterizer graphics renderer**, verified 2026-09-08. Select it
with:

```bash
cmake -S . -B cmake-build-software \
  -DCNA_GRAPHICS_RENDERER=SOFTWARE \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-software --parallel 2
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
attribute interpolation, and real per-sample depth/stencil tests). `GraphicsDevice::GetBackBufferData()`/
`ReadBackbuffer()` return genuinely correct pixels — no GPU, window, or display server involved at
any point. Triangle lists and strips, line lists and strips, and the existing `PointListEXT` path
all use the CPU rasterizer; triangle strips preserve XNA's alternating winding across indexed and
non-indexed user/buffer draws. That makes it useful for:

- **Deterministic pixel tests** that need no GPU driver, display server, or Xvfb at all — unlike
  the existing EasyGL/BGFX/Vulkan golden-image tests (see `docs/graphics-renderer-feature-matrix.md`),
  which all need a real GPU context even under Xvfb.
- **Server/CI environments** with no GPU whatsoever.
- **Verifying basic XNA primitive/effect behavior** (does a triangle with these vertices, this
  effect, this blend state produce the pixels you'd expect) as a fast, portable reference.
- **Cross-renderer diagnostics**: comparing a real GPU renderer's output against this renderer's
  independently-implemented rasterizer can help localize whether a rendering bug is in shared
  code, a specific GPU renderer, or expected-but-undocumented behavior (see "Cross-renderer
  diagnostic" below, `plans/plan_software.md` `SOFTWARE-61`/`SOFTWARE-84`).

**What it proves:** "this triangle, with this effect state, this texture, and this blend mode,
produces these exact pixels" — a real, independently-derived rendering result, not a fake one.
**What it is not:** a real-time gameplay renderer. There is no SIMD, no multithreading, no
tiling/binning — correctness and determinism are the goals, not speed (see `plans/plan_software.md`
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
up — see `plans/plan_software.md` `SOFTWARE-50`'s notes for the full story).

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

## Known limitations (2026-09-08)

- **Vertex input is declaration-driven.** `VertexElementUsage`/usage index selects attributes
  across one or multiple streams, and all 12 XNA `VertexElementFormat` values are decoded at their
  declared offsets. Reordered, padded, application-defined and non-canonical-stride layouts,
  binding offsets and 16/32-bit indexed user draws are covered by one deterministic public fixture
  that passes 16/16 on both Software and EasyGL. EasyGL's stock path now supplies the matching
  FNA3D-style native format conversion instead of requiring canonical byte formats or selecting an
  effect variant from an accidentally colliding stride. Only CNAEXT's old empty-declaration
  `VertexBuffer(device,count)` convenience path retains canonical stride inference for
  compatibility.
- **Classic indexed instancing runs entirely on the CPU** (`SOFTWARE-129`). Per-instance
  declarations concatenate into the same four-column stock-effect matrix contract as EasyGL. Each
  stream advances from its own binding offset by `floor(instanceIndex / InstanceFrequency)` while
  base vertex affects only per-vertex streams. Split geometry/matrix streams, both index widths,
  arbitrary positive frequencies, dynamic updates, queued lifetime and ordinary/instanced state
  transitions share one renderer-neutral public test corpus. This is deterministic CPU expansion,
  not delegation to EasyGL or a GPU.
- **Static and dynamic vertex/index buffers share EasyGL's public contract** (`SOFTWARE-109`).
  Source-window uploads, `None`/`Discard`/`NoOverwrite`, repeated mutation, typed readback,
  `BufferUsage`, missing bindings, disposed-resource guards and draw-range validation pass the same
  nine renderer-neutral fixtures on Software and EasyGL (122/122 checks each). Destination-window
  CNAEXT overloads and exact offset/base/index-width behavior have additional shared/unit coverage.
- **An unbound optional base texture is white.** `PbrEffect`, `SkinnedPbrEffect` and
  `SkinnedEffect` deliberately keep their textured program selected with no base map; SOFTWARE
  preserves the vertex/factor colour in that case, matching the white fallback used by native
  shader renderers. A missing second DualTexture map or environment cube remains a clear error.
- **`BasicEffect` lighting is complete** (`SOFTWARE-113`). Software evaluates FNA's three-light
  Blinn–Phong equation with ambient, diffuse, emissive and specular material terms, including
  `EnableDefaultLighting`, per-light enable/colour/direction, `SpecularPower`, texture, vertex
  colour and alpha ordering. The XNA-default per-vertex path saturates and perspective-interpolates
  its diffuse/specular outputs; `PreferPerPixelLighting=true` re-evaluates normalized world-space
  inputs per fragment. Normals use inverse-transpose `World`, including non-uniform scale.
- **`EnvironmentMapEffect` lighting and reflection semantics are complete** (`SOFTWARE-114`).
  Its ambient/emissive/material colour and all three directional diffuse lights use FNA's vertex
  equation. Inverse-transpose normals drive both lighting and reflection under non-uniform World
  transforms; eye-relative reflection and Fresnel are evaluated per vertex, clipped and
  perspective-interpolated. D3D9 COLOR saturation bounds material and environment-amount outputs
  before interpolation. Texture/effect alpha scales the cube lerp target and environment-specular
  term. The shared Software/EasyGL contract passes 14/14 with a two-byte tolerance.
- **`SkinnedEffect` lighting is complete** (`SOFTWARE-115`). Software applies 1/2/4 weighted bones
  to positions and normals, then evaluates the same classic ambient/diffuse/emissive/specular
  equation as FNA with all three lights, default lighting, `SpecularPower`, texture and alpha.
  FNA's normal order is preserved exactly: direct weighted-bone 3x3 followed by inverse-transpose
  `World`. Both XNA's default per-vertex path and `PreferPerPixelLighting=true` are implemented.
  The shared Software/EasyGL contract passes 19/19 with a two-byte tolerance; the oracle also
  corrected EasyGL's classic bone-normal path without changing its CNAEXT PBR/glTF shaders.
- **Classic stock-effect fog is complete** (`SOFTWARE-112`). `BasicEffect`, `AlphaTestEffect`,
  `DualTextureEffect`, `EnvironmentMapEffect` and `SkinnedEffect` use FNA's view-space fog vector,
  including transformed World/View matrices, the degenerate start=end case and SkinnedEffect's
  post-bone position. The per-vertex factor is clipped and perspective-interpolated, and fog mixes
  final RGB toward `FogColor * outputAlpha` after the stock effect's texture/environment work and
  alpha test but before blending.
- **`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect` are supported** (`SOFTWARE-82`,
  completed by `SOFTWARE-114..116`):
  - `DualTextureEffect`: real second-texture sampling, FNA's own
    `color.rgb*=2; color *= overlay*diffuse` formula. Declaration-driven `TextureCoordinate0/1`
    remain independent through clipping and perspective interpolation; each texture also has its
    own sampler and mip footprint. The shared Software/EasyGL contract passes 122/122 across every
    static/dynamic, indexed/non-indexed and user/buffer draw path.
  - `EnvironmentMapEffect`: real six-face RGBA8 cube storage and mip chains, sampler-slot-1
    point/linear/min/mag/mip and address behavior, FNA vertex lighting, inverse-transpose normals,
    reflection/Fresnel, alpha-scaled lerp/specular and final fog semantics.
  - `SkinnedEffect`: real `WeightsPerVertex`-gated 1/2/4-bone position and normal blending,
    inverse-transpose World normals, full classic lighting in vertex/pixel modes, texture, alpha
    and fog.
- **MRT and render-target cube maps are real.** Ordinary `Texture2D` and `TextureCube`
  mip storage and sampling are real. `Texture3D` has exact CPU RGBA8 storage for its full XNA mip
  chain plus bounded full/partial `SetData` and `GetData`; this storage capability does not imply
  CNAEXT `ShaderEffect` volume sampling.
  `RenderTarget2D` does implement an actual four-sample CPU colour plane and generated mip levels.
  Unbind resolves the samples before mip generation; a level-zero `GetData` while the target is
  active snapshots the live samples without unbinding it, while generated levels remain unavailable
  until the pass ends. Requests other than 0 or 4 samples still fall back to single-sample storage.
  `RenderTargetCube` owns six isolated color faces, an XNA-style depth/stencil attachment shared
  across face switches, face-local 4x resolve and generated box-filter mips. Its faces support
  exact partial transfer and can be sampled after rendering by `EnvironmentMapEffect` through the
  same CPU cube sampler as plain `TextureCube`. Up to four simultaneous 2D/cube-face bindings own
  distinct CPU color planes. Clear/discard, resolve and mip generation visit every attachment;
  only slot zero owns depth/stencil and receives the `COLOR0` output emitted by classic stock
  effects. Higher attachments therefore retain their own explicit clear/preserved contents. This
  includes mixed 2D/cube sets and distinct faces of one cube. The shared 22-check contract passes
  unchanged on both Software and EasyGL, including face-local 4x resolve, independent mip chains,
  cube depth ownership and bound-cube destruction with live-peer finalization (`SOFTWARE-135`).
- **Classic float render targets preserve their declared storage** (`SOFTWARE-146`).
  `Single`, `Vector2`, `Vector4`, `HalfSingle`, `HalfVector2`, `HalfVector4` and `HdrBlendable`
  retain negative and above-one components through clear, raster writes, independent RGB/alpha
  blending, 4x resolve, float-domain mip generation, sampling and exact typed transfers. One- and
  two-channel targets expose missing colour components as one; half formats quantize at every
  target store. `RenderTargetCube` uses the same storage independently for all six faces and can
  feed `EnvironmentMapEffect` without first narrowing to RGBA8. Known classic formats whose target
  storage is not yet implemented are rejected before construction instead of being silently
  substituted. The shared Software/EasyGL contract includes distinguishing 2D and cube samples,
  additive HDR output, MSAA, depth, mips and full/partial typed transfers.
- **Classic packed-16 `Texture2D` formats are real** (`SOFTWARE-140`). `Bgr565`, `Bgra5551`
  and `Bgra4444` retain their exact two-byte XNA layout for full, partial and mip transfers while
  each supplied level is decoded into a separate RGBA8 CPU sampling plane. The same public transfer
  contract and exact sampled-draw oracle pass on Software and Mesa EasyGL.
- **Classic DXT textures are real CPU resources** (`SOFTWARE-141`, `SOFTWARE-145`). DXT1, DXT3 and DXT5 retain
  exact padded 4x4 block streams at every supplied mip for full and block-aligned partial readback,
  including NPOT right/bottom tails, while `DxtUtil` produces the RGBA8 sampling planes. DDS/XNB
  content keeps its compressed format and complete mip chain instead of being expanded by the
  loader. Shared transfer and unchanged sampled-draw probes pass on Software and Mesa EasyGL; the
  same evidence also repaired EasyGL's previously missing compressed readback/context-restoration
  shadow. `TextureCube` retains independent blocks per face and mip as well; shared tests prove
  DXT1/3/5 decode, a non-zero partial block update, a non-zero face mip and real
  `EnvironmentMapEffect` sampling.
- **Every classic uncompressed `Texture2D` format has real typed storage and sampling**
  (`SOFTWARE-142`). In addition to Color and the packed-16 formats, Software accepts
  `NormalizedByte2/4`, `Rgba1010102`, `Rg32`, `Rgba64`, `Alpha8`, `Single`, `Vector2/4`,
  `HalfSingle`, `HalfVector2/4` and `HdrBlendable` at the XNA profile tiers that permit them.
  Exact original bytes back full/partial/mip `GetData`; a separate float RGBA plane prevents signed
  normalized or HDR values from being clipped to RGBA8 before shader math. One- and two-channel
  colour formats expose XNA's missing components as one, while `Alpha8` follows FNA's
  `(0,0,0,A)` mapping. A shared Software/EasyGL public contract checks all typed transfers, all
  point-sampled channel layouts, and `BasicEffect` probes that would fail if `Single(2)` or
  `NormalizedByte4(-0.5)` were narrowed early. Non-XNA `*EXT` texture formats remain outside this
  campaign rather than being accepted as Color.
- **Every XNA-permitted ordinary `TextureCube` format has the same exact storage and sampling**
  (`SOFTWARE-145`, `SOFTWARE-149`, `SOFTWARE-150`). Each of the six faces and every declared mip
  independently retains Color, DXT1/3/5, normalized-integer, binary32 or binary16 bytes. Typed
  full/start/rectangle/mip transfers preserve those bytes, while the cube sampler expands missing
  channels and keeps negative or above-one values until effect math. Shared `EnvironmentMapEffect`
  probes distinguish the packed layouts and prove that `2.0 * 0.25` reaches the framebuffer as
  0.5 rather than being narrowed through RGBA8. `NormalizedByte2/4` remain rejected because the
  measured XNA cube-format table excludes them on both Reach and HiDef.
- **NPOT textures use the ordinary complete texture path** (`SOFTWARE-144`). Exact public transfer
  coverage includes 3x5 full and partial-row updates plus the 1x2/1x1 mip tail. The unchanged 3x5
  sampled-row scene passes on Software and EasyGL, while the shared sampler contracts cover NPOT
  point/linear magnification, minification, per-resource dimensions and mip selection. There is no
  power-of-two padding, row-alignment special case or alternate sampling rule in the CPU backend.
- **Complete classic `BlendState` equations are applied.** RGB and alpha source/destination
  factors and functions are independent; `BlendFactor`, `ColorWriteChannels` and
  `MultiSampleMask` are honored. All four MRT write-channel masks are retained; the classic stock
  effect paths consume slot zero's mask because their sole fragment output is `COLOR0`.
- **Classic 2D/cube sampler state is applied.** Point/linear minification and magnification,
  point/linear mip selection, independent U/V Wrap/Clamp/Mirror and per-slot state are covered by
  shared contracts. `TextureFilter::Anisotropic` computes the directional texel footprint, selects
  mip LOD from its minor axis and averages up to 16 taps along its major axis; `MaxAnisotropy`,
  per-slot independence, SpriteBatch forwarding and address interaction are shared-tested against
  EasyGL by `SOFTWARE-117`.
- **Backface culling respects `RasterizerState.CullMode`** (`SOFTWARE-81`) — `None`/
  `CullClockwiseFace`/`CullCounterClockwiseFace` are all honored, including by
  `SpriteBatch`'s own quads (matching real FNA, whose `SpriteBatch` defaults to
  `CullCounterClockwise` rather than `CullNone`).
- **Complete homogeneous frustum clipping** (`SOFTWARE-106`) — points, lines and triangles are
  clipped before perspective divide against XNA/Direct3D's six clip planes
  (`-W <= X,Y <= W`, `0 <= Z <= W`). Polygon clipping interpolates all active varyings, preserves
  winding, supports multi-plane results larger than the old near-only quad, and keeps internal fan
  diagonals out of wireframe output.
- **XNA/D3D raster coverage** (`SOFTWARE-107`, `SOFTWARE-131`, `SOFTWARE-136`) — 3D viewport mapping reproduces
  Direct3D 9's integer pixel-center convention, and exact triangle boundaries use the top-left fill
  rule. Adjacent triangles therefore own a shared edge exactly once regardless of draw order or
  submitted winding, including the Software renderer's four coverage samples; manual diagonal
  exceptions are no longer part of solid rasterization. Algebraically equivalent affine-difference
  barycentric interpolation also keeps a constant vertex varying byte-identical across a primitive,
  instead of letting floating-point weight-sum drift create one-byte bands. As on EasyGL, the
  near-half-pixel geometry displacement is a single-sample compatibility rule and is omitted for a
  multisampled destination;
  applying it to quarter-pixel 4x locations would incorrectly remove outer-edge coverage.
- **Complete `AlphaTestEffect` comparisons** (`SOFTWARE-111`) — all eight XNA `CompareFunction`
  values use FNA's half-byte threshold encoding after texture, vertex and effect alpha are
  multiplied. A rejected fragment is discarded before colour, depth or stencil writes; a null
  texture contributes opaque white, matching the stock-effect path on EasyGL.
- **Complete XNA stencil state** (`SOFTWARE-121`) — the CPU fragment paths honor all eight
  comparisons and operations, read/write masks, `GraphicsDevice.ReferenceStencil`, the distinct
  counter-clockwise tuple, and the stencil-fail/depth-fail/pass ordering. This applies to colored
  and stock-effect triangles, strips, lines, points, wireframe and SpriteBatch, with alpha discard
  occurring first.
- **Sample-correct 4x MSAA** (`SOFTWARE-110`) — color, depth and stencil are stored and tested
  independently at four rotated 2x2 coverage locations. `MultiSampleMask` gates those same samples,
  triangle depth is evaluated at each covered location, and resolve deterministically averages the
  surviving colors. The renderer-neutral mask/depth/stencil contract also exposed and repaired
  EasyGL's former silent omission of non-default sample masks.
- **`OcclusionQuery` is an exact CPU raster query** (`SOFTWARE-122`). `Begin` starts a fresh
  measurement, `End` completes synchronously, and `PixelCount` is the number of raster samples
  surviving clipping, scissor, geometric coverage, `MultiSampleMask`, alpha test, depth and
  stencil. Color-write masks do not suppress visibility, 4× MSAA counts individual selected
  samples, and deferred SpriteBatch draws participate in the same interval. The three existing
  EasyGL public lifecycle/visible/occluded scenes compile unchanged for Software, while the CPU
  exact contract passes 43/43 checks.
- **`SpriteBatch` honors a custom `GraphicsDevice.Viewport`** (`REMED-GFX-073`) — sprite
  coordinates are viewport-local (sprite `(0,0)` = the viewport's top-left), the result is placed at
  `Viewport.X/Y`, and pixels outside the viewport rectangle are clipped, matching real XNA/FNA and
  the GPU renderers' GFX-072 contract. The **3D** path also honors X/Y, Width/Height, raster clipping
  and `MinDepth/MaxDepth` (`REMED-GFX-079`, 25/25 focused checks), and an enabled
  `ScissorRectangle` intersects both paths in target space (`REMED-GFX-080`). The default
  full-target viewport remains byte-identical to the earlier behavior.
- **`SpriteBatch` destinations remain sub-pixel precise** (`SOFTWARE-137`) — Vector2 positions,
  scalar/non-uniform scales and per-glyph DrawString rectangles reach CPU quad generation as floats
  rather than being truncated by the renderer interface's compatibility fallback. A shared
  Software/EasyGL fixture compares each direct draw with the equivalent `Begin` transform using
  byte-exact full-target images.
- **The classic SpriteBatch/SpriteFont parity corpus executes on the CPU** (`SOFTWARE-138`).
  Eighteen renderer-independent scenes shared with EasyGL cover flips, rotation/origin, both scale
  overloads, source rectangles, layer sorting, transforms, render targets, viewports, scissor,
  blend-state interactions, glyph placement, spacing, newline, fallback and text transforms. The
  complete `Software_Sprite*` CTest selection passes 22/22.
- **SpriteBatch has a real disposable lifecycle** (`SOFTWARE-139`). Explicit `Dispose()` releases
  the CPU renderer and every queued texture handle immediately, is idempotent, and makes later
  Begin/End/Draw/DrawString calls fail with `ObjectDisposedException` rather than continuing to
  render through a resource that only claimed to be disposed. FNA's internal SpriteEffect contract
  (matrix-transformed texture × vertex color) is implemented directly by the same CPU quad path.
- **Classic `Model.Draw()` is end-to-end tested on the CPU** (`SOFTWARE-125`). Nine unchanged
  EasyGL fixtures pass through Software for rigid and skinned models, root/child mesh placement,
  per-mesh effects, texture materials, JSON/binary content loading, 16/32-bit indices, imported
  skeleton data and visible animation-clip deformation. This proof exercises public Model and
  ContentManager orchestration rather than stopping at isolated SkinnedEffect triangles.
- **Custom `ShaderEffect` (arbitrary GLSL/HLSL/WGSL source) compiles but doesn't actually execute**
  — mirrors `HEADLESS-16`'s own precedent exactly: the source is accepted without compiling, and
  only effects whose `FillGpuDrawParams()` output matches one of this renderer's fixed stock-effect
  CPU paths will render correctly.
- **`Present()` is a no-op**; there is no way to visually inspect a Software-rendered frame on
  screen in this renderer's current form. An opt-in "blit the CPU framebuffer to a real window"
  mode is a reasonable future addition (`plans/plan_software.md` design decision 3) but isn't needed for
  this renderer's actual value proposition (deterministic, GPU-free pixel tests).

See `plans/plan_software.md` for the full task-by-task status and design rationale.
