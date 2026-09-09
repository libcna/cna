# Software (CPU rasterizer) graphics renderer

## Status

The Software renderer is a **CPU-only rasterizer graphics renderer**, verified 2026-09-09. Select it
with:

```bash
cmake -S . -B cmake-build-software \
  -DCNA_GRAPHICS_RENDERER=SOFTWARE \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-software --parallel 2
```

No extra dependencies are needed. The renderer descriptor requests neither SDL's video subsystem
nor a native window, and the implementation never uses OpenGL, Vulkan, or any GPU library. Shared
`GraphicsAdapter` enumeration may initialize and retain platform video when a display is available
so its public display IDs stay valid; Software does not depend on that succeeding and remains fully
usable without a display server. The shared pin follows ambient-platform transitions atomically
(`SOFTWARE-157`), acquiring the replacement before releasing the predecessor; it is never counted
as ownership by the windowless Software device. The renderer only needs the same
SDL3/SDL3_image/SDL3_mixer and
`../sharp-runtime` checkout every other renderer already requires.

The final adversarial challenge found one material classic-XNA boundary: Software does not execute
compiled Direct3D 9 Effect Framework bytecode, while EasyGL does when built with
`CNA_EASYGL_COMPILED_EFFECTS=ON`. Software truthfully reports
`GraphicsCapability::CompiledEffects=false` and rejects those bytes. This is distinct from the
excluded CNAEXT `ShaderEffect` API; `SOFTWARE-161` records the assessment and
`SOFTWARE-162..165` the CPU shader-interpreter backlog.

The same challenge also confirmed a renderer-wide public-API hole: CNA stores
`GraphicsDevice.VertexTextures` and `VertexSamplerStates`, but no renderer contract consumes them.
FNA applies those collections before drawing, and EasyGL currently rejects even compiled vertex
shaders that declare samplers. `SOFTWARE-167` records the proof and `SOFTWARE-168` the shared
binding work; Software execution additionally depends on the compiled-effect phases.

## What this renderer is for

GPU-backed CNA renderers need a graphics context and normally a window or offscreen platform
surface. `HEADLESS` avoids that for testing game *logic*, but never renders a real pixel;
`ReadBackbuffer()` only reports the last `Clear()` color for every pixel.

Software is different: it actually **rasterizes real triangles** into a CPU-owned RGBA8
framebuffer, entirely in software (a real edge-function rasterizer, real perspective-correct
attribute interpolation, and real per-sample depth/stencil tests). `GraphicsDevice::GetBackBufferData()`/
`ReadBackbuffer()` return genuinely correct pixels without a GPU or native window and with no
display-server requirement. Triangle lists and strips, line lists and strips, and the existing `PointListEXT` path
all use the CPU rasterizer; triangle strips preserve XNA's alternating winding across indexed and
non-indexed user/buffer draws. That makes it useful for:

- **Deterministic pixel tests** that need no GPU driver, display server, or Xvfb at all — unlike
  the existing EasyGL/BGFX/Vulkan golden-image tests (see `docs/graphics-renderer-feature-matrix.md`),
  which all need a real GPU context even under Xvfb.
- **Server/CI environments** with no GPU whatsoever.
- **Running the stock-effect classic XNA/Core graphics subset on the CPU**, including complete
  graphics state for those paths, textures/cubes/targets/MRT, SpriteBatch/SpriteFont, queries and
  stock-effect Model drawing.
- **Cross-renderer diagnostics**: comparing a real GPU renderer's output against this renderer's
  independently-implemented rasterizer can help localize whether a rendering bug is in shared
  code, a specific GPU renderer, or expected-but-undocumented behavior (see "Cross-renderer
  diagnostic" below, `plans/plan_software.md` `SOFTWARE-61`/`SOFTWARE-84`).

Software is a first-class stock-effect correctness renderer: an XNA-style CNA application that does
not use compiled custom Effects can rely on the verified CPU contract without a GPU. It is not
optimized to match GPU throughput—there is no SIMD, multithreading or tile binning—because
determinism and fidelity remain the primary goals (see `plans/plan_software.md` design decision 1).

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

## Cross-renderer differential infrastructure (SOFTWARE-61/84/103/156)

The original raw-dump diagnostic below remains a useful small end-to-end smoke test. The main
parity corpus is now much broader: renderer-neutral public programs are compiled independently in
the Software and EasyGL configurations and assert exact pixels, transfers, resource properties,
exception behavior and query counts. They cover raster/sample rules, declarations and buffers,
textures/samplers/formats, targets/cubes/MRT, complete stock-path graphics state, classic stock
effects, SpriteBatch/SpriteFont, lifecycle/reset, occlusion and stock-effect `Model.Draw()`. Tight non-zero tolerances are
local to tests with a documented normalized-conversion or raster-rounding reason; most contracts
use exact equality.

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

# 3. Compare (the one-byte tolerance is explicit here and is also the default)
./cmake-build-software/cna_diag_compare /tmp/software.rgba /tmp/easygl.rgba 1
```

Verified 2026-07-13: `SOFTWARE` vs. `OPENGLES3` on this canonical scene gives a max per-channel
diff of 1 (mean 0.139) — effectively identical, the residual being ordinary rounding noise, not a
real rendering discrepancy. The comparator was also checked against a deliberately corrupted dump
(one channel of one pixel flipped) to confirm it actually fails when the images genuinely differ,
rather than always passing. `SOFTWARE-156` reduced the historical default tolerance of 40 to the
measured one-byte bound; a wider tolerance now has to be an explicit, evidence-backed choice.

## Current XNA/Core capabilities and intentional boundaries (2026-09-08)

- **Classic compiled XNA Effects are not supported** (`SOFTWARE-161`). The public bytecode
  constructor accepts XNA/FNA Direct3D 9 Effect Framework bytes only on renderers whose real
  runtime implements them. Opt-in EasyGL passes 37 tests covering reflection, techniques/passes,
  parameters, draw pixels, state, SpriteBatch, instancing, multi-stream input and 2D/cube/volume
  sampling. Software has no CPU vertex/pixel shader interpreter, advertises
  `CompiledEffects=false`, and rejects those same valid bytes. `SOFTWARE-162..165` is the phased
  implementation backlog; CNAEXT `ShaderEffect` is a separate excluded API.
- **Public vertex-stage texture/sampler collections are inert renderer-wide** (`SOFTWARE-167`).
  `GraphicsDevice.VertexTextures` and `VertexSamplerStates` have the correct public shape and
  resource-disposal bookkeeping, but common code has no operation that publishes their contents
  to any renderer. FNA does so on every dirty draw-state application. `SOFTWARE-168` owns the
  shared binding contract; Software's actual vertex sampling also depends on `SOFTWARE-163/164`.
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
- **Unlit classic stock material output matches XNA's vertex boundary** (`SOFTWARE-153`).
  `BasicEffect`, `AlphaTestEffect` and `DualTextureEffect` fold material alpha into diffuse colour
  exactly as FNA does, multiply any enabled vertex colour, and saturate the D3D9 `COLOR0` value
  before clipping and interpolation. Texture sampling follows that boundary, so a material value
  of two multiplied by a 0.4 texture produces 0.4, not 0.8. The shared 8/8 public contract also
  covers `(DiffuseColor+EmissiveColor)*Alpha`, disabled vertex colour, disabled texture, real
  texture and the opaque-white null-texture fallback on both Software and EasyGL.
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
  either CNAEXT `ShaderEffect` or classic compiled-Effect volume sampling. The latter is an
  in-scope gap tracked by `SOFTWARE-159`/`SOFTWARE-164` because opt-in EasyGL demonstrably supports
  it.
  `RenderTarget2D` does implement an actual four-sample CPU colour plane and generated mip levels.
  Unbind resolves the samples before mip generation; a level-zero `GetData` while the target is
  active snapshots the live samples without unbinding it, while generated levels remain unavailable
  until the pass ends. Requests of four or more clamp to the real four-sample mode; one and two
  remain single-sample rather than being promoted beyond the requested quality.
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
- **Every classic renderable target format preserves its declared storage** (`SOFTWARE-143`,
  `SOFTWARE-146`, `SOFTWARE-151`). `RenderTarget2D` and `RenderTargetCube` accept Color,
  `Rgba1010102`, `Rg32`, `Rgba64`, `Single`, `Vector2`, `Vector4`, `HalfSingle`, `HalfVector2`,
  `HalfVector4` and `HdrBlendable`. The three normalized layouts quantize exactly at 10/10/10/2,
  16/16 and 16/16/16/16 bits; `Rg32` exposes missing B/A components as one. Float layouts
  retain negative and above-one components through clear, raster writes, independent RGB/alpha
  blending, 4x resolve, float-domain mip generation, sampling and exact typed transfers. One- and
  two-channel targets expose missing colour components as one; half formats quantize at every
  target store. `RenderTargetCube` uses the same storage independently for all six faces and can
  feed `EnvironmentMapEffect` without first narrowing to RGBA8. Classic packed-16, compressed,
  signed-normalized and Alpha8 targets are not XNA-renderable and reject before construction
  instead of being silently substituted. Shared Software/EasyGL contracts include distinguishing
  2D and cube samples, additive HDR output, exact normalized clear/readback, MSAA, depth, mips and
  full/partial typed transfers.
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
- **Complete classic `BlendState` equations are applied** (`SOFTWARE-152`). RGB and alpha
  source/destination factors and functions are independent; `BlendFactor`, `ColorWriteChannels`
  and `MultiSampleMask` are honored. A shared public target-readback matrix covers all 13 source
  factors, every GL/FNA-valid destination factor and all five RGB/alpha functions in 35/35 checks
  on both Software and EasyGL. Seven unchanged EasyGL preset/separate-state scenes also pass on
  Software. All four MRT write-channel masks are retained; the classic stock effect paths consume
  slot zero's mask because their sole fragment output is `COLOR0`. The independent
  `GraphicsDevice.MultiSampleMask` property is also live (`SOFTWARE-166`): mask 0/1/all output,
  restoration and synchronization after assigning a whole BlendState pass the expanded 18/18
  Software/desktop-EasyGL MSAA contract.
- **Classic 2D/cube sampler state is applied.** Point/linear minification and magnification,
  point/linear mip selection, independent U/V Wrap/Clamp/Mirror and per-slot state are covered by
  shared contracts. `TextureFilter::Anisotropic` computes the directional texel footprint, selects
  mip LOD from its minor axis and averages up to 16 taps along its major axis; `MaxAnisotropy`,
  per-slot independence, SpriteBatch forwarding and address interaction are shared-tested against
  EasyGL by `SOFTWARE-117`. `SOFTWARE-158` additionally proves that
  `MipMapLevelOfDetailBias` shifts the computed LOD and `MaxMipLevel` applies afterward as the
  most-detailed permitted level (FNA3D's minimum-LOD interpretation), including combined state,
  transitions and SpriteBatch. Software passes all 97 checks; OpenGL ES runs the 91 representable
  checks because, like FNA3D, EasyGL cannot express texture LOD bias on that profile.
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
  texture contributes opaque white, matching the stock-effect path on EasyGL. Its common material
  output is also saturated per vertex before texture sampling (`SOFTWARE-153`).
- **Complete XNA stencil state** (`SOFTWARE-121`) — the CPU fragment paths honor all eight
  comparisons and operations, read/write masks, `GraphicsDevice.ReferenceStencil`, the distinct
  counter-clockwise tuple, and the stencil-fail/depth-fail/pass ordering. This applies to colored
  and stock-effect triangles, strips, lines, points, wireframe and SpriteBatch, with alpha discard
  occurring first.
- **Sample-correct 4x MSAA** (`SOFTWARE-110`, `SOFTWARE-160`) — color, depth and stencil are stored and tested
  independently at four rotated 2x2 coverage locations. `MultiSampleMask` gates those same samples,
  triangle depth is evaluated at each covered location, and resolve deterministically averages the
  surviving colors. `RasterizerState.MultiSampleAntiAlias=false` leaves four-sample storage intact
  but evaluates triangle coverage/depth once at the pixel center and replicates that result to the
  enabled samples; the shared boundary fixture proves the result has no partially covered pixels
  and that a true/false/true transition restores the exact independently sampled image. The same
  contract exposed and repaired EasyGL's former silent omission of non-default sample masks and now
  maps this rasterizer toggle to desktop `GL_MULTISAMPLE`; OpenGL ES has no equivalent.
- **`OcclusionQuery` is an exact CPU raster query** (`SOFTWARE-122`, `SOFTWARE-199`). `Begin` starts a fresh
  measurement, `End` completes synchronously, and `PixelCount` is the number of raster samples
  surviving clipping, scissor, geometric coverage, `MultiSampleMask`, alpha test, depth and
  stencil. Color-write masks do not suppress visibility, 4× MSAA counts individual selected
  samples, and deferred SpriteBatch draws participate in the same interval. The three existing
  EasyGL public lifecycle/visible/occluded scenes compile unchanged for Software, while the CPU
  exact contract passes 44/44 checks. The renderer-neutral public object also enforces recovered
  Microsoft XNA profile, result-availability, Begin/End/reuse and disposal rules.
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
- **Large `SpriteBatch` queues remain exact** (`SOFTWARE-251`) — Microsoft XNA/FNA expose queues
  larger than one native submission but chunk the actual 16-bit indexed draws at 2,048 sprites.
  Software already rendered the 16,385th same-texture sprite correctly; the new shared exact-pixel
  discriminator also forced EasyGL to adopt the same native boundary instead of wrapping its
  65,536th vertex back to zero.
- **`SpriteSortMode::Immediate` is observably immediate** (`SOFTWARE-252`) — a shared test draws
  into one target and switches targets before `End`. Software leaves the sprite in the target that
  was active at `Draw`, matching XNA/FNA; the audit used that CPU behavior to catch and repair an
  EasyGL renderer-private queue that had delayed the sprite until `End`.
- **Classic draw failures follow Microsoft XNA's public error contract** (`SOFTWARE-253`) —
  buffered draws validate counts and profile ceilings before checking shader/index/vertex state,
  missing state raises `InvalidOperationException`, and user-array null/range checks precede the
  shader check. The public 32-bit user-index overload remains the deliberate exception: under
  Reach its profile refusal occurs first, exactly where Microsoft places that gate.
- **Model name lookups expose XNA collection failures** (`SOFTWARE-254`) — missing bones/meshes
  throw `KeyNotFoundException`; empty lookup names throw `ArgumentNullException` before changing
  the caller's out pointer, identically above Software and EasyGL.
- **Effect integer indexers retain XNA's nullable object semantics** (`SOFTWARE-255`) — annotation,
  parameter, pass and technique collections return a stable object pointer for a valid index and
  null for either invalid direction. This intentionally follows recovered Microsoft XNA over
  FNA's list-index exception, and is exercised on stock graphs plus a parsed EasyGL Effect graph.
- **Effect semantic lookup is ordinal and case-insensitive** (`SOFTWARE-256`) — both mutable and
  const `GetParameterBySemantic` overloads follow recovered Microsoft XNA's
  `StringComparison::OrdinalIgnoreCase` rule and retain first-declaration wins for duplicates;
  the separate name indexer remains case-sensitive.
- **`Effect.CurrentTechnique` enforces the Microsoft object boundary** (`SOFTWARE-257`) — setting
  null, a technique owned by another Effect, or any value after disposal throws the recovered XNA
  exception before changing either the public selection or a compiled renderer runtime.
- **`EffectPass.Apply` validates lifetime before technique membership** (`SOFTWARE-258`) — a
  disposed Effect consistently reports `ObjectDisposedException`, including when the requested
  pass is also outside the current technique.
- **Disposed Effects cannot be clone sources** (`SOFTWARE-259`) — the base Effect clone paths,
  every classic stock-effect override and EffectMaterial reject `Clone()` with
  `ObjectDisposedException`, following Microsoft's clone-constructor lifetime check.
- **`EffectMaterial.Clone()` uses the inherited base result** (`SOFTWARE-260`) — Microsoft XNA and
  FNA do not override this virtual method. CNA therefore returns an independent base `Effect`
  while preserving cloned compiled parameters, techniques and current selection.
- **EffectParameter array getters require a positive count** (`SOFTWARE-261`) — all nine classic
  `GetValue*Array` families reject zero and negative counts with `ArgumentOutOfRangeException`
  before checking whether the reflected parameter can provide a numeric value.
- **Stock-effect validation reports the XNA exception types** (`SOFTWARE-262`) —
  `EnvironmentMapEffect`/`SkinnedEffect` required-lighting, bone-palette and skin-weight guards use
  the recovered `NotSupportedException`/`Argument*Exception` contracts and parameter names.
- **`Texture3D` transfers require an exact volume-sized count** (`SOFTWARE-263`) — a full or
  partial `SetData`/`GetData` rejects both short and surplus element counts before mutation, and
  null/count/box failures expose Microsoft's named `System::Argument*Exception` families rather
  than renderer-independent native C++ exceptions.
- **Compiled Effect texture access respects reflected types** (`SOFTWARE-264`) — the shared public
  layer rejects incompatible texture getters and non-texture setters with `InvalidCastException`,
  while generic `Texture` parameters accept every classic dimension. This is executable on
  compiled-capable EasyGL; Software continues to report that larger subsystem unsupported.
- **Compiled-Effect conformance declares its HiDef prerequisite** (`SOFTWARE-265`) — its deliberate
  separate-alpha state is not legal under Reach. All backend wrappers now select HiDef and the
  shared helper guards that assumption, keeping the EasyGL reference evidence executable.
- **Compiled Effect texture assignment enforces XNA resource lifetime** (`SOFTWARE-266`) — a
  disposed texture or currently bound render target is rejected before reflected parameter-type
  validation, matching Microsoft's observable exception order. The shared fix is exercised on
  compiled-capable EasyGL; Software retains its honest compiled-effect capability skip.
- **Compiled typed EffectParameter setters enforce reflected shape** (`SOFTWARE-267`) — Matrix,
  Vector2/3/4 and Quaternion scalar/array overloads reject incompatible class, dimensions and
  scalar-versus-array use exactly where recovered Microsoft IL does. The corrected synthetic
  fixture also matches fxc/FNA reflection for ordinary versus array structure members.
- **Compiled scalar EffectParameter setters broadcast by reflected shape** (`SOFTWARE-268`) — the
  bool, int and float overloads reject array parents and broadcast their converted value across
  every reflected vector or matrix component, matching recovered Microsoft XNA behavior instead
  of changing only the first register cell. Standalone CNA/C API parameters remain lenient.
- **Compiled numeric-array setters reject structures** (`SOFTWARE-269`) — bool, int and float
  arrays accept only reflected Scalar, Vector or Matrix classes, as Microsoft XNA does, rather
  than overwriting the raw backing cells of a Structure parameter.
- **Compiled typed getters enforce reflected shape and conversion** (`SOFTWARE-270`) — scalar
  reads convert float/int/bool storage instead of reinterpreting bits; scalar parameters broadcast
  to vector, Quaternion and Matrix results; incompatible aggregate widths/classes and non-array
  Matrix-array reads throw `InvalidCastException`, matching recovered Microsoft IL.
- **Compiled array getters preserve requested length and flat packing** (`SOFTWARE-271`) — every
  positive `count` produces that many zero-initialized results, available values are converted and
  packed from the reflected logical stream, and Matrix/transpose arrays zero-pad their tail.
- **Compiled numeric-array setters convert to reflected storage** (`SOFTWARE-272`) — bool, int and
  float array sources are converted cell-by-cell to the parameter's reflected Bool, Int32 or Single
  representation instead of copying incompatible source-type bit patterns into compiled registers.
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
- **Graphics resources share the reference renderer's complete public lifecycle contract**
  (`SOFTWARE-124`). Nine unchanged EasyGL programs pass 126/126 checks on Software for validation,
  disposed-use rejection, idempotent and bound-resource disposal, renderer-handle release, move
  ownership, resource events, device-first cleanup and leak accounting. The shared device manager
  also releases an active renderer-thread lease before device teardown and unsubscribes its event
  callbacks, preventing a pending frame from touching an already-destroyed renderer.
- **Classic `Model.Draw()` is end-to-end tested on the CPU** (`SOFTWARE-125`). Nine unchanged
  EasyGL fixtures pass through Software for rigid and skinned models, root/child mesh placement,
  per-mesh effects, texture materials, JSON/binary content loading, 16/32-bit indices, imported
  skeleton data and visible animation-clip deformation. This proof exercises public Model and
  ContentManager orchestration rather than stopping at isolated SkinnedEffect triangles.
- **Meaningful presentation resets are real CPU state changes** (`SOFTWARE-127`).
  `GraphicsDeviceManager.ApplyChanges()` resizes the backbuffer, reports the renderer-applied
  sample count, switches real single/4x sample storage, and allocates exactly the depth/stencil
  planes requested by `None`, `Depth16`/`Depth24` or `Depth24Stencil8`. A size change restores the
  viewport and scissor to the complete target; an ordinary `Present()` preserves custom values.
  Six unchanged EasyGL public fixtures plus shared MSAA and target-restoration contracts protect
  these renderer-independent semantics. Physical fullscreen modes, swap timing and a native GPU
  context remain intentionally non-applicable.
- **Custom `ShaderEffect` (arbitrary GLSL/HLSL/WGSL source) compiles but doesn't actually execute**
  — mirrors `HEADLESS-16`'s own precedent exactly: the source is accepted without compiling, and
  only effects whose `FillGpuDrawParams()` output matches one of this renderer's fixed stock-effect
  CPU paths will render correctly.
- **`Present()` completes immediately without a physical swap**, preserving the CPU framebuffer
  and current viewport/scissor state. There is no way to visually inspect a Software-rendered frame
  on screen in this renderer's current form. An opt-in "blit the CPU framebuffer to a real window"
  mode is a reasonable future addition (`plans/plan_software.md` design decision 3) but isn't needed for
  this renderer's actual value proposition (deterministic, GPU-free pixel tests).

See `plans/plan_software.md` for the full task-by-task status and design rationale.
