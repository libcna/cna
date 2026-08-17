# Migration guide: porting an XNA 4.0 / FNA game to CNA

**Written for Task 486 (`plan_graphics.md` Phase 54).** This is a practical guide for someone who
already has an XNA 4.0 or FNA game and wants to know: can I bring this to CNA, what needs to
change, and what doesn't work yet. It draws conclusions from `docs/xna-4-api-coverage.md` (Tasks
482–485) and `docs/graphics-renderer-feature-matrix.md` (Task 451) for a practical audience — see
those two documents for the full underlying detail; this guide does not repeat their tables.

## What CNA is (and isn't)

CNA is a **C++23 reimplementation** of the XNA 4.0 programming model — it preserves XNA's class
names, method signatures, and namespaces (`Microsoft::Xna::Framework::*`), but it is not a C#/.NET
runtime and cannot run your existing C# assembly. Porting a game means **translating the C#
source to C++**, not recompiling or bridging it. If your game is small-to-medium and mostly uses
the XNA API surface directly (not deep reflection, `dynamic`, or heavy LINQ), the translation is
usually mechanical once you know the conventions below.

## The single biggest mechanical difference: properties

C# properties (`public Color Color { get; set; }`) do not exist in C++. CNA's project-wide
convention is explicit getter/setter methods:

```csharp
// XNA/FNA C#
spriteBatch.GraphicsDevice.BlendState = BlendState.AlphaBlend;
var color = texture.Format;
```

```cpp
// CNA C++
spriteBatch->getGraphicsDeviceProperty()->setBlendStateProperty(BlendState::AlphaBlend);
auto format = texture->getFormatProperty();
```

Every single property access in your existing C# code needs this rewrite — `X` becomes
`getXProperty()` for reads and `setXProperty(value)` for writes. There is no way around this; it's
the most common source of purely mechanical (not logical) porting errors.

## Namespace mapping

`Microsoft.Xna.Framework.*` stays exactly the same, just with C++ `::` instead of C# `.`:
`Microsoft.Xna.Framework.Graphics.Texture2D` → `Microsoft::Xna::Framework::Graphics::Texture2D`.
Class names, enum names, and constants are preserved exactly (e.g. `Color::CornflowerBlue`, not a
renamed or restructured equivalent) — CLAUDE.md's own project rule is to never diverge from the
XNA/FNA name even when a more "C++-idiomatic" name would read better.

Non-XNA CNA-only additions (helpers, extensions, internal renderer types) are marked with a
`CNAEXT` macro and/or an `EXT` suffix (e.g. `PrimitiveType::PointListEXT`,
`Texture2D::SetDataPointerEXT`) — anything with that suffix is safe to ignore unless you
specifically want a CNA extension your original XNA code never used.

## Choosing a renderer

CNA has 4 pluggable graphics renderers, chosen at CMake configure time
(`-DCNA_GRAPHICS_RENDERER=<SDL_RENDERER|OPENGLES3|OPENGL33|WEBGL1|WEBGL2|VULKAN|BGFX>`). Full per-feature detail is in
`docs/graphics-renderer-feature-matrix.md`; the practical summary:

- **Your game is 2D only** (SpriteBatch/SpriteFont, no 3D models or stock Effects): any renderer
  works, but **SDL_Renderer** is the simplest and is comprehensively pixel-verified for the entire
  2D path (`docs/sdl-renderer-2d-completeness.md`). It cannot do 3D rendering at all — any 3D call
  throws `std::runtime_error` by design, this is not a bug to work around.
- **Your game uses 3D** (`BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/
  `SkinnedEffect`, `Model`, render targets): use **OPENGLES3** (or **WEBGL2** under Emscripten) —
  internally implemented by EasyGL, it is the most mature 3D renderer, with the fewest open gaps
  and the most complete pixel-test coverage.
- **Your game loads custom compiled XNA/FNA effects:** choose **FNA3D**. It is currently the only
  renderer that advertises `GraphicsCapability::CompiledEffects`; all other renderers reject the
  same constructor explicitly instead of substituting a stock shader.
- **Vulkan** and **Bgfx** both have working 3D pipelines (core MVP/lighting/texture/fog is
  pixel-verified on all 3), but each has its own real, currently-open gaps — see the next section
  before picking one over EasyGL.

## What fully works today

Per `docs/xna-4-api-coverage.md`'s per-class table (Task 483): `RenderTarget2D`/`RenderTargetCube`,
`SpriteFont`, `BasicEffect`'s core rendering, `VertexBuffer`/`VertexDeclaration`, `Viewport`,
`PresentationParameters`, and `GraphicsAdapter` have **no open gaps on any renderer**. Most of the
rest of the Graphics API surface is fully correct on at least EasyGL.

## The former blockers that matter to existing ports

Before the smaller caveats below — these two are the ones most likely to block a real port:

- **`.xnb` is supported for the registered built-in readers**, including the general
  `EffectReader`. Reflective custom readers and some media/surface formats remain explicit gaps;
  consult `docs/xnb-content-pipeline-support.md` before assuming every asset type is portable.
- **`Effect(GraphicsDevice&, byte[])` is implemented on FNA3D.** The input must be compiled
  Direct3D 9 Effect Framework bytecode (`.fxb` or an XNB Effect payload), not HLSL `.fx` source and
  not MonoGame MGFX. Public parameters, techniques/passes, cloning, pass state, general 3D draws,
  and `SpriteBatch` use the native FNA3D/MojoShader runtime. Other renderers currently report
  `CompiledEffects == false`, so use FNA3D or keep a renderer-specific CNAEXT `ShaderEffect` path.

## What has caveats — read this before porting anything using these

**Update, 2026-07-11: the four items originally listed in this section are all fixed** — kept here
briefly so old bookmarks/searches still find them, not because they're still risks:

- ~~`IndexElementSize` (32-bit index buffers)~~ — **fixed, Task 921** (2026-07-09). CNA's values now
  match FNA exactly (`SixteenBits=0`, `ThirtyTwoBits=1`).
- ~~Vulkan `BlendState`~~ — **fixed, Task 868** (2026-07-09). Vulkan now applies the real requested
  blend state, same as EasyGL/Bgfx.
- ~~EasyGL anisotropic filtering~~ — **fixed, Task 918** (2026-07-09). `TextureFilter::Anisotropic`
  now issues a real `GL_EXT_texture_filter_anisotropic` call on EasyGL too.
- ~~`Model` root bone~~ — **fixed, Task 916** (2026-07-09). The constructor now takes an optional
  `rootBoneIndex` parameter.

Real, currently-open caveats worth knowing about instead:

- ~~EasyGL: a `SpriteBatch.Begin()`/`End()` pair leaks its blend state into subsequent 3D draws~~ —
  **fixed, Task 956** (2026-07-11). `SpriteBatch::Begin()` on EasyGL used to hardcode its own blend
  factors regardless of what `BlendState` was requested, and leave that leftover state in effect
  after `End()`. Now, whatever `BlendState` you pass to `SpriteBatch.Begin()` (or the default
  `AlphaBlend`) genuinely persists on `GraphicsDevice.BlendState` afterward, matching real FNA — if
  you draw 3D geometry after a `SpriteBatch` pass without resetting `BlendState` yourself, you get
  real FNA's own well-known behavior (it inherits the SpriteBatch's blend mode), not leftover
  garbage state.
- **EasyGL: a full-backbuffer `SpriteBatch` draw before any 3D draw in the same frame breaks that
  frame's 3D rendering** (Task 933) — investigated, root cause not yet isolated.
- **Vulkan `RasterizerState.DepthBias` has no effect** — one isolated, unresolved case.
- **Bgfx: `DrawIndexedPrimitivesEx`'s non-wireframe path silently discards `startIndex`/`baseVertex`**
  (Task 954) — not hit by any current CNA sample (every `Model`/`ModelMeshPart` owns its own buffer
  starting at 0), but affects a genuine sub-range indexed draw if your game does one.
- ~~`EnvironmentMapEffect` only forwards `DirectionalLight0`~~ — **fixed, Task 890** (2026-07-11):
  `DirectionalLight1`/`2` now forward correctly on all 3 renderers.
- ~~`SkinnedEffect` only forwards `DirectionalLight0`~~ — **fixed, Task 893** (2026-07-11):
  `DirectionalLight1`/`2` now forward correctly on all 3 renderers.
- ~~`SkinnedEffect` has no specular term~~ — **fixed, Task 894** (2026-07-11): real half-vector
  Blinn-Phong specular (`SpecularColor`/`SpecularPower`) now implemented on all 3 renderers.
- ~~`SkinnedEffect`'s `WeightsPerVertex` is a GPU no-op~~ — **fixed, Task 895** (2026-07-11): each
  renderer's skinning vertex shader now only sums the first `WeightsPerVertex` (1, 2, or 4)
  weight/index pairs, matching FNA's real `Skin(vin, boneCount)` behavior.
- See `docs/xna-4-api-coverage.md`'s full "Known deviations from XNA/FNA" list (Task 485) for a
  handful of smaller, permanent, intentional deviations (e.g. `GetHashCode()` returns
  `std::size_t` not `int`; a couple of `Texture2D` methods have looser null/argument validation
  than FNA) that are unlikely to affect a typical port but are documented there in full.
- `NEXT.md` §5 is the actively-maintained current bug list — check it directly for anything not
  covered above before committing to a renderer.

## What doesn't work at all yet

These need a project-owner architecture decision before they can be implemented — not just more
engineering time — so don't plan a port around them without checking current status first:

- **`SpriteBatch` `TextureAddressMode::Wrap`/`Mirror` on SDL_Renderer** (Tasks 686/687) — no native
  support in the draw path SDL_Renderer's `SpriteBatch` renderer uses.
- **`Texture3D`/`TextureCube` on SDL_Renderer** (Task 725) — construction currently succeeds
  silently with a null renderer rather than throwing; don't rely on 3D textures if you pick
  SDL_Renderer.
- **Non-`Color` `SurfaceFormat` GPU texture data on any renderer** (Task 732) — if your game uses
  compressed or non-8-bit-per-channel texture formats for real GPU sampling (not just file I/O),
  this is currently blocked project-wide.

(Vulkan `OcclusionQuery`, previously listed here as functionally inert pending an architecture
decision, is **fixed as of Task 447/854, 2026-07-10** — real per-draw-call query correlation is now
implemented; see `docs/occlusionquery-support.md`.)

## Content pipeline: the other big difference

Real XNA/FNA games ship compiled `.xnb` binary assets built by the Content Pipeline build tool.
**CNA's `ContentManager` does not read `.xnb` files at all** — it uses a file-extension-based
loader instead (e.g. loading a `.png`/`.jpg` directly for a `Texture2D`, a `.model.json` for a
`Model`). If your game ships `.xnb` assets, you cannot point CNA's `ContentManager` at them
unchanged — you'll need to either re-export your source assets in a format CNA's loaders
recognize, or write a new loader. `ContentReader`/`ContentTypeReaderManager`/`LzxDecoder` (the
classes that would read raw XNB binaries) are intentionally not implemented in CNA (see
`docs/xna-4-api-coverage.md` §3/§6). `Model`'s own content-pipeline loader additionally has real
gaps versus FNA's `.xnb` format today (no bone hierarchy, no `ParentBone` wiring — Task 440); a
hand-built `Model` via its public constructors works today, but loading one through
`ContentManager` does not yet fully round-trip a real FNA-authored model asset.

## Summary checklist for a new port

1. Rewrite every C# property access as `getXProperty()`/`setXProperty()`.
2. Pick a renderer: SDL_Renderer for 2D-only, EasyGL for anything with 3D.
3. Re-export or rewrite your content pipeline — don't expect `.xnb` assets to load unchanged.
4. Check the "what doesn't work yet" list above against your game's actual feature use before
   committing to a renderer.
5. Check whether your game loads custom `.fx` effect bytecode or ships `.xnb` content — both are
   currently unsupported (see "The two gaps that actually matter" above) and are the most common
   reasons a real XNA/FNA game can't port as-is yet.

## 2D compatibility checklist (Task 487)

A scannable, tick-through-your-own-source checklist for 2D-only games (`SpriteBatch`/`SpriteFont`/
`Texture2D`). Every row was checked against the real current header (not assumed from XNA docs)
and against `docs/sdl-renderer-2d-completeness.md` (Task 731, the detailed source behind every
✅/⚠️/❌ below — this list summarizes, it doesn't repeat that document's own rationale). Legend:
✅ ports as-is (after the property rewrite) · ⚠️ ports with a caveat, read the note · ❌ not
supported, throws · ⛔ BLOCKED, needs a project-owner decision.

**`SpriteBatch`**

| Member | Status | Note |
|---|---|---|
| `Begin()` (all 4 overloads: no-arg, sort+blend, +sampler/depth/raster, +effect/transform) | ✅ | |
| `End()` | ✅ | |
| `Draw(Texture2D, Vector2 position, Color)` | ✅ | |
| `Draw(Texture2D, Vector2 position, Rectangle? source, Color)` | ✅ | |
| `Draw(Texture2D, Vector2 position, Rectangle? source, Color, rotation, origin, float scale, SpriteEffects, layerDepth)` | ✅ | |
| `Draw(Texture2D, Vector2 position, Rectangle? source, Color, rotation, origin, Vector2 scale, SpriteEffects, layerDepth)` | ✅ | |
| `Draw(Texture2D, Rectangle destination, Color)` | ✅ | |
| `Draw(Texture2D, Rectangle destination, Rectangle? source, Color)` | ✅ | |
| `Draw(Texture2D, Rectangle destination, Rectangle? source, Color, rotation, origin, SpriteEffects, layerDepth)` | ✅ **fixed, Task 922** (2026-07-09) | The real overload now takes an optional `std::optional<Rectangle> sourceRectangle`, matching FNA's `Rectangle?` exactly. Previously this was a `CNAEXT`-marked near-equivalent with a required `Rectangle` source parameter — not a drop-in signature match — found while verifying this checklist against the real header. |
| `SpriteSortMode` (all 4 values) | ✅ | |
| `transformMatrix` in `Begin()` | ✅ | |
| Custom `Effect` via `Begin(effect)` | ⚠️ (SDL_Renderer only) | Throws by design on SDL_Renderer (no shader stage there); works on EasyGL/Vulkan/Bgfx. |
| `DrawString` (6 overloads) | ✅ | |

**`SpriteFont`**

| Member | Status | Note |
|---|---|---|
| `MeasureString(String)` | ✅ | |
| `MeasureString(StringBuilder)` | ✅ | Added by Task 423 — if you're on an older CNA checkout predating that task, this overload won't exist yet. |
| Glyph placement, spacing/kerning, `\n` newlines, unknown-char fallback | ✅ | Pixel-verified (Tasks 424-427/690-693). |
| `SpriteEffects` flip + rotation/origin/scale via `DrawString` | ✅ | Fixed in shared code this session (Task 694) — correct on every renderer, not just where it was found. |

**`Texture2D`**

| Member | Status | Note |
|---|---|---|
| `Texture2D(GraphicsDevice&, int width, int height)` | ✅ | |
| `Texture2D(GraphicsDevice&, int width, int height, bool mipMap, SurfaceFormat)` | ✅ | |
| `FromStream(GraphicsDevice&, Stream&)` (2 overloads) | ✅ | PNG/JPEG/BMP/DDS auto-detected — see the "Known deviations" entry on `FromStream`'s DDS auto-detection differing from FNA's stricter contract. |
| `SetData(Color* data, int elementCount)` | ✅ | |
| `SetData(int level, Rectangle* rect, Color* data, int startIndex, int elementCount)` | ⚠️ | `level > 0` (mip levels) is a silent no-op on Vulkan/Bgfx (Task 867) and throws on SDL_Renderer by design (Task 681); `level == 0` is fully correct everywhere. |
| `GetData` (3 overloads) | ✅ | Pure CPU-side cache read on every renderer — renderer-independent by construction. |
| `SaveAsPng`/`SaveAsJpeg` (stream + file-path `CNAEXT` overloads) | ✅ | |
| Non-`Color` `SurfaceFormat` for real GPU sampling | ⛔ BLOCKED | Task 732 — file I/O round-trips fine; GPU texture upload of non-`Color` formats does not. |

**`BlendState`/`SamplerState` (as used via `SpriteBatch::Begin`)**

| Preset | Status | Note |
|---|---|---|
| `BlendState::Opaque`/`AlphaBlend`/`NonPremultiplied`/`Additive` | ✅ | Correct on all 4 renderers — Vulkan's blend-equation-hardcoding bug (Task 868) was fixed 2026-07-09. |
| `SamplerState::PointClamp`/`PointWrap`/`LinearClamp`/`LinearWrap`/`AnisotropicClamp`/`AnisotropicWrap` | ✅ | `TextureFilter::Anisotropic` is now genuinely supported on all 3 3D renderers — EasyGL's trilinear-fallback bug (Task 918) was fixed 2026-07-09. |
| `TextureAddressMode::Wrap`/`Mirror` on SDL_Renderer specifically | ⛔ BLOCKED | Tasks 686/687 — works on all 3 other renderers. |

## 3D compatibility checklist (Task 488)

A scannable, tick-through-your-own-source checklist for 3D games (the 5 stock Effects, `Model`,
`VertexBuffer`/`IndexBuffer`/`VertexDeclaration`). Unlike the 2D checklist above, 3D behavior
genuinely differs by renderer, so every row below is qualified per renderer. Every constructor/
overload listed was checked against the real current CNA header **and** the real FNA `.cs` source
at `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/` — the same
overload-diffing discipline that found Task 487's `SpriteBatch::Draw` gap (Task 922) — not assumed
from memory. **`SDL_Renderer` is not in the per-renderer columns below because it doesn't support
3D at all** — every member on this page throws `"SDL_Renderer does not support 3D"` there by
design; if you're 2D-only, see the checklist above instead and skip this section entirely. Legend:
✅ ports as-is (after the property rewrite) · ⚠️ ports with a caveat, read the note · ❌ known gap
· ⛔ BLOCKED.

**The 5 stock Effects — construction**

Real FNA has exactly one public constructor per effect, `EffectName(GraphicsDevice device)`, plus
a `Clone()` override backed by a `protected` copy constructor (confirmed by reading each of
`BasicEffect.cs`/`AlphaTestEffect.cs`/`DualTextureEffect.cs`/`EnvironmentMapEffect.cs`/
`SkinnedEffect.cs` directly). CNA matches this exactly for all 5: one public
`explicit EffectName(GraphicsDevice&)` constructor (device passed by reference, this project's
established convention throughout — see `docs/xna-4-api-coverage.md`'s deviations list) plus a
`Clone()` override backed by a `private` copy constructor (CNA's accepted mapping for FNA's
`protected`, per `CLAUDE.md`'s visibility table). No signature or overload-count mismatch found —
unlike `SpriteBatch::Draw` (Task 922), this area checked out clean.

| Effect | Construction | Core rendering (EasyGL / Vulkan / Bgfx) | Known caveats |
|---|---|---|---|
| `BasicEffect` | ✅ | ✅ / ✅ / ✅ | None — no open gaps on any 3D renderer (per Task 483's table). |
| `AlphaTestEffect` | ✅ | ✅ / ✅ / ✅ | `VertexColorEnabled` missing on Vulkan/Bgfx (Task 887). |
| `DualTextureEffect` | ✅ | ✅ / ✅ / ✅ | `VertexColorEnabled` missing on all 3 3D renderers (Task 889). |
| `EnvironmentMapEffect` | ✅ | ✅ / ✅ / ✅ | No open gaps — `DirectionalLight1`/`2` (Task 890, fixed 2026-07-11, was missing on all 3 renderers not just Vulkan/Bgfx) and base-lerp alpha scaling (Task 891) are both fixed. |
| `SkinnedEffect` | ✅ | ✅ / ✅ / ✅ | No open gaps — `DirectionalLight1`/`2` (Task 893), `SpecularColor`/`Power` (Task 894), and `WeightsPerVertex` GPU enforcement (Task 895) all fixed 2026-07-11, all three were missing on all 3 renderers not just Vulkan/Bgfx. |
| Fog (all 5 effects) | — | ✅ / ✅ / ✅ | Fully implemented on every 3D renderer for every effect, including Vulkan's `env_map3d`/`skinned3d` (Task 899, closed 2026-07-07) — a stale "Vulkan still lacks fog" claim in this same file's own per-renderer table (Task 484) was found and corrected while writing this checklist. |
| `ShaderEffect` (CNAEXT custom shader) | ✅ (constructor exists on all 3) | ✅ / ✅ / ❌ | Bgfx's `CreateEffectRenderer` returns `nullptr` for it — the one whole-feature 3D gap left. |

**`Model` / `ModelMesh` / `ModelBone`**

Real FNA's own `Model` constructor is `internal` — ordinary XNA/FNA game code never calls it
directly, only via `ContentManager.Load<Model>()`'s content pipeline (confirmed in `Model.cs`).
CNA exposes two `CNAEXT`-marked public constructors instead (there being no real public FNA
constructor to match against) so hand-built models are possible without content loading — this is
an intentional, documented CNA convenience, not a signature mismatch.

| Member | Status | Note |
|---|---|---|
| `Model::Draw(Matrix world, Matrix view, Matrix projection)` | ✅ | Correct on EasyGL/Vulkan/Bgfx; throws on SDL_Renderer by design. |
| Constructing a `Model` by hand (`CNAEXT` constructors) | ✅ | **Fixed, Task 916** (2026-07-09) — an optional `rootBoneIndex` parameter (default `0`) now lets you specify a root bone other than `bones[0]`. |
| Loading a `Model` via `ContentManager` | ⚠️ | CNA's own `.model.json` format, not FNA's `.xnb` — real gaps versus FNA's loader (no bone hierarchy/`ParentBone`/`BoundingSphere`/`Tag` wiring, Task 440). Don't expect an FNA-authored `.xnb` model to load as-is; see "Content pipeline" above. |
| `ModelMesh`/`ModelBone` collections (`ModelMeshCollection`, `ModelBoneCollection`) | ✅ | `TryGetValue`/`Contains`/`begin()`/`end()` all present (Tasks 432/433). |
| `Model::CopyBoneTransformsFrom`/`To` | ⚠️ | Loop bound is `Bones.Count`, not the caller array's length like FNA — an intentional, safer deviation (see "Known deviations" list), not a bug. |

**`VertexBuffer` / `IndexBuffer` / `VertexDeclaration`**

Real FNA has exactly 2 public constructors each for `VertexBuffer`/`IndexBuffer` and 2 for
`VertexDeclaration` (confirmed against `VertexBuffer.cs`/`IndexBuffer.cs`/`VertexDeclaration.cs`);
CNA has all of FNA's overloads plus extra `CNAEXT` convenience overloads (e.g.
`VertexBuffer(GraphicsDevice&, int vertexCount)` without an explicit `VertexDeclaration`) — purely
additive, no missing or mismatched FNA-facing overload found.

| Member | Status | Note |
|---|---|---|
| `VertexBuffer`/`IndexBuffer` construction (both FNA overloads) | ✅ | No open gaps (Task 483's table). |
| `VertexDeclaration` construction (both FNA overloads + `initializer_list` convenience) | ✅ | Construction/assignment confirmed never throws (Task 729). |
| `IndexElementSize::ThirtyTwoBits` numeric value | ✅ | **Fixed, Task 921** (2026-07-09) — CNA's values now match FNA's real `0`/`1` exactly (previously `16`/`32`). |
| `SetDataOptions`/`BufferUsage` | ✅ | No open gaps. |

## Troubleshooting graphics renderer selection (Task 489)

Practical fixes for real configure-time and runtime problems, not another compatibility list —
see the sections above for "is this a bug or a known limitation." Every command and env var below
was verified against the actual current `CMakeLists.txt`/source, not assumed from general CMake/XNA
knowledge.

### Picking/overriding the renderer at configure time

```bash
cmake -S . -B build -DCNA_GRAPHICS_RENDERER=OPENGLES3      # or SDL_RENDERER / VULKAN / BGFX
cmake --build build --target CnaTests
```

`CNA_GRAPHICS_RENDERER` defaults to `OPENGLES3` on Linux/Emscripten, `SDL_RENDERER` elsewhere. See the
top-level `README.md` §9 for the full per-platform build matrix (Windows/MinGW cross-compile
included) — not repeated here.

### Common configure-time failures

| Symptom | Cause | Fix |
|---|---|---|
| `CNA: Missing sibling repository 'sharp-runtime'` / `'easy-gl'` | These are sibling checkouts next to this repo, **not** git submodules — `git submodule update --init` will not fetch them | `cd ..` and `git clone` the missing repo next to this one (the error message prints the exact URL) |
| `Missing vendored 'SDL' …` right after a fresh clone | Downloaded a ZIP/release archive instead of cloning with Git, or forgot to init submodules — vendored `third_party/SDL`/`SDL_image`/`SDL_mixer` are empty | `git submodule update --init --recursive`, or pass `-DCNA_USE_SYSTEM_SDL=ON` to use system SDL3 packages instead |
| `Could not find a package configuration file for Vulkan` (`VULKAN` renderer) | `find_package(Vulkan REQUIRED)` — no Vulkan SDK/loader installed | Install your distro's `vulkan-sdk`/`libvulkan-dev` package (or the LunarG SDK) before configuring |
| CMake FetchContent hangs/fails on `bgfx.cmake` clone (`BGFX` renderer) | `BGFX` fetches `bgfx.cmake` from GitHub at configure time — needs network access and can be slow the first time | Retry, or pre-seed a local clone and point `FETCHCONTENT_SOURCE_DIR_BGFX_CMAKE` at it |
| Video/`VideoPlayer`-related link errors | Missing FFmpeg dev packages — not a graphics-renderer issue, but hits every renderer | `CLAUDE.md`'s "System Dependencies (Linux)" section: `sudo apt-get install libavcodec-dev libavformat-dev libavutil-dev libswresample-dev` |

### Common runtime failures

| Symptom | Cause | Fix / is it a bug? |
|---|---|---|
| Any 3D call (`BasicEffect`, `Model::Draw`, render targets with depth, etc.) throws `std::runtime_error` | You're on **SDL_Renderer** | Not a bug — SDL_Renderer is 2D-only by design (see "Choosing a renderer" above). Switch to EasyGL/Vulkan/Bgfx for 3D. |
| `bgfx::init failed` or a native-window-handle error on the `BGFX` renderer | No usable native window handle for bgfx's chosen renderer under your current SDL video driver | Check `SDL_GetCurrentVideoDriver()` output (included in the real error message); try forcing a renderer via `CNA_BGFX_RENDERER` below |
| Bgfx picks the wrong graphics API (e.g. tries Vulkan on a machine without a Vulkan ICD) | Bgfx's renderer auto-detection (`bgfx::RendererType::Count`) picked something unavailable | Set the `CNA_BGFX_RENDERER` environment variable to force one: `AUTO`, `OPENGL`, `OPENGLES3`, `VULKAN`, `METAL`, `DIRECT3D11`, `DIRECT3D12`, or `NOOP` (case-insensitive; an unsupported value throws immediately with this exact list) |
| Blank/black window, or GL-based renderers (EasyGL/Bgfx-OpenGL) fail to create a context in CI/headless environments | No real X server / GPU driver — common under Xvfb with only a software (`llvmpipe`) GL driver | Expected in headless CI; tests still run against software rendering. If entire runs fail rather than just individual pixel-tolerance edge cases, confirm an X server is actually reachable (`DISPLAY` env var, or pass `-DCNA_TEST_DISPLAY=:99` at configure time to point the whole `ctest` suite at a specific Xvfb display) |
| Pixel-comparison example/tests fail after an intentional rendering change | Golden images are stale references, not a bug | Re-run the specific test with `CNA_UPDATE_GOLDEN=1` set to regenerate the golden PNG, then review the diff before committing it |
| Flaky/inconsistent test failures that don't reproduce in isolation | **Never run multiple renderers' full `ctest` suites concurrently** — confirmed to cause transient GPU/driver-contention false failures (`NEXT.md` §2) | Run renderer test suites sequentially; re-run any single anomalous test in isolation (`ctest -R <TestName>`) before treating it as a real regression |

### Is it a bug, or a known limitation?

Before filing anything, check `docs/xna-4-api-coverage.md`'s "Per-renderer Graphics support" table
(Task 484), "Known deviations from XNA/FNA" list (Task 485), and `NEXT.md` §5 (the actively
maintained current bug list) — most currently-open gaps already have a task number and a documented
root cause (e.g. Vulkan's `RasterizerState.DepthBias` no-op, Bgfx's `DrawIndexedPrimitivesEx`
`startIndex`/`baseVertex` gap/954, `EnvironmentMapEffect`/`SkinnedEffect`'s dropped
`DirectionalLight1`/`2`). If your symptom isn't listed there, it's more likely a genuinely new
finding worth its own task.
