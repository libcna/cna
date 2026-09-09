# Known gaps

Capabilities XNA has and CNA does not, **measured** against a specific XNA behaviour and recorded
with the case that found them. Distinct from [`known_bugs.md`](known_bugs.md): nothing here is
wrong, it is absent — or present as a deliberate divergence — and closing it is a decision rather
than a repair.

A row leaves this file when the capability lands, or when the owner records that CNA will not have
it.

Most of these surface through the `cna-samples` campaign, because porting a sample is what puts
weight on a corner of the API nothing else reaches. The sample that found a gap is named, but a gap
outlives that sample's own fate: several were found through rows that were then cancelled for
unrelated reasons, and the gap is not cancelled with them.

---

## 1. `SurfaceFormat::Alpha8` render targets are refused

**Found:** 2026-09-09, through SAMPLE-087 (AvatarShadows), whose row was cancelled for an unrelated
reason — the sample is Xbox-360-only. The gap is not.

`GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Alpha8)` returns **false**,
asserted by `GraphicsCapabilityFloatRenderTargetTest.NonColourNonFloatFormatsAreNotRenderTargets`.
The test's own comment gives the reasoning: "claiming otherwise would put the caller back in the
position MOD-100 exists to end — asking for one format and silently receiving another."

**XNA does the opposite.** Its `RenderTarget2D` silently substitutes when it cannot honour a
requested format — the behaviour this project records as a defect in `MOD-107`, where a WebGPU
`RenderTargetCube(HdrBlendable)` reported the float format it was asked for while holding 8-bit
texels.

So CNA's refusal is principled and knowing, and it is still a divergence. It costs any technique
that renders into a single-channel target: SAMPLE-087's planar avatar shadows drew into a
full-screen `Alpha8` target and sampled it to darken the ground by 50 %, which is an ordinary
shadow-mask idiom rather than anything exotic.

**Closing it** means either supporting `Alpha8` as a render-target format on the renderers whose
API can hold it — GL has `GL_R8`, and a swizzle or a shader read gives the alpha semantics — or an
owner decision to substitute as XNA does and give up the MOD-100 guarantee for this format. The
first keeps both promises; the second is smaller.

---

## 2. No `FontTextureProcessor`: a marker bitmap cannot become a `SpriteFont`

**Found:** 2026-09-09, through SAMPLE-090 (BitmapFontMaker).

XNA has two routes from authored content into a `SpriteFont`:

| route | XNA processor | CNA |
|---|---|---|
| `.spritefont` XML → rasterise an installed TrueType face | `FontDescriptionProcessor` | **present** — `CNA::Content::Pipeline::FontDescription`, `modules/content-pipeline` |
| a pre-rendered bitmap whose glyphs are separated by a marker colour | `FontTextureProcessor` | **absent** |

The second route is how a project ships a hand-drawn or pixel-art font, or one rendered by a tool
rather than by the pipeline. SAMPLE-090 is such a tool: it clears an atlas to `Color.Magenta` and
blits each glyph over it with `CompositingMode.SourceCopy`, saving 32-bit ARGB BMP
(`MainForm.cs:219`). But the gap is not about that tool — **any** bitmap produced by any means is
equally unusable, because CNA has no importer for the format.

**Closing it** means a processor that takes an image, splits glyphs on the marker colour, and emits
the same `SpriteFont` the description route already emits. The runtime side needs nothing: the
`SpriteFont` it would produce is the one CNA already loads.

---

## 3. `IntermediateSerializer` XML is read for exactly one schema

**Found:** 2026-09-09, through SAMPLE-093 (CurveEditor).

XNA's content pipeline can take a `.xml` source asset in `IntermediateSerializer` form — an
`<XnaContent><Asset Type="...">` document — and import **any** type it names. That is how a project
ships hand-authored or tool-authored content: a `Curve`, a level description, a custom data class.

CNA reads that envelope for **one** asset type. `ParseFontDescription`
(`modules/content-pipeline/src/SpriteFontContentPipeline.cpp:115`) checks the root really is
`XnaContent`, finds `<Asset>`, and then requires its `Type` attribute to be a font description,
reading `<Size>`, `<Style>` and the character regions by hand. It is a bespoke parser for the
`.spritefont` schema, not a reader of the format.

There is no `IntermediateSerializer` anywhere else in the tree: the name appears once, in a comment
in `ReflectiveTypeReader.hpp` explaining why XNB field order is what it is.

**What that costs.** Every `.xml` content asset that is not a `.spritefont` is unimportable, whatever
produced it. SAMPLE-093's editor round-trips `IntermediateSerializer<Curve>` XML with keys,
tangents, continuity and loop types — CNA has the whole `Curve` type family and loads curves happily
from **XNB**, so only the authoring format is missing, not the runtime.

**Closing it** means a general reader over the envelope CNA already parses, dispatching on the
`Type` attribute the way `ReflectiveTypeReader` already dispatches for XNB. The two would then agree
on one type table.

