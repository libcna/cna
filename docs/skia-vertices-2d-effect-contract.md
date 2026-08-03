# Skia SkVertices 2D vertex/fragment effect contract (SKIA-153)

Status: normative SKIA-153 contract, fixed before the spike below was written, matching SKIA-144's
own "fix the contract before any implementation" precedent for Phase S15.

## Why this replaces the originally planned `SkMeshSpecification`

SKIA-153's original text asked for a `SkMeshSpecification` prototype. Before writing any code, the
pinned Skia source was checked directly: `SkBitmapDevice::drawMesh` --
`src/core/SkBitmapDevice.cpp:561` in the pinned checkout -- is a literal empty function body:

```cpp
void SkBitmapDevice::drawMesh(const SkMesh&, sk_sp<SkBlender>, const SkPaint&) {
    // TODO: Implement, maybe with a subclass of BitmapDevice that has SkSL support.
}
```

CNA's Skia backend creates its surfaces via `SkSurfaces::Raster(...)` (`SkiaSurface.cpp`), which is
backed by exactly this `SkBitmapDevice` class. `SkMesh`/`SkMeshSpecification` therefore draws nothing
at all on this backend -- not a CNA gap, an upstream Skia stub in the pinned revision. The user was
asked how to proceed and explicitly chose redesigning around `SkVertices` (the older, simpler mesh
API) rather than pausing Phase S16 or investigating a Skia re-pin.

`SkBitmapDevice::drawVertices` (`BDDraw(this).drawVertices(...)`) is genuinely implemented for
raster -- confirmed by reading `src/core/SkDraw_vertices.cpp`'s real triangle-fill rasterizer -- and
is what SKIA-96 already used earlier in this project for its own 3D-feasibility spikes.

## What `SkVertices` can and cannot carry

`include/core/SkVertices.h`'s `Builder`/`MakeCopy` API exposes exactly three fixed per-vertex
channels, no more:

- `positions`: `SkPoint` (x, y) -- **no W/depth component exists anywhere in the type.** There is no
  way to construct, and no way to later add, a homogeneous or perspective-divided position through
  this API.
- `texCoords`: `SkPoint` (u, v), optional.
- `colors`: `SkColor` (straight/unpremultiplied 8-bit ARGB), optional.

There is no custom-attribute or custom-varying mechanism (unlike `SkMeshSpecification`, which does
have one but cannot draw at all per above). This happens to match SKIA-153's own original scope
exactly -- "transforms, colour, UV interpolation, clipping, and child sampling" never mentioned
normals, tangents, or bone weights -- so nothing SKIA-153 was actually asked to prove is lost by this
substitution. What SKIA-152's inventory classified as `SkMesh`-shaped *because it needed extra
varyings* (`lit_textured_`'s `vNormal`/`vWorldPos`, `pbr_`'s TBN basis) cannot be revisited through
`SkVertices` either -- those rows stay exactly where SKIA-152 put them; this contract does not reopen
them.

## Interpolation is affine-only, by construction, not by measurement

Because `SkPoint` carries no W, `SkVertices` cannot perform perspective-correct triangle
interpolation even in principle -- there is no perspective divisor anywhere in the pipeline to divide
by. This is proven by the type signature alone, not by rendering a triangle and comparing pixels: no
pixel test could ever demonstrate the *absence* of a capability the API has no field to carry. SKIA-153's
acceptance criterion is therefore "affine interpolation matches a 2D affine-transformed EasyGL
draw" -- not "perspective interpolation is available", which this API architecturally forecloses.
Any future content whose correctness depends on true per-vertex perspective (a triangle with
meaningfully different implied depth at each vertex under a real 3D projection) is out of scope for
whatever effect subset gets built on `SkVertices`, by design, matching this task's own "2D vertex/
fragment subset" framing -- not a limitation discovered after the fact.

## Winding and culling

`src/core/SkDraw_vertices.cpp`'s real CPU rasterizer (`Draw::drawVertices`/`fill_triangle`) contains
no winding-order check, no `CullMode` concept, and no back-face rejection of any kind -- confirmed by
reading the file; it unconditionally fills every triangle regardless of vertex order. This matches
ordinary 2D Skia geometry (`SkPath` fill has no camera-facing concept either) and is architecturally
expected, not a bug to work around. A caller wanting XNA `CullMode` semantics (cull clockwise or
counter-clockwise triangles before submission) must perform that check itself, CPU-side, before
building the `SkVertices` object -- matching this project's own established "pre-check on the CPU
side, submit only what should render" pattern used elsewhere in this backend (e.g. `RasterizerState`
wireframe-before-cull ordering). SKIA-153's own spike proves both windings render *identically*
through `SkVertices` (since nothing culls), which is the correct, expected raster contract for this
API -- not evidence of a defect.

## Alpha convention

`SkDraw_vertices.cpp`'s `convert_colors` treats every caller-supplied `SkColor` vertex colour as
**unpremultiplied** (`kUnpremul_SkAlphaType` source), converting internally to premultiplied `float`
for rasterization -- ordinary `SkColor` semantics, matching how the rest of this Skia backend already
treats straight-alpha 8-bit colour input elsewhere (`Color`'s own RGBA8 straight-alpha convention).
The vertex-colour and paint-shader (texture) contributions combine via the blend mode passed to
`drawVertices` -- `SkBlendMode::kModulate` (multiply) reproduces `BasicEffect`'s own
`vertexColour * textureSample * uDiffuseColor`-shaped combine (`docs/skia-easygl-effect-inventory.md`),
matching the default blend `SkCanvas::drawMesh` itself falls back to when no blender is supplied
(`SkBlender::Mode(SkBlendMode::kModulate)`), even though `drawMesh` cannot draw on this backend --
`drawVertices` is given the same mode explicitly since it takes no blender-optional default.

## What SKIA-153 actually builds

A below-the-API spike (matching the SKIA-93/145/147 precedent: prove the mechanism with real pixels
before any public API integration) that:

1. Builds already-transformed 2D device-space `SkVertices` triangles (the CPU-side WVP/viewport
   transform this project's own established pattern already performs for 2D content, matching
   `docs/skia-3d-emulation-adr.md`'s own `3D-TRANSFORM` row: "CPU WVP/viewport math passes for
   bounded fixtures" -- no programmable vertex stage is needed for this task's scope).
2. Draws with `SkCanvas::drawVertices(vertices, SkBlendMode::kModulate, paint)`, `paint`'s shader
   being either a solid colour, or an `SkImage`-backed texture shader for the textured cases.
3. Reproduces `docs/skia-easygl-effect-inventory.md`'s `colored`/`textured`/`col_textured` (vertex
   colour, texture, and vertex-colour-times-texture combines) and `dual_textured`/
   `dual_textured_colored` (two independent texture samples, `tex0.rgb*=2` doubling) fragment
   formulas exactly, proving each against known input colours/textures and reading back real
   rendered pixels.
4. Proves both triangle windings render identically (no culling), the alpha convention matches
   straight-vertex-colour-in/premultiplied-blend-out, and that `SkVertices` draws integrate cleanly
   with the existing SpriteBatch-driven draw order (a `drawVertices` call interleaved with ordinary
   `SpriteBatch::Draw` calls on the same canvas/target produces the expected combined image, with no
   state leakage in either direction).

Explicitly out of scope for this task (owned by later Phase S16 tasks): the versioned public SkSL
mesh/effect ABI naming/uniform/child-texture reflection rules (SKIA-154), any GLSL-source
translation (SKIA-155), compilation hardening/caching (SKIA-156), and public `ShaderEffect`/content
integration (SKIA-157). SKIA-153 only needs to prove the underlying `SkVertices` mechanism is sound
and pixel-correct; it does not wire a public API.
