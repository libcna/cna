# Skia effect compatibility boundary

This document is the SKIA-89 audit of CNA's effect surface against the pinned raster Skia API.
It separates the already-correct built-in SpriteBatch paint path from source-based custom effects;
support for one must not be inferred from the other.

## CNA effect routes

| CNA route | Observable contract | EasyGL route | Current Skia result |
|---|---|---|---|
| `SpriteBatch::Begin(..., effect=nullptr)` | Stock textured sprite, vertex tint, transform, sampler, blend, viewport/scissor and ordering semantics. | Built-in GL sprite program. | Direct `SkCanvas` image/paint path; already pixel-proven. It does not construct or compile a `SpriteEffect`. |
| Explicit `SpriteEffect` | Stock effect type whose `OnApply()` attempts to update `MatrixTransform`; it has no source or backend program. | Falls back to the built-in sprite program because `GetEffectBackendPtr()` is null. | Exact runtime type aliases the already-proven built-in path; clones work identically. Derived types reject so overridden behavior cannot be lost. |
| `ShaderEffect(device, vert, frag)` | NOXNA pair of backend-specific opaque byte strings retained for cloning and source access. | Treats both as GLSL ES shader source. | Untagged strings still return null. An exact `CNA_SKIA_SKSL_V1` first string opts the fragment string into the bounded SkSL SpriteBatch ABI below. |
| `Effect(device, effectCode)` | Compiled XNA `.fx` bytecode. | Shared constructor throws; no backend receives it. | Same shared, deterministic `NotImplementedException`; outside the SkSL bridge. |
| `ContentManager` custom `Effect` | `.cnj`/`.shader.json` names separate `vertex` and `fragment` text files. | Files are GLSL source. | Existing untagged descriptors remain invalid. A deliberately backend-specific descriptor can place the exact marker in its vertex payload, but the format still has no portable language tag. |
| Stock 3D effects | XNA properties feed vertex/fragment/depth/cube/lighting pipelines. | Dedicated GL programs. | Outside the 2D effect route; tracked by SKIA-93--103 and never implied by a runtime colour filter. |

`SpriteEffect::CacheEffectParameters()` currently looks up `MatrixTransform` in the base effect's
empty parameter collection, so its cached pointer is null and `OnApply()` is a no-op in CNA. The
real default SpriteBatch behavior comes from each backend's built-in sprite implementation. This
makes exact-type `SpriteEffect` recognition a compatibility alias, not a new programmable shader.

## Source-language and stage mismatch

| Property | CNA `ShaderEffect` / EasyGL | Pinned `SkRuntimeEffect` | Compatibility decision |
|---|---|---|---|
| Language identity | Constructor has no language enum. EasyGL examples use `#version 300 es`; Vulkan passes raw SPIR-V bytes in the same `std::string` parameters. | Accepts SkSL text only. | Never auto-detect or feed existing strings to SkSL. A future route needs an explicit SkSL contract. |
| Vertex stage | Full user vertex shader with named/location attributes, varyings and matrices. | No vertex program. `MakeForShader` requires `vec4 main(vec2 inCoords)`. | A Skia custom sprite subset can replace only fragment colour generation; arbitrary vertex source and all 3D layouts must reject. |
| Fragment result | GLSL fragment shader can discard, write location 0--3, use derivatives and backend GLSL features. | One premultiplied colour returned from `main`; public options default to SkSL 100 and note that raster remains largely ES3-unaware. | Single-output bounded 2D effects only. MRT, arbitrary discard/coverage and unsupported language features reject at compilation. |
| Coordinate model | SpriteBatch provides normalized UV varyings plus pixel-position vertex input. | Runtime shader receives local coordinates; child image shaders are evaluated explicitly. | The Skia adapter must define whether coordinates are source pixels, normalized UVs, or destination local pixels. It may not guess per shader. |
| Primary texture/tint | EasyGL binds sprite texture at unit 0 and supplies per-vertex `aColor`. | Image input is a `uniform shader` child; per-draw values are uniforms or composed filters. | A proposed SkSL sprite ABI must name the primary child and tint explicitly and preserve premultiplied/straight-alpha rules. |

## Parameter and texture mapping

| CNA API | EasyGL behavior | Public Skia representation | Status for a future explicit SkSL subset |
|---|---|---|---|
| `SetUniformFloat` | Named `float`. | Reflected `kFloat`. | Direct after exact name/type validation. |
| `SetUniformInt` | Named `int`. | Reflected `kInt`. | Direct after exact name/type validation. |
| `SetUniformVec2/3/4` | Named vectors. | Reflected `kFloat2/3/4`. | Direct after exact name/type validation. |
| `SetUniformMat4` | Column-major 4x4 upload. | Reflected `kFloat4x4` in a packed uniform block. | Viable only with tested matrix byte layout and coordinate convention. |
| `SetUniformFloatArray` | Scalar array; caller supplies scalar count. | Reflected array flag/count. | Viable with non-null data, non-negative count and exact declared-count checks. |
| `SetUniformVec2Array` | Vec2 array; caller supplies element count. | Reflected array flag/count. | Same bounded validation; byte count is `count * 2 * sizeof(float)`. |
| `SetTexture(unit, Texture2D)` | Binds a numeric sampler unit; GLSL separately names a sampler uniform. | Named `uniform shader` child, no numeric sampler-unit API. | Not source-compatible. An explicit name-to-child ABI or new API is required. |
| Primary SpriteBatch texture | Implicit unit 0 (`texture1` by convention in current tests). | Child image shader with chosen sampling/tile matrix. | Viable only through a documented reserved child name and the active SamplerState. |
| `SetTexture(unit, TextureCube/Texture3D)` | GLSL `samplerCube`/`sampler3D`. | Runtime-effect children are 2D shader/filter/blender objects. | Unsupported; CPU transfer storage does not create cube/volume samplers. |

The current `IEffectBackend` uniform methods have default no-op implementations, and
`ShaderEffect` silently skips every setter when backend construction returned null. Skia must not
use those no-ops as evidence of support. Any accepted explicit SkSL effect must instead report
missing names, wrong reflected types, invalid pointers/counts, unsupported children and compile
errors deterministically.

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
4. SKIA-92 may implement only reflected scalar/vector/matrix/array writes and named 2D child
   shaders proven by pixels. GLSL, SPIR-V, vertex stages, cube/volume sampling and unsupported
   SkSL must return actionable errors, never a successful no-op.
5. SKIA-93/94 evaluate stock effects separately. A runtime colour operation does not provide XNA
   vertex processing, depth, alpha-test coverage, dual-texture addressing or environment sampling.

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
- The prototype accepts exactly one `uniform shader cnaTexture0` child and exactly one non-array
  `uniform float4 cnaTint`. The child inherits SpriteBatch's filter, independent U/V address mode,
  source rectangle, origin and geometry transforms. `cnaTint` uses the same proven alpha-convention
  scale as the stock tint filter.
- Source is non-empty and at most 65,536 bytes; the reflected uniform block is at most 16,384
  bytes. Texture/target dimensions retain the backend-wide 16,384-axis and 256 MiB limits. Skia
  exposes no public compile-time or compiler-memory budget, so this prototype makes no stronger
  timeout claim.
- Wrong markers stay on the historical null-backend path. Tagged syntax errors preserve Skia's
  compiler text; wrong children/uniforms and size violations keep an invalid backend with a
  deterministic adapter diagnostic. A failed `Begin` clears pending custom state.
- General uniforms, additional 2D children and all cube/volume children remain unsupported until
  SKIA-92 proves their reflection, storage and pixel semantics. `CustomEffects` therefore remains
  false: this opt-in is not arbitrary EasyGL GLSL compatibility.

`Skia_Effect_Boundary` covers the unchanged untagged route. `Skia_SkSL_Effect_Prototype` proves
real compile and pixel output through `cnaTexture0`, compiler/ABI/size diagnostics, and immediate
stock-path reuse after a rejected tagged effect.
