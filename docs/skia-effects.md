# Skia effect compatibility boundary

This document is the SKIA-89 audit of CNA's effect surface against the pinned raster Skia API.
It separates the already-correct built-in SpriteBatch paint path from source-based custom effects;
support for one must not be inferred from the other.

## CNA effect routes

| CNA route | Observable contract | EasyGL route | Current Skia result |
|---|---|---|---|
| `SpriteBatch::Begin(..., effect=nullptr)` | Stock textured sprite, vertex tint, transform, sampler, blend, viewport/scissor and ordering semantics. | Built-in GL sprite program. | Direct `SkCanvas` image/paint path; already pixel-proven. It does not construct or compile a `SpriteEffect`. |
| Explicit `SpriteEffect` | Stock effect type whose `OnApply()` attempts to update `MatrixTransform`; it has no source or renderer program. | Falls back to the built-in sprite program because `GetEffectRendererPtr()` is null. | Exact runtime type aliases the already-proven built-in path; clones work identically. Derived types reject so overridden behavior cannot be lost. |
| `ShaderEffect(device, vert, frag)` | CNAEXT pair of renderer-specific opaque byte strings retained for cloning and source access. | Treats both as GLSL ES shader source. | Untagged strings still return null. An exact `CNA_SKIA_SKSL_V1` first string opts the fragment string into the bounded SkSL SpriteBatch ABI below. |
| `Effect(device, effectCode)` | Compiled XNA `.fx` bytecode. | Shared constructor throws; no renderer receives it. | Same shared, deterministic `NotImplementedException`; outside the SkSL bridge. |
| `ContentManager` custom `Effect` | `.cnj`/`.shader.json` names separate `vertex` and `fragment` text files. | Files are GLSL source. | Existing untagged descriptors remain invalid. A deliberately renderer-specific descriptor can place the exact marker in its vertex payload, but the format still has no portable language tag. |
| Stock 3D effects | XNA properties feed vertex/fragment/depth/cube/lighting pipelines. | Dedicated GL programs. | Outside the 2D effect route; tracked by SKIA-93--103 and never implied by a runtime colour filter. |

`SpriteEffect::CacheEffectParameters()` currently looks up `MatrixTransform` in the base effect's
empty parameter collection, so its cached pointer is null and `OnApply()` is a no-op in CNA. The
real default SpriteBatch behavior comes from each renderer's built-in sprite implementation. This
makes exact-type `SpriteEffect` recognition a compatibility alias, not a new programmable shader.

## Source-language and stage mismatch

| Property | CNA `ShaderEffect` / EasyGL | Pinned `SkRuntimeEffect` | Compatibility decision |
|---|---|---|---|
| Language identity | Constructor has no language enum. EasyGL examples use `#version 300 es`; Vulkan passes raw SPIR-V bytes in the same `std::string` parameters. | Accepts SkSL text only. | Never auto-detect or feed existing strings to SkSL. A future route needs an explicit SkSL contract. |
| Vertex stage | Full user vertex shader with named/location attributes, varyings and matrices. | No vertex program. `MakeForShader` requires `vec4 main(vec2 inCoords)`. | A Skia custom sprite subset can replace only fragment colour generation; arbitrary vertex source and all 3D layouts must reject. |
| Fragment result | GLSL fragment shader can discard, write location 0--3, use derivatives and renderer GLSL features. | One premultiplied colour returned from `main`; public options default to SkSL 100 and note that raster remains largely ES3-unaware. | Single-output bounded 2D effects only. MRT, arbitrary discard/coverage and unsupported language features reject at compilation. |
| Coordinate model | SpriteBatch provides normalized UV varyings plus pixel-position vertex input. | Runtime shader receives local coordinates; child image shaders are evaluated explicitly. | The Skia adapter must define whether coordinates are source pixels, normalized UVs, or destination local pixels. It may not guess per shader. |
| Primary texture/tint | EasyGL binds sprite texture at unit 0 and supplies per-vertex `aColor`. | Image input is a `uniform shader` child; per-draw values are uniforms or composed filters. | A proposed SkSL sprite ABI must name the primary child and tint explicitly and preserve premultiplied/straight-alpha rules. |

## Parameter and texture mapping

| CNA API | EasyGL behavior | Public Skia representation | Status in the explicit SkSL subset |
|---|---|---|---|
| `SetUniformFloat` | Named `float`. | Reflected `kFloat`. | Implemented with exact name, non-array type, offset and byte-size validation. |
| `SetUniformInt` | Named `int`. | Reflected `kInt`. | Implemented as one checked 32-bit SkSL integer. |
| `SetUniformVec2/3/4` | Named vectors. | Reflected `kFloat2/3/4`. | Implemented; a scalar/vector type mismatch throws rather than no-opping. |
| `SetUniformMat4` | Column-major 4x4 upload. | Reflected `kFloat4x4` in a packed uniform block. | Implemented; the column-2/row-1 byte position is exercised from caller array through SkSL pixel output. |
| `SetUniformFloatArray` | Scalar array; caller supplies scalar count. | Reflected array flag/count. | Implemented with non-null data, non-negative count and exact declared-count checks. |
| `SetUniformVec2Array` | Vec2 array; caller supplies element count. | Reflected array flag/count. | Implemented with the same checks and `count * 2 * sizeof(float)` byte validation. |
| `SetTexture(unit, Texture2D)` | Binds a numeric sampler unit; GLSL separately names a sampler uniform. | Named `uniform shader` child, no numeric sampler-unit API. | Units 1–7 map exactly to optional `cnaTexture1`–`cnaTexture7`; undeclared/out-of-range/null bindings throw. A weak renderer binding observes updates and expires safely on dispose. |
| Primary SpriteBatch texture | Implicit unit 0 (`texture1` by convention in current tests). | Child image shader with chosen sampling/tile matrix. | Reserved `cnaTexture0`; it inherits the active SpriteBatch sampler and source-coordinate mapping. |
| `SetTexture(unit, TextureCube/Texture3D)` | GLSL `samplerCube`/`sampler3D`. | Runtime-effect children are 2D shader/filter/blender objects. | Implemented as a bounded extension, unit 1 only: reserved `cnaCubeFace0`–`5` (cube, dominant-axis face table) / `cnaVolumeAtlas0` (volume, padded grid atlas) children, reachable through `cnaSampleCubeEXT`/`cnaSampleVolumeEXT` in the author's own SkSL. `BindTextureCube`/`BindTexture3D` reject only unit≠1, an effect that never calls the matching function (no children declared), a null renderer, an expired renderer, or (volume only) a padded atlas exceeding the 256 MiB budget -- not a blanket "unsupported." Weak-lifetime-tracked exactly like `SetTexture(unit, Texture2D)` above. See `docs/skia-cube-volume-sampling-contract.md` (SKIA-144–151). |

`IEffectRenderer` has compatibility no-op defaults and `ShaderEffect` skips setters when renderer
construction returned null. `SkiaEffectRenderer` overrides every applicable method: an accepted
tagged effect reports missing names, wrong reflected types, invalid pointers/counts/units,
unbound/disposed children and compile errors deterministically.

## Safe implementation sequence

1. SKIA-90 recognizes only the exact stock `SpriteEffect` and routes it to the unchanged built-in
   SpriteBatch implementation. Full-target default/explicit/clone pixels are identical; derived
   effects reject.
2. SKIA-91 implements an explicitly identified SkSL fragment-only prototype. Reusing untagged
   `ShaderEffect` strings is prohibited: only the exact first-string marker
   `CNA_SKIA_SKSL_V1` is the unambiguous opt-in.
3. Bound source length before compilation; validate reflected uniform bytes/counts and children
   before accepting the compiled effect; retain existing texture-dimension/allocation bounds.
   `SkRuntimeEffect::Options` provides a language-version ceiling but no public compile-time/memory
   budget.
4. SKIA-92 implements only the reflected scalar/vector/matrix/array writes and named 2D child
   shaders proven by pixels. GLSL, SPIR-V, vertex stages, cube/volume sampling and unsupported
   SkSL return actionable errors, never a successful no-op.
5. SKIA-93/94 evaluate stock effects separately. A runtime colour operation does not provide XNA
   vertex processing, depth, alpha-test coverage, dual-texture addressing or environment sampling.

## SKIA-93 composition spike

`Skia_Effect_Emulation_Spike` evaluates the fragment-like pieces independently from CNA's public
stock-effect draw route. This distinction is mandatory: proving a pixel equation on an axis-
aligned raster rectangle does not prove an effect whose public input is transformed triangles.

| Candidate piece | Exact Skia composition | Visual oracle | Public conclusion |
|---|---|---|---|
| `AlphaTestEffect` comparison/colour coverage | A runtime shader samples the source and returns a binary coverage mask to `SkCanvas::clipShader`; the real colour draw then uses the requested blender. | Alpha bytes 127/128/129 against reference 128 match all 24 results across the eight `CompareFunction` values. Failed pixels retain an opaque non-black sentinel even with source replacement. A control draw proves that returning transparent instead would erase the sentinel under `Opaque`/`kSrc`, so it is not discard. | Exact for a colour-only 2D draw. It does not suppress a missing depth/stencil write, define triangle edge coverage, provide vertex colour/fog interpolation, or process world/view/projection matrices; do not promote the stock effect yet. |
| `DualTextureEffect` fragment formula | One runtime shader evaluates two independently constructed image children, doubles only texture 0 RGB, then multiplies texture 1 and tint. | Four pixels combine texture-0 `Repeat` with texture-1 `Mirror` and produce the expected approximately 32/193/96/64 grayscale sequence. | The fragment equation and independent child sampler objects are feasible in one draw. The stock effect still requires a primitive route, two public sampler slots, matrices, vertex colour and vertex-derived fog. |
| Uniform colour transform | A runtime color filter composes with the image shader in the same paint. | Two asymmetric input pixels match a channel swizzle plus independent scale/bias values. | Sprite tint and explicit SkSL transforms remain viable single-pass 2D operations. A stock effect's per-vertex fog factor is not a uniform colour transform. |

The selected candidates allocate no intermediate render target, so there is no extra RGBA8
round-trip between stages. Dual texture performs two child samples in one paint; the colour filter
performs one source sample plus one filter evaluation. Alpha test is the expensive case: the source
is evaluated once for binary clip coverage and again for the colour draw, and a clip-stack entry is
created per draw. The raster renderer executes all of this on the owner CPU thread.

Runtime arithmetic uses Skia's float/half pipeline, while input/output surfaces remain
premultiplied RGBA8. The alpha oracle uses CNA's `0.5/255` threshold construction and exact stored
alpha bytes; the final target clamps/rounds once to RGBA8. Any future design that introduces an
intermediate surface would add another clamp, premultiplication and eight-bit quantization boundary
and therefore needs new oracles. These costs and precision limits are acceptable evidence for the
components, not evidence that the stock 3D effect types are complete.

## SKIA-94 stock-effect promotion decision

No additional stock effect is promoted. `SpriteEffect` remains the sole exact stock alias because
its observable CNA/EasyGL route is already the built-in SpriteBatch paint. `AlphaTestEffect` and
`DualTextureEffect` expose working CPU-side properties and `GpuDrawParams`, but their public draw
contract begins with a transformed vertex stream. On raster Skia, `DrawUserPrimitives` therefore
reaches the explicit public 3D guard before inspecting or packing that stream and before any
candidate fragment operation.

`Skia_StockEffect_Boundary` makes the decision observable rather than relying on capability prose:

- all eight alpha compare modes forward the same 24 below/equal/above decisions proven by the
  SKIA-93 clip spike, while every public draw rejects at `GraphicsDevice::DrawUserPrimitives`;
- all 16 combinations of dual texture availability, fog and vertex-colour enablement reject at the
  same boundary; a separate assertion proves both texture renderers, premultiplied diffuse/alpha,
  fog and vertex-colour state were forwarded rather than absent;
- every refused draw leaves a non-black raster sentinel byte-exactly unchanged;
- passing either stock 3D effect to SpriteBatch reports that the unsupported 3D primitive route is
  required, and the same batch immediately renders through its normal 2D path afterward;
- `ThreeD` and `CustomEffects` remain false.

The existing 78 AlphaTestEffect/DualTextureEffect property, clone, ownership, reference-scaling and
forwarding tests pass on the Skia selection under Xvfb. Existing EasyGL alpha/dual golden images
remain classified as 3D geometry oracles in `docs/skia-easygl-test-matrix.md`. Since neither stock
effect passed the complete public draw gate, SKIA-94 intentionally registers no Skia golden image;
doing so for only the fragment equation would mislabel an unsupported effect as promoted.

## Explicit SkSL SpriteBatch ABI v1

The SKIA-91 prototype deliberately reuses the two opaque `ShaderEffect` payload slots without
guessing their contents:

```cpp
ShaderEffect effect(device, "CNA_SKIA_SKSL_V1", R"(
    uniform shader cnaTexture0;
    uniform float4 cnaTint;
    half4 main(float2 sourcePixel) {
        return cnaTexture0.eval(sourcePixel) * half4(cnaTint);
    }
)");
```

- The first string must be exactly `CNA_SKIA_SKSL_V1`; there is no user vertex stage. The second
  string is compiled by `SkRuntimeEffect::MakeForShader` with the public default SkSL 100 ceiling.
- Every effect requires `uniform shader cnaTexture0` and non-array `uniform float4 cnaTint`.
  `cnaTexture0` inherits SpriteBatch's filter, independent U/V address mode, source rectangle,
  origin and geometry transforms. `cnaTint` uses the stock path's proven alpha-convention scale.
  Optional children are uniquely named `cnaTexture1` through `cnaTexture7`; they are weakly bound
  by the matching `SetTexture` unit and sampled as current premultiplied LinearClamp images.
- Up to 64 reflected uniforms may occupy the 16 KiB block. Besides reserved `cnaTint`, accepted
  types are non-array float/int/float2/float3/float4/float4x4 plus float and float2 arrays. Setters
  require the exact reflected name/type/count and valid data. Half/layout-colour flags and every
  unrepresentable vector/matrix/array type reject during ABI validation.
- Source is non-empty and at most 65,536 bytes; the reflected uniform block is at most 16,384
  bytes. Texture/target dimensions retain the renderer-wide 16,384-axis and 256 MiB limits. Skia
  exposes no public compile-time or compiler-memory budget, so this prototype makes no stronger
  timeout claim.
- Wrong markers stay on the historical null-renderer path. Tagged syntax errors preserve Skia's
  compiler text; wrong children/uniforms and size violations keep an invalid renderer with a
  deterministic adapter diagnostic. A failed `Begin` clears pending custom state.
- Cube/volume sampling is supported as a further bounded extension of this same v1 ABI
  (`cnaSampleCubeEXT`/`cnaSampleVolumeEXT`, reserved `cnaCubeFace0`-`5`/`cnaVolumeAtlas0` children,
  SKIA-144–151, `docs/skia-cube-volume-sampling-contract.md`), reachable only for an effect that
  actually calls one of those two functions in its own source. `CustomEffects` remains false
  regardless because the bounded fragment-only opt-in -- with or without cube/volume sampling -- is
  not arbitrary EasyGL GLSL compatibility.

`Skia_Effect_Boundary` covers the unchanged untagged route. `Skia_SkSL_Effect_Prototype` proves
real compile and pixel output through `cnaTexture0`, compiler/ABI/size diagnostics, and immediate
stock-path reuse after a rejected tagged effect.
`Skia_SkSL_UniformTexture` proves every accepted setter in one pixel equation, column-major matrix
layout, `cnaTint`, updated additional-texture sampling, source rectangle/transform/PointClamp,
deterministic negative cases, weak disposal safety and clone state/binding isolation.

## Explicit SkSL SpriteBatch Mesh ABI (SKIA-152–158)

The v1 ABI above is fragment-only: it paints one shader across the quad SpriteBatch already draws,
with no way for a caller to supply its own vertex positions, per-vertex colours or per-vertex UVs.
SKIA-152 surveyed every stock EasyGL effect against that limitation and found `SkMesh` -- Skia's
programmable-vertex-shader mesh API, the originally planned route to lift it -- to be a non-functional
stub on raster Skia in the pinned revision (`SkBitmapDevice::drawMesh` is a literal empty function
body; SKIA-153). SKIA-153 redesigned the approach around `SkVertices`/`SkCanvas::drawVertices`
instead, Skia's older, simpler mesh API, which genuinely rasterizes on this renderer but carries only
a **fixed, non-programmable** per-vertex attribute set: position (no W component -- perspective-
correct interpolation is architecturally impossible, not just unimplemented), optional texcoord,
optional colour. No custom vertex attribute, varying, or vertex-stage computation is possible through
this route; see `docs/skia-vertices-2d-effect-contract.md` for the full finding.

A second, separate marker distinguishes a mesh-ABI `ShaderEffect` from a v1 fragment-only one:

```cpp
ShaderEffect effect(device, "CNA_SKIA_SKSL_MESH_V1", R"(
    uniform shader cnaTexture0;
    uniform shader cnaTexture1;
    uniform float4 cnaTint;
    half4 main(float2 vUV) {
        half4 base = cnaTexture0.eval(vUV);
        base.rgb *= 2.0;
        return base * cnaTexture1.eval(vUV) * half4(cnaTint);
    }
)");
```

- The marker must be exactly `CNA_SKIA_SKSL_MESH_V1` (SKIA-154, `SkiaMeshEffectRenderer`/
  `SkiaMeshEffectAdapterEXT`). Unlike v1, there is no mandatory reserved texture/tint: per-vertex
  colour combines with the shader's output externally, through the `SkBlendMode` passed to
  `drawVertices` (`kModulate`), not as a shader input. Optional texture children reuse the same
  `cnaTexture0`-`7` naming convention as v1, but the unit range is `0..7` (all optional) rather than
  v1's reserved-0 `1..7` range, and reuse every existing `SkiaResourcePolicy.hpp` budget constant.
- The only route to draw a mesh-ABI effect is the new CNAEXT `SpriteBatch::DrawMeshEXT(effect,
  positions, colors, uvs, vertexCount, indices, indexCount)`, restricted to `SpriteSortMode::Immediate`
  -- a declared, tested scope boundary, since a mesh draw does not fit the shared quad-shaped
  deferred sort/batch queue every other `SpriteBatch` draw call uses. It throws for any other sort
  mode, for a non-mesh-ABI effect, and for any unbound texture child the shader actually samples.
- A restricted GLSL-to-SkSL translator (SKIA-155, `SkiaGlslToSkslTranslatorEXT`) additionally reaches
  this ABI from real EasyGL GLSL source, but only for the one fragment formula SKIA-152 proved has a
  direct SkSL equivalent (`DualTextureEffect`'s `base.rgb*=2.0; FragColor=base*tex2*tint;` combine).
  It is a restricted token-rewriter, not a general compiler front end, and unconditionally rejects
  every other construct (branching helper functions, `discard`, fog, lighting, PBR, cube/volume
  sampling, a second varying) with a source line/column rather than silently mistranslating; see
  `docs/skia-glsl-to-sksl-translator-contract.md` for the complete accepted grammar and rejection
  list. `sampler2D` uniforms are renamed to `cnaTexture0`-`7` in declaration order during translation
  -- SKIA-157's own public integration test caught this exact seam gap between two already-shipped,
  already-individually-tested pieces (the ABI and the translator) on its first run.
- The compiled-program cache (`SkiaMeshEffectCacheEXT`, SKIA-154/156) is source-keyed, growth-bounded
  (`kSkiaMeshEffectCacheMaxEntriesEXT = 64`, monotonic-tick LRU eviction) and shared across both the
  hand-written and translated routes into this ABI.
- `CustomEffects` remains false regardless: the mesh ABI is bounded to `dual_textured`'s core formula
  reached through one API entry point, not arbitrary EasyGL GLSL compatibility, matching the same
  rationale the v1 ABI's cube/volume extension already established above.

`Skia_MeshEffect_ABI`/`Skia_MeshEffect_Hardening` prove the ABI in isolation (compilation, uniform
setters, texture children, cache growth/eviction bounds, oversized-source rejection) below the public
API, matching this renderer's established "prove it below the API first" sequencing. `Skia_Vertices2D_Spike`
and `Skia_GlslTranslator` prove the `SkVertices` rasterization behaviour and the translator's accepted/
rejected grammar respectively, also below the public API. `Skia_MeshEffect_PublicApi` is the first and
only test to exercise the complete real path: raw untranslated GLSL rejected outright, colour-only and
textured mesh draws, non-`Immediate` sort mode rejected, `Clone()` producing independently-compiled
state, and the capstone check -- real EasyGL GLSL translated through `TranslateGlslToSkslEXT`, built
into a real mesh `ShaderEffect`, drawn through the real public `SpriteBatch::DrawMeshEXT` with two
real bound `Texture2D` children, and pixel-matched against `dual_textured`'s known formula result.

SKIA-158 closed Phase S16 by comparing this promoted formula against its own prior golden results
(SKIA-93's hand-written spike, SKIA-153's `SkVertices` spike, SKIA-155's translator differential
test) rather than a fourth, redundant golden image, and by sweeping every doc claim below into
agreement with what actually shipped; see `docs/skia-vertices-2d-effect-contract.md`'s closing
section for the final refused/promoted boundary statement.
