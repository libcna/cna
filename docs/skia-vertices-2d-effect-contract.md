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

CNA's Skia renderer creates its surfaces via `SkSurfaces::Raster(...)` (`SkiaSurface.cpp`), which is
backed by exactly this `SkBitmapDevice` class. `SkMesh`/`SkMeshSpecification` therefore draws nothing
at all on this renderer -- not a CNA gap, an upstream Skia stub in the pinned revision. The user was
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
side, submit only what should render" pattern used elsewhere in this renderer (e.g. `RasterizerState`
wireframe-before-cull ordering). SKIA-153's own spike proves both windings render *identically*
through `SkVertices` (since nothing culls), which is the correct, expected raster contract for this
API -- not evidence of a defect.

## Alpha convention

`SkDraw_vertices.cpp`'s `convert_colors` treats every caller-supplied `SkColor` vertex colour as
**unpremultiplied** (`kUnpremul_SkAlphaType` source), converting internally to premultiplied `float`
for rasterization -- ordinary `SkColor` semantics, matching how the rest of this Skia renderer already
treats straight-alpha 8-bit colour input elsewhere (`Color`'s own RGBA8 straight-alpha convention).
The vertex-colour and paint-shader (texture) contributions combine via the blend mode passed to
`drawVertices` -- `SkBlendMode::kModulate` (multiply) reproduces `BasicEffect`'s own
`vertexColour * textureSample * uDiffuseColor`-shaped combine (`docs/skia-easygl-effect-inventory.md`),
matching the default blend `SkCanvas::drawMesh` itself falls back to when no blender is supplied
(`SkBlender::Mode(SkBlendMode::kModulate)`), even though `drawMesh` cannot draw on this renderer --
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
translation (SKIA-155), compilation/cache hardening (SKIA-156), and public `ShaderEffect`/content
integration (SKIA-157). SKIA-153 only needs to prove the underlying `SkVertices` mechanism is sound
and pixel-correct; it does not wire a public API.

## SKIA-154: the mesh/effect ABI itself

### Marker

`CNA_SKIA_SKSL_MESH_V1`, reusing `ShaderEffect`'s existing two-opaque-string payload convention
exactly like `CNA_SKIA_SKSL_V1` does (the first string is the marker, never source; the second is
the SkSL 100 fragment program). A different marker value -- not a variant of the sprite marker --
so a mesh-mode program can never be silently accepted down the sprite path or vice versa; each
compiles through its own dedicated code path with its own validation rules.

### Why the fragment shader carries no reserved primary texture or tint

`CNA_SKIA_SKSL_V1`'s `cnaTexture0`/`cnaTint` are reserved because SpriteBatch always has exactly one
primary texture and one per-draw tint to hand the shader. A mesh draw has neither: SKIA-153's own
spike proved the vertex-colour contribution combines *outside* the shader, through the `SkBlendMode`
passed to `drawVertices` (`applyShaderColorBlend`), not as a value the shader itself reads. A
`colored`-only mesh (no texture at all) is a completely valid, common case (`docs/skia-easygl-effect-
inventory.md`'s `prog_colored_`). The mesh ABI's fragment `main(float2 p)` is therefore a plain,
self-contained SkSL function with **no mandatory reserved uniform or child** -- it may declare zero
or more optional texture children, reusing the exact same `cnaTexture0`-`7` reserved child-naming
convention as the sprite ABI (same 8-slot budget, same names) purely for implementation consistency
and reuse, not because a mesh draw has a "primary" texture in the sprite sense.

### Budgets

Reuses every existing `SkiaResourcePolicy.hpp` ceiling unchanged -- no new budget category:
`kSkiaSkslMaxSourceBytesEXT` (64 KiB source), `kSkiaSkslMaxUniformBytesEXT` (16 KiB reflected
uniform block), `kSkiaSkslMaxUniformCountEXT` (64 reflected uniforms), `kSkiaSkslMaxTextureUnitEXT`
(8 texture children, `cnaTexture0`-`7`). Reflected uniform types are the same accepted set as the
sprite ABI (non-array float/int/float2/float3/float4/float4x4, plus float/float2 arrays).

### Compilation cache

Every existing `ShaderEffect` construction -- sprite or mesh -- compiles its `SkRuntimeEffect` from
scratch; `Effect::Clone()` (`ShaderEffect.cpp`) constructs an entirely new `ShaderEffect` from the
same two source strings, which recompiles again. `SkRuntimeEffect::MakeForShader` itself has no
internal cache (confirmed: no cache-related declaration anywhere in `include/effects/
SkRuntimeEffect.h`). SKIA-154 gives the *mesh* ABI its own basic cache -- **not** applied to the
existing, already-shipped sprite ABI, which is out of this task's scope -- keyed by the exact
fragment source string (a `std::string` equality key is sufficient and unambiguous; no hashing
scheme is introduced). A cache hit returns the same immutable compiled program (the `sk_sp<
SkRuntimeEffect>` plus its reflected uniform-offset/type table and child-name/index table) without
invoking the SkSL compiler again. Cache entries are retained for the lifetime of the owning cache
object with no eviction policy in this task's scope -- bounding cache growth, time limits, and
malicious-input stress are SKIA-156's "harden" job, not this one's "define and implement" job.

**Clone isolation**: the cached object is immutable and shared read-only across every renderer
instance compiled from identical source. Each `SkiaMeshEffectRenderer` instance still owns its own
independent mutable uniform-value byte buffer (freshly zero-initialized to the compiled program's
own uniform block size on construction, exactly like the sprite ABI's `uniformBytes_`) and its own
independent `weak_ptr` array of bound texture children. Two instances sharing one cached compiled
program therefore never observe each other's `SetUniformX`/`BindTexture` calls -- a cache hit changes
*which compiled program object is referenced*, never *whose mutable state is shared*.

### What SKIA-154 built

A new, standalone below-the-API class, `SkiaMeshEffectRenderer` (not an `IEffectRenderer` override --
that interface is sprite-shaped around a single primary texture/tint the mesh ABI deliberately has
neither of), with a dedicated `SkiaMeshEffectCacheEXT` cache class, proven directly against
`SkCanvas::drawVertices` the same way SKIA-153's spike did (`Skia_MeshEffect_ABI`, 19 checks) --
still no public `ShaderEffect`/`SpriteBatch` wiring, which stayed SKIA-157's job.

### SKIA-157: reaching the real public API

SKIA-157 added a second, thin class, `SkiaMeshEffectAdapterEXT`, that *does* conform to
`IEffectRenderer` -- it wraps a `SkiaMeshEffectRenderer` and forwards every interface method, so a
`CNA_SKIA_SKSL_MESH_V1`-tagged `ShaderEffect` flows through `ShaderEffect`'s existing, completely
unmodified public surface. Drawing reuses `ISpriteBatchRenderer`'s established additive-virtual-
with-safe-default pattern: a new `DrawMeshEXT` method, implemented only by `SkiaSpriteBatchRenderer`,
reached from a new public `SpriteBatch::DrawMeshEXT` (CNAEXT) restricted to `SpriteSortMode::
Immediate`, since a mesh draw does not participate in the shared deferred sort/batch queue that
every ordinary `Draw()` overload's quad-shaped `SpriteInfo` does. See `plans/plan_skia.md`'s own SKIA-157
row for the full acceptance evidence, including a real integration bug the new public test caught:
the GLSL translator (SKIA-155) preserved each `sampler2D` uniform's original GLSL name, but the mesh
ABI requires the reserved `cnaTexture0`-`7` child-naming convention -- fixed in the translator, not
here, by renaming samplers to `cnaTexture0`-`7` in declaration order during translation.

### SKIA-158: the final programmable-effect boundary

Phase S16 (SKIA-152–158) closes with a bounded, but genuinely public and tested, programmable 2D
effect route. This section states that boundary plainly, as the closing task for both this contract
and `docs/skia-easygl-effect-inventory.md`'s survey.

**Promoted** (reachable through the real public API today):

- `DualTextureEffect`'s core fragment formula -- `base.rgb*=2.0; FragColor=base*tex2*tint;` -- drawn
  as a triangle mesh through `SpriteBatch::DrawMeshEXT`, restricted to `SpriteSortMode::Immediate`.
- Both a hand-written `CNA_SKIA_SKSL_MESH_V1` source for that formula (SKIA-154) and the same formula
  reached from real EasyGL GLSL source through the restricted GLSL-to-SkSL translator (SKIA-155),
  compiled and cached identically (SKIA-156's growth-bounded LRU cache).
- Up to eight optional 2D texture children (`cnaTexture0`-`7`) and the full existing reflected
  uniform-setter surface (scalars/vectors/matrices/arrays) v1 already established, reused unchanged.
- Fixed per-vertex position/texcoord/colour, straight-alpha vertex colour combined externally via
  `SkBlendMode::kModulate`, no winding/cull-mode distinction (`SkVertices` has none).

**Refused, by API limitation rather than by pending implementation**:

- Any custom vertex attribute, varying, or per-vertex computation beyond position/texcoord/colour --
  `SkMeshSpecification`, the API that would have carried these, is a non-functional stub on raster
  Skia (`SkBitmapDevice::drawMesh`); `SkVertices`, the promoted replacement, has no extension point
  for one. This is the `colored`/`textured`/`col_textured`/`lit_textured`/`pbr`/`pbr_skinned` bucket
  from `docs/skia-easygl-effect-inventory.md`.
- Arbitrary EasyGL GLSL: the translator unconditionally rejects every construct outside
  `dual_textured`'s exact accepted grammar (helper functions, branching-around-early-exit, `discard`,
  fog, lighting, PBR, cube/volume sampling, a second varying), each with a source line/column, never
  silently mistranslating.
- Perspective-correct interpolation: `SkVertices`' `SkPoint` position carries no W component, so this
  is architecturally impossible through this route, not merely unproven.
- All stock 3D effects, `DrawUserPrimitives`, and every construct already governed by
  `docs/skia-3d-emulation-adr.md`'s accepted `reject`/`3D-only` dispositions -- unaffected by
  SKIA-152–158, which added a new bounded 2D mesh route alongside the existing v1 fragment-only ABI,
  not a 3D capability.
- `CustomEffects` remains `false`: the mesh ABI is one proven formula reached through one API entry
  point, not arbitrary EasyGL GLSL compatibility.

No new Skia golden image is registered for SKIA-158. `dual_textured`'s pixel result was already
proven three times against independent methodologies -- SKIA-93's hand-written SkSL spike, SKIA-153's
`SkVertices` spike, and SKIA-155's translator differential test, each comparing against the same
known-correct formula result -- and SKIA-157's public-API test proved the identical result reachable
through the real `SpriteBatch::DrawMeshEXT`/`ShaderEffect` surface. A fourth golden image comparing
the same already-proven formula against itself would add no new evidence; see
`docs/skia-easygl-test-matrix.md` for this codebase's existing golden-image classification. A true
live dual-renderer (Skia vs. EasyGL) runtime comparison remains architecturally impossible under CNA's
one-renderer-per-build CMake selection, matching every prior phase's own "derive the expected value
from real EasyGL source text" golden methodology rather than a live side-by-side render.
