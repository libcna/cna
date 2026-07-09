# Migration guide: porting an XNA 4.0 / FNA game to CNA

**Written for Task 486 (`plan_graphics.md` Phase 54).** This is a practical guide for someone who
already has an XNA 4.0 or FNA game and wants to know: can I bring this to CNA, what needs to
change, and what doesn't work yet. It draws conclusions from `docs/xna-4-api-coverage.md` (Tasks
482–485) and `docs/graphics-backend-feature-matrix.md` (Task 451) for a practical audience — see
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

Non-XNA CNA-only additions (helpers, extensions, internal backend types) are marked with a
`NOXNA` macro and/or an `EXT` suffix (e.g. `PrimitiveType::PointListEXT`,
`Texture2D::SetDataPointerEXT`) — anything with that suffix is safe to ignore unless you
specifically want a CNA extension your original XNA code never used.

## Choosing a backend

CNA has 4 pluggable graphics backends, chosen at CMake configure time
(`-DCNA_GRAPHICS_BACKEND=<SDL_RENDERER|EASYGL|VULKAN|BGFX>`). Full per-feature detail is in
`docs/graphics-backend-feature-matrix.md`; the practical summary:

- **Your game is 2D only** (SpriteBatch/SpriteFont, no 3D models or stock Effects): any backend
  works, but **SDL_Renderer** is the simplest and is comprehensively pixel-verified for the entire
  2D path (`docs/sdl-renderer-2d-completeness.md`). It cannot do 3D rendering at all — any 3D call
  throws `std::runtime_error` by design, this is not a bug to work around.
- **Your game uses 3D** (`BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/
  `SkinnedEffect`, `Model`, render targets): use **EasyGL** — it is the most mature 3D backend,
  with the fewest open gaps and the most complete pixel-test coverage.
- **Vulkan** and **Bgfx** both have working 3D pipelines (core MVP/lighting/texture/fog is
  pixel-verified on all 3), but each has its own real, currently-open gaps — see the next section
  before picking one over EasyGL.

## What fully works today

Per `docs/xna-4-api-coverage.md`'s per-class table (Task 483): `RenderTarget2D`/`RenderTargetCube`,
`SpriteFont`, `BasicEffect`'s core rendering, `VertexBuffer`/`VertexDeclaration`, `Viewport`,
`PresentationParameters`, and `GraphicsAdapter` have **no open gaps on any backend**. Most of the
rest of the Graphics API surface is fully correct on at least EasyGL.

## What has caveats — read this before porting anything using these

- **`IndexElementSize` (32-bit index buffers)** — if your game uses
  `IndexElementSize.ThirtyTwoBits` anywhere your own code reads or compares the enum's *numeric*
  value (not just passes it symbolically to `IndexBuffer`'s constructor), be aware CNA's current
  values (`SixteenBits=16`, `ThirtyTwoBits=32`) do not match real FNA's (`0`/`1`). This is a
  confirmed, open bug (Task 921), found by literally running FNA and CNA side-by-side and diffing
  their JSON output (`tools/fna-reference/`, `tools/cna-reference/`,
  `scripts/compare-fna-reference.py` — Tasks 471–479). Ordinary use (passing the enum symbolically
  to `IndexBuffer`) is unaffected; only code that inspects the raw numeric value is at risk.
- **Vulkan `BlendState`** — if you pick the Vulkan backend and your game relies on a specific blend
  mode other than `Opaque` (e.g. additive particle effects, alpha blending with a
  non-default `BlendFunction`), be aware Vulkan currently hardcodes one blend equation regardless
  of what `BlendState` you actually set (Task 868, open, confirmed via pixel tests). EasyGL and
  Bgfx apply the real requested blend state correctly.
- **EasyGL anisotropic filtering** — `SamplerState`s with `TextureFilter::Anisotropic` silently
  fall back to plain trilinear filtering on EasyGL (Task 918, open); Vulkan and Bgfx genuinely
  support it.
- **`Model` root bone** — if you construct a `Model` directly (not via content loading) and your
  model's true root bone isn't `bones[0]`, CNA's constructor currently has no way to specify a
  different root bone index (Task 916, open).
- See `docs/xna-4-api-coverage.md`'s full "Known deviations from XNA/FNA" list (Task 485) for a
  handful of smaller, permanent, intentional deviations (e.g. `GetHashCode()` returns
  `std::size_t` not `int`; a couple of `Texture2D` methods have looser null/argument validation
  than FNA) that are unlikely to affect a typical port but are documented there in full.

## What doesn't work at all yet

These need a project-owner architecture decision before they can be implemented — not just more
engineering time — so don't plan a port around them without checking current status first:

- **Vulkan `OcclusionQuery`** (Task 447) — functionally inert; the backend's deferred-draw
  recording architecture can't currently correlate a query's Begin/End span with a specific draw.
- **`SpriteBatch` `TextureAddressMode::Wrap`/`Mirror` on SDL_Renderer** (Tasks 686/687) — no native
  support in the draw path SDL_Renderer's `SpriteBatch` backend uses.
- **`Texture3D`/`TextureCube` on SDL_Renderer** (Task 725) — construction currently succeeds
  silently with a null backend rather than throwing; don't rely on 3D textures if you pick
  SDL_Renderer.
- **Non-`Color` `SurfaceFormat` GPU texture data on any backend** (Task 732) — if your game uses
  compressed or non-8-bit-per-channel texture formats for real GPU sampling (not just file I/O),
  this is currently blocked project-wide.

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
2. Pick a backend: SDL_Renderer for 2D-only, EasyGL for anything with 3D.
3. Re-export or rewrite your content pipeline — don't expect `.xnb` assets to load unchanged.
4. Check the "what doesn't work yet" list above against your game's actual feature use before
   committing to a backend.
5. If your game does anything numeric with `IndexElementSize` beyond passing it to `IndexBuffer`,
   watch for Task 921 landing (it will change the enum's underlying values to match FNA).
