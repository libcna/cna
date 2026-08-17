# EasyGL stock-effect GLSL inventory (SKIA-152)

This is the SKIA-152 inventory required by Phase S16 (`plan_skia.md` SKIA-152–158): every checked-in
EasyGL stock-effect GLSL source, classified by language, vertex attributes/varyings, uniforms,
samplers, discard, derivatives, MRT, depth, and 3D dependencies, with a disposition and the
downstream task that owns its promotion (or its continued rejection).

## Scope

"EasyGL" here means CNA's internal implementation selected publicly as `OPENGLES3`, `OPENGL33`,
`WEBGL1`, or `WEBGL2` -- a real, full 3D-capable GL renderer, not the separate sibling `easy-gl`
GL-wrapper library it links against (that library owns no shader source of its own; it is a thin
Shader/Program/Buffer wrapper).
This is the same "EasyGL" `docs/skia-3d-emulation-adr.md` and its companion matrices measure Skia's
own 3D-refusal decisions against.

Every embedded shader lives in exactly one file, `src/Graphics/Renderers/EasyGL/EasyGLRenderer.cpp`
(5748 lines): thirteen `#version 300 es` vertex+fragment program pairs, compiled through a shared
`CompileAndLink(prog, vsrc, fsrc, label)` helper. `AlphaTestEffect` is not a separate program: a
`uniform vec4 uAlphaTest` plus an `if(_at<0.0)discard;` clause (reference/tolerance/pass/fail-sentinel
packed into one vec4) is baked identically into all twelve stock-3D programs; `AlphaTestEffect`
reuses whichever of `colored`/`textured`/`col_textured` matches its vertex layout, since XNA's
`AlphaTestEffect` has no lighting or skinning path of its own. Separately,
`EasyGLEffectRenderer::CompileProgram(vertSrc, fragSrc)` (~line 528) is the generic compile path for
arbitrary caller-supplied GLSL (custom `ShaderEffect` content) -- not a fixed source to classify the
same way, but the compile path SKIA-155's translator will eventually need to intercept.

No program in this corpus writes more than one fragment colour output (no MRT), writes
`gl_FragDepth`, or calls a screen-space derivative/LOD function (`dFdx`/`dFdy`/`fwidth`/`textureLod`)
-- confirmed by direct grep across the whole file, zero matches. Every sample is a plain, implicit-LOD
`texture()` call. These three columns are therefore omitted from the per-program table below (all
three are uniformly "no" / "N/A") rather than repeated fourteen times.

## Dispositions

Matching SKIA-152's own acceptance text exactly -- five values, no others:

- **direct SkSL**: the fragment-stage math alone, given already-resolved per-pixel inputs, could be
  re-expressed as an `SkRuntimeEffect` fragment shader today with no vertex-stage/geometry
  dependency -- a pure 2D per-pixel colour operation.
- **SkMesh**: needs real vertex-stage transform/interpolation (SKIA-153's own `SkMeshSpecification`
  prototype target), but the fragment math itself is expressible in SkSL once SkMesh supplies
  correctly-interpolated varyings.
- **restricted-translation**: a plausible SKIA-155 GLSL-to-SkSL source-translator target for the
  specific constructs it uses, once SkMesh handles the vertex stage.
- **3D-only**: fundamentally requires the rejected general 3D vertex/lighting/skinning pipeline per
  `docs/skia-3d-emulation-adr.md`'s accepted 2D-only decision; cannot be promoted without reopening
  that ADR.
- **impossible**: architecturally cannot work in Skia's raster CPU model regardless of ADR status
  (genuine MRT, real depth-buffer-dependent output, or a derivative-dependent LOD computation
  `SkRuntimeEffect` cannot provide). No row in this corpus currently needs this disposition -- see
  "Constant-negative findings" below.

A program's vertex and fragment stages can carry *different* dispositions when the fragment math is
separable from the vertex-stage 3D work; both are given where that split exists.

## Per-program inventory

| Program | Effect | Language notes | Vertex attrs / varyings | Uniforms | Samplers | 3D dependencies | Disposition |
|---|---|---|---|---|---|---|---|
| (inline, `EasyGLSpriteBatchRenderer::InitializeResources`) | `SpriteEffect` / built-in SpriteBatch | Trivial: one ortho `projection` mat4, no branching. | in `aPos`(vec2)/`aTexCoord`(vec2)/`aColor`(vec4); out `TexCoord`/`Color`. | `projection`(mat4) | 1x `sampler2D` | None -- pure 2D quad, no perspective. | **already implemented** -- `SpriteBatch::Begin(effect=nullptr)`'s direct `SkCanvas` paint path already reproduces this exactly (`docs/skia-effects.md`); not a Phase S16 target. |
| `prog_colored_` | `BasicEffect` (vertex colour) | Ternary alpha-test, `mix` fog blend. | in `aPos`(vec3)/`aColor`(vec4); out `vColor`/`vFogFactor`. | `uWVP`(mat4), `uDiffuseColor`, `uAlphaTest`, `uFogVector`(vec4), `uFogColor`, `uVertexColorEnabled` | 0 | Vertex: `uWVP*vec4(aPos,1)` clip-space transform. | Fragment **SkMesh** / Vertex **3D-only** -- pure colour combine once WVP-transformed `vColor` exists; the WVP transform itself needs real perspective-correct triangle rasterization. |
| `prog_textured_` | `BasicEffect` (textured) | Same pattern, one texture sample. | in `aPos`/`aUV`(vec2); out `vUV`/`vFogFactor`. | `uWVP`, `uDiffuseColor`, `uAlphaTest`, `uFogVector`, `uFogColor` | 1x `sampler2D` | Vertex: WVP transform. | Fragment **SkMesh** / Vertex **3D-only** -- same shape as `colored`, texture sample instead of vertex colour. |
| `prog_col_textured_` | `BasicEffect` (colour+textured) | Same + `uVertexColorEnabled` gate. | `aPos`/`aColor`/`aUV`; out `vColor`/`vUV`/`vFogFactor`. | as above + `uVertexColorEnabled` | 1x `sampler2D` | Vertex: WVP transform. | Fragment **SkMesh** / Vertex **3D-only** -- union of the two above; still a pure per-pixel combine once varyings exist. |
| `prog_lit_textured_` | `BasicEffect` (pixel-lit) | Three-light Blinn-Phong, hand-unrolled (not a real loop); `normalize`/`pow`/`step`. | in `aPos`/`aNormal`(vec3)/`aUV`; out `vNormal`/`vUV`/`vFogFactor`/`vWorldPos`. | `uWVP`, `uWorld`(mat4), `uNormalMatrix`(mat3), `uAmbientColor`, 3x(`uLightNDir`/`uLightNDiffuse`/`uLightNSpecular`), `uSpecularColor`, `uSpecularPower`, `uEyePosition`, `uEmissiveColor`, `uDiffuseColor`, `uAlphaTest`, `uFogVector`, `uFogColor` | 1x `sampler2D` | Vertex: `uNormalMatrix*aNormal` (inverse-transpose world), `uWorld*aPos` for `vWorldPos`. Fragment: per-fragment N.L/H.N Phong. | Fragment **SkMesh** / Vertex **3D-only** -- the Phong math has no dependency on triangle topology once `vNormal`/`vWorldPos`/`vUV` are correctly interpolated; SkMesh's own varying interpolation is exactly what is needed. |
| `prog_lit_textured_vertexlit_` | `BasicEffect` (vertex-lit) | Same Blinn-Phong math moved to the vertex stage; cross-stage precision-qualifier match required (documented GLSL ES 3.00 linker gotcha). | in as above; out `vUV`/`vFogFactor`/`vLitRGB`/`vSpecularRGB` (already-lit, no `vNormal`). | same set, evaluated in the vertex stage | 1x `sampler2D` | Lighting evaluated per-vertex from real geometry normals -- inherently Gouraud, only correct against a real triangle mesh. | **3D-only** (whole program) -- unlike its pixel-lit sibling, the lit colour computation itself *is* the vertex-stage 3D work; there is no separable 2D-fragment piece to extract. |
| `prog_dual_textured_` | `DualTextureEffect` | Trivial: `base.rgb*=2.0` doubling, two samples. | `aPos`/`aUV`; out `vUV`/`vFogFactor`. | `uWVP`, `uDiffuseColor`, `uAlphaTest`, `uFogVector`, `uFogColor` | 2x `sampler2D` | Vertex: WVP transform only. | Fragment **direct SkSL** / Vertex **SkMesh** -- the fragment formula (`tex0*2 * tex1 * tint`) is exactly what SKIA-93's own spike already proved pixel-correct as a 2-child SkSL shader (`docs/skia-effects.md`'s SKIA-93 table); only the primitive/vertex route around it needs SkMesh. |
| `prog_dual_textured_colored_` | `DualTextureEffect` (+vertex colour) | As above + vertex-colour gate. | `aPos`/`aColor`/`aUV`; out `vColor`/`vUV`/`vFogFactor`. | as `dual_textured` + `uVertexColorEnabled` | 2x `sampler2D` | Vertex: WVP transform only. | Fragment **direct SkSL** / Vertex **SkMesh** -- same as above with one added `vec4` multiply; still a pure per-pixel combine. |
| `prog_env_mapped_` | `EnvironmentMapEffect` | Vertex-stage per-vertex Fresnel (`pow(1-abs(N.E), factor)`, matching FNA's `ComputeFresnelFactor`, which itself runs per-vertex, not per-fragment); fragment does `reflect(-E,N)` + cube sample. | `aPos`/`aNormal`/`aUV`; out `vWorldNormal`/`vEyeDir`/`vUV`/`vFogFactor`/`vFresnel`. | `uWVP`, `uWorld`, `uNormalMatrix`, `uEyePosition`, `uEnvMapAmount`, `uFresnelEnabled`, `uFresnelFactor`, 2-light subset, `uEnvMapSpecular`, `uDiffuseColor`, `uEmissiveColor`, `uAlphaTest`, `uFogVector`, `uFogColor` | 1x `sampler2D` + 1x `samplerCube` (`uEnvMap`) | Vertex: world/normal-matrix/eye-position-derived `vWorldNormal`/`vEyeDir`, **per-vertex** Fresnel (not re-derivable per-fragment from an already-interpolated normal, per this file's own comment). | **3D-only** (whole program) -- the cube-reflection sample itself is architecturally identical to what SKIA-144–151's `cnaSampleCubeEXT` already implements and could run as ordinary 2D SkSL given an externally-supplied reflection direction, but this program's Fresnel blend factor is computed per-vertex from real mesh normals and is explicitly non-equivalent to a per-fragment recompute, so the program as a whole does not decompose into a SkMesh-clean fragment. Promoting environment-map-style reflection would need a genuinely new 2D effect design, not a translation of this shader. |
| `prog_skinned_` | `SkinnedEffect` (pixel-lit) | Bone-palette blend (`uBones[72]`, weights-per-vertex gated 1/2/4), inverse-transpose joint normal with a near-singular guard, then identical Blinn-Phong to `lit_textured_`. | `aPos`/`aNormal`/`aUV`/`aBoneWeights`(vec4)/`aBoneIndices`(`uvec4`)/`aColor`; out `vNormal`/`vUV`/`vFogFactor`/`vWorldPos`/`vColor`. | `uWVP`, `uWorld`, `uNormalMatrix`, `uBones[72]`(mat4 array), `uWeightsPerVertex`(int), full lighting set, `uVertexColorEnabled` | 1x `sampler2D` | Bone-weighted position plus inverse-transpose normal skinning -- fundamentally geometry/animation-driven. | **3D-only** (whole program) -- skinning is per-vertex bone-palette matrix blending with no 2D analogue; directly reopens the rejected `3D-FX-SKINNED`/`3D-MODEL-SKIN` ADR rows. |
| `prog_skinned_vertexlit_` | `SkinnedEffect` (vertex-lit) | Same skinning + vertex-stage lighting (same precision-qualifier gotcha as `lit_textured_vertexlit_`). | as `skinned_` but with lighting outputs (`vLitRGB`/`vSpecularRGB`) instead of `vNormal`. | superset of `skinned_`'s uniforms | 1x `sampler2D` | Bone skinning + per-vertex lighting. | **3D-only** (whole program) -- same reasoning as `skinned_`, compounded by vertex-stage-only lighting (same non-decomposability as `lit_textured_vertexlit_`). |
| `prog_pbr_` | `PbrEffect` | Cook-Torrance BRDF helper function `PbrLight(N,V,L,...)` (GGX `D`, Smith `G`, Schlick `F`); TBN construction; five texture reads including a normal map (`*2-1` unpack). | `aPos`/`aNormal`/`aTangent`(vec4)/`aUV`; out `vNormal`/`vTangent`/`vBitangentSign`/`vUV`/`vFogFactor`/`vWorldPos`. | `uWVP`, `uWorld`, `uNormalMatrix`, `uMetallicFactor`, `uRoughnessFactor`, 3-light set, `uEyePosition`, `uDiffuseColor`, `uAmbientColor`, `uEmissiveColor`, `uAlphaTest`, `uFogVector`, `uFogColor` | 5x `sampler2D` (base, normal, metallic-roughness, emissive, occlusion) | Vertex: TBN world-space construction, world/normal-matrix transform. | Fragment **restricted-translation** (also SkMesh-shaped) / Vertex **3D-only** -- `PbrLight()`'s Cook-Torrance math is pure per-pixel arithmetic (`pow`/`clamp`/`mix`/`dot`, no derivatives, no branching beyond the helper call) given N/T/B/V/L already resolved; a strong SKIA-155 grammar target once SkMesh supplies a correctly-interpolated TBN basis. |
| `prog_pbr_skinned_` | `SkinnedPbrEffect` | `prog_pbr_`'s exact fragment reused verbatim; vertex adds bone-palette position/tangent transforms and the inverse-transpose joint normal needed under non-uniform scale. | `aPos`/`aNormal`/`aTangent`/`aUV`/`aBoneWeights`/`aBoneIndices`; out matches `prog_pbr_` (no `vColor`). | `uBones[72]` + `uWeightsPerVertex` + `prog_pbr_`'s full set | 5x `sampler2D` | Bone-weighted skin of position plus the complete corrected TBN basis. | **3D-only** (whole program) -- fragment is byte-identical PBR math to `prog_pbr_` (same restricted-translation potential in isolation), but this program's vertex stage additionally requires bone skinning, the same blocker as `skinned_`; the whole program stays gated even though its fragment shader alone would not be. |

A render-target-source flip-V helper (`CNA_GL_RT_SAMPLE_UV_DECL`/`cnaSampleUV(uv, flip)`) is
preprocessor-injected into every fragment shader's texture-sample call across the corpus. It is a
renderer-specific construct with no SkSL equivalent need (Skia has no analogous render-target-source
V-flip convention to reconcile) -- SKIA-155's translator grammar must recognize and strip/reinterpret
this macro rather than attempt to translate it literally, for every row where it appears.

## Constant-negative findings

- **MRT**: zero programs write more than one fragment colour output.
- **Depth**: zero programs write `gl_FragDepth`; depth interaction is the implicit rasterizer test
  driven by `gl_Position.z`, outside any of these fragment shaders' own text.
- **Derivatives**: zero programs call `dFdx`/`dFdy`/`fwidth`/`textureLod`; every sample is a plain
  implicit-LOD `texture()` call.
- **Discard**: every one of the twelve stock-3D programs (all except the SpriteBatch shader) uses
  `discard` for `AlphaTestEffect`'s reference/tolerance test, packed into the shared `uAlphaTest`
  uniform and `if(_at<0.0)discard;` pattern -- byte-identical across all twelve.

Because nothing in this corpus currently exercises real MRT, `gl_FragDepth`, or a
derivative-dependent LOD call, the **impossible** disposition has zero rows to route to a task in
this inventory -- those architectural ceilings remain real (`docs/skia-effects.md`'s own "MRT,
arbitrary discard/coverage... reject" line is unaffected), they simply are not yet exercised by any
*currently checked-in* EasyGL source.

## Downstream task ownership

- **already implemented** (SpriteBatch/`SpriteEffect`): no action; pre-existing and unaffected by
  Phase S16.
- **direct SkSL** (`dual_textured`/`dual_textured_colored` fragment formulas): needed no `SkMesh`
  work for their fragment math -- the existing bounded `CNA_SKIA_SKSL_V1` two-child-texture ABI
  already expressed them, and SKIA-93's spike already proved the exact formula pixel-correct. The
  remaining gap was purely the primitive/vertex route around them; SKIA-153-157 closed it by
  promoting `DualTextureEffect`'s core formula through the new bounded `SkVertices`-based
  `CNA_SKIA_SKSL_MESH_V1` mesh ABI and its public `SpriteBatch::DrawMeshEXT` entry point (see
  `docs/skia-vertices-2d-effect-contract.md`), not through the originally-planned `SkMesh` route.
- **SkMesh** (`colored`/`textured`/`col_textured`/`lit_textured` fragment stages, `pbr`/`pbr_skinned`
  fragment stages): SKIA-153 found `SkMeshSpecification`/`SkMesh` to be a non-functional stub on
  raster Skia in the pinned revision (`SkBitmapDevice::drawMesh` is a literal empty function body),
  so this route is closed, not pending. The promoted replacement, `SkVertices`, carries only a
  fixed, non-programmable per-vertex attribute set (position, optional texcoord, optional colour --
  no normal, tangent, or arbitrary varying), so these rows' custom vertex attributes remain refused
  by that API limitation; see `docs/skia-vertices-2d-effect-contract.md` for the full finding.
- **restricted-translation** (`dual_textured`'s fragment formula, as the concrete grammar target):
  SKIA-155 scoped its translator grammar to exactly `dual_textured`'s core formula (two `sampler2D`
  uniforms, one `vec4` tint uniform, one UV varying, straight-line combine), not `PbrLight()` --
  `PbrLight()` needs helper-function definitions and `mat3` construction, both explicitly outside
  this MVP grammar (`docs/skia-glsl-to-sksl-translator-contract.md`). Widening the grammar toward
  `PbrLight()` remains a real, still-open next acceptance bar, but it was not attempted in SKIA-155.
- **3D-only** (`lit_textured_vertexlit`, `env_mapped`, `skinned`, `skinned_vertexlit`,
  `pbr_skinned`, plus the *vertex stage* of every other row): unaffected by SKIA-153–158; these
  remain governed by `docs/skia-3d-emulation-adr.md`'s existing accepted `reject`/`3D-only`/
  `bounded-2d-sampling` dispositions and require a new ADR to reopen, not a Phase S16 task.
- **impossible**: no rows in this inventory; see "Constant-negative findings" above.

## Relationship to the SKIA-144–151 cube/volume sampling extension

`prog_env_mapped_`'s fragment-stage `texture(uEnvMap, reflectDir)` cube sample is architecturally the
same operation SKIA-144–151's `cnaSampleCubeEXT` already implements as a bounded 2D SkSL extension
(`docs/skia-cube-volume-sampling-contract.md`). That extension does not promote this row: the blocker
for `EnvironmentMapEffect` is its per-vertex Fresnel term and WVP/normal-matrix vertex transform, not
the cube sample itself. A hypothetical future 2D "reflection map" SkSL effect, given an externally
supplied reflection direction and Fresnel factor as ordinary 2D uniforms, could reuse
`cnaSampleCubeEXT` directly -- but that would be a new effect design exercise for a later task, not a
translation of `prog_env_mapped_`.
