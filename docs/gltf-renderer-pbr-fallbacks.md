# glTF PBR texture bindings and fallbacks across renderers

Every `PbrEffect`/`SkinnedPbrEffect` material has the same five core logical resources, in this
order: base colour, normal, metallic-roughness, emissive and occlusion. `KHR_materials_specular`
adds independent strength and colour resources after those five. “The same order” does not mean
every native API has the same binding numbers. `GltfRendererPbrFallbackPolicy` locks the complete
CPU-field → native binding → shader declaration chain for each implementation, including both
rigid and skinned fragment variants where a renderer stores them separately.

## Texture-slot ABI (`GLTF-373`)

| Renderer | Native binding (core five, then extension slots where implemented) |
|---|---|
| Bgfx | stages `0 / 1 / 2 / 3 / 4 / 5 / 6` (`s_texColor`, `s_texNormal`, `s_texMetallicRoughness`, `s_texEmissive`, `s_texOcclusion`, `s_texSpecular`, `s_texSpecularColor`) |
| Diligent | name-bound `g_Texture / g_NormalMap / g_MetallicRoughnessMap / g_EmissiveMap / g_OcclusionMap`; sampler-state slots `0 / 1 / 2 / 3 / 4` |
| DirectX 9 | sampler registers `s0 / s1 / s2 / s3 / s4` |
| DirectX 11 | SRV/sampler register pairs `t0/s0` through `t4/s4` |
| DirectX 12 | five separate SRV tables `t0..t4` followed by five sampler tables `s0..s4` |
| EasyGL | GL texture units `0 / 1 / 2 / 3 / 4`, explicitly written into the five sampler uniforms |
| LLGL | texture bindings `2 / 4 / 6 / 8 / 10`, paired samplers `3 / 5 / 7 / 9 / 11` |
| Magnum | GL texture units `0 / 1 / 2 / 3 / 4`, explicitly written into the five sampler uniforms |
| Metal | fragment texture/sampler indices `0 / 1 / 2 / 3 / 4` |
| OpenGL 2 | GL texture units `0 / 1 / 2 / 3 / 4`, explicitly written into `uTex` plus four map uniforms |
| OpenGL 4 | GL texture units `0 / 1 / 2 / 3 / 4`, explicitly written into the five sampler uniforms |
| SDL GPU | fragment sampler bindings `0 / 1 / 2 / 3 / 4` (shader set 2) |
| Vulkan | descriptor-set 0 bindings `0 / 1 / 2 / 3 / 4` |
| WebGPU | bind-group 1 texture bindings `1 / 2 / 3 / 4 / 5`; binding 0 is its sampler |
| Wicked | HLSL resources `t0 / t3 / t4 / t5 / t6`; `t1` and `t2` remain the non-PBR second texture and environment cube |

The executable audit is deliberately source-based as well as runtime-based. It discovers the PBR
renderer inventory, so a sixteenth implementation cannot silently sit outside the table. It then
requires every mapping fragment above and checks the separately stored D3D9, shared D3D11/12 and
Vulkan rigid/skinned shader files independently; the two inline EasyGL/WebGPU variants must each
contain their complete declaration set.

`EasyGL_Pbr_TextureSlots` and `Vulkan_Pbr_TextureSlots` add independent real-pixel evidence. Their
texture-slot portion runs fourteen cases each (seven maps × rigid/skinned) with semantic sentinel
texels: red base colour, green metallic-roughness (`G=1`, `B=0`), blue emissive,
channel-asymmetric occlusion, a tilted normal, zero-alpha specular strength and red specular colour.
The linearly rendered expected bytes are analytically stated in the test; a swap cannot pass by
showing merely that “some texture” was sampled. `EasyGL_Pbr_MaterialMaps` additionally covers
output transfer and supplies a second scalar-semantics oracle at the same binding boundary.

## Packed texture channels (`GLTF-226`, `GLTF-227`, `GLTF-233`, `GLTF-234`, `GLTF-379`)

Every one of the 15 PBR shader implementations now has source-audit evidence for the same glTF
packing: the normal map consumes RGB and remaps it with `*2−1`, metallic-roughness consumes G as
roughness and B as metallic, and occlusion consumes R. The audit requires every separately stored
rigid/skinned fragment. It also requires all three LLGL representations: the GL and Vulkan sources
and the generated embedded GL copy, preventing a regenerated header from silently retaining stale
channel semantics.

The shared texture-slot executable is the discriminating L7 oracle required by `GLTF-234`.
OPENGLES3 and Vulkan both pass all ten rigid/skinned slot cases. Its MR texel is pure green
(`G=1`, `B=0`), which produces a fully rough dielectric; swapping G/B instead produces the sharply
different near-smooth metallic case. Occlusion `(64,128,192)` distinguishes the required R byte
from G and B, while normal `(255,128,191)` distinguishes the full RGB remap from the geometric
normal and from an unremapped UNORM sample.

## PBR colour transfer (`GLTF-210`–`GLTF-213`, `GLTF-379`)

All imported images remain ordinary RGBA8 UNORM textures. Colour meaning therefore travels as
three independent draw flags: decode the base-colour sample, decode the emissive sample, and encode
the final PBR RGB output. The first two follow the glTF texture roles and default on; the third is a
destination policy and can be disabled for an sRGB render target or a later tone-map pass. Normal,
metallic-roughness and occlusion stay linear and have no configurable transfer flag. Factors are
already linear and are multiplied only after the relevant texture sample is decoded.

When output encoding is enabled, fog is mixed in linear space too: the shader decodes the fog
colour, performs the mix, and encodes RGB once at the end. Alpha is never transferred. The
renderer-specific carriers are:

| Renderer | Base decode / emissive decode / output encode carrier | Focused evidence |
|---|---|---|
| Bgfx | `u_srgb.x / .y / .z` | OpenGL `Bgfx_Pbr_SrgbTransfer`, 12 exact rigid/skinned cases |
| Diligent | `g_PbrMapScales.z / .w`, `g_FogColor.w` | Vulkan and OpenGL `Diligent_Pbr_SrgbTransfer`, 12/12 on each API |
| DirectX 9 | existing pixel constants `c1.w / c2.w / c12.w` | Microsoft `ps_3_0` compile plus WineD3D diagnostic 12/12; no DXVK prefix was available |
| DirectX 11 / 12 | shared `PbrMapScales.z / .w`, `FogColor.w`; constant-buffer sizes unchanged | Microsoft `ps_5_0` compile and both MinGW frontends; D3D11 WineD3D diagnostic 12/12, D3D12 executable build-only because its Wine swap-chain gate remains unavailable |
| EasyGL | `uSrgb.x / .y / .z` | `EasyGL_Pbr_SrgbTransfer`, exact rigid/skinned cases on OPENGLES2 and OPENGLES3 |
| LLGL | unused `.w` lanes of ambient, eye-position and light-0 direction | `Llgl_Pbr_SrgbTransfer` on its Vulkan and OpenGL paths |
| Magnum | `uSrgb.x / .y / .z` | `Magnum_Pbr_SrgbTransfer` |
| Metal | `PbrUniforms::srgbFlags.x / .y / .z` | MSL source/ABI tests on Linux; real-device execution remains platform-owned |
| OpenGL 2 / 4 | `uSrgb.x / .y / .z` | each backend's registered `Pbr_SrgbTransfer` executable |
| SDL GPU | third `PbrParams` vec4, `.x / .y / .z` | `SdlGpu_Pbr_SrgbTransfer` |
| Vulkan | `srgbFlags.x / .y / .z` in the PBR UBO | `Vulkan_Pbr_SrgbTransfer`, 12/12 on rigid/skinned SPIR-V |
| WebGPU | third `PbrFactors` vec4, `.x / .y / .z` | `WebGPU_Pbr_SrgbTransfer`, rigid and skinned WGSL |
| Wicked | `cb.pbrSrgb.x / .y / .z` | `Wicked_Pbr_SrgbTransfer`, 12/12 on Intel Vulkan with runtime DXC |

The common mid-grey oracle uses texture byte 128. Decode plus encode must round-trip to 128, while
linear bypass plus encode yields 188. A linear factor of 0.5 after decode yields 92, and adding
quarter-strength base to half-strength emissive in linear space yields 112. Each case runs through
both `PbrEffect` and identity-skinned `SkinnedPbrEffect`; endpoint-only samples could not distinguish
the paths. `EveryPbrShaderHonorsColorSpaceDeclarations` additionally requires all three CPU flags
and all three shader equations in every one of the 15 PBR implementations. It requires two copies
for separately stored rigid/skinned fragments, so one corrected variant cannot hide a stale sibling.

## Core material factors (`GLTF-216`–`GLTF-223`, `GLTF-379`)

Texture decoding and channel selection do not prove that the imported factors are actually used.
Every PBR fragment must independently multiply decoded base RGB by the RGB base-colour factor,
multiply texture alpha by the alpha factor, multiply the emissive sample by the emissive factor,
and apply roughness/metallic factors to the required G/B channels. The last two expressions are
shared with the packed-channel audit above; `EveryPbrShaderConsumesTheCoreMaterialFactors` joins
them to the three colour-factor expressions in one 15-renderer inventory:

| Renderer | Base RGB / alpha carrier in the fragment | Emissive carrier | Required fragment copies |
|---|---|---|---:|
| Bgfx | `u_diffuseColor.rgb / .a` | `u_emissiveColor.xyz` | 1 |
| Diligent | `g_DiffuseColor.rgb / .a` | `g_PbrEmissiveRoughness.xyz` | 1 |
| DirectX 9 | `DiffuseColor.rgb / .a` | `EmissiveColor.xyz` | 2 |
| DirectX 11 | `DiffuseColor.rgb / .a` | `EmissiveRoughness.xyz` | 2 |
| DirectX 12 | `DiffuseColor.rgb / .a` | `EmissiveRoughness.xyz` | 2 |
| EasyGL | `uDiffuseColor.rgb / .a` | `uEmissiveColor` | 2 |
| LLGL | `diffuseColor.rgb / .a` | `emissiveMetallic.xyz` | 3 |
| Magnum | `uDiffuseColor.rgb / .a` | `uEmissiveColor` | 1 |
| Metal | `pu.diffuseColor.rgb / .a` | `pu.emissiveColor.xyz` | 1 |
| OpenGL 2 | `uDiffuse.rgb / .a` | `uEmissiveColor` | 1 |
| OpenGL 4 | `uDiffuseColor.rgb / .a` | `uEmissiveColor` | 1 |
| SDL GPU | `pc.diffuseColor.rgb / .a` | `lp.emissiveColor_pad.xyz` | 1 |
| Vulkan | `pc.diffuseColor.rgb / .a` | `pbr.emissive_roughness.xyz` | 2 |
| WebGPU | `u.diffuseColor.rgb / .a` | `lp.emissiveColor.xyz` | 2 |
| Wicked | `cb.diffuse.rgb / .a` | `cb.emissive.rgb` | 1 |

The counts name stored shader variants rather than renderer draw calls. LLGL's three copies are its
Vulkan-style source, native-GL source and generated native-GL header. Each row asserts the complete
multiplication expression, not merely the presence of a uniform declaration or CPU upload, so a
factor that reaches the backend but is shading-inert fails this ordinary no-GPU test.

## World/View/Projection transport (`GLTF-266`, `GLTF-366`, `GLTF-379`)

The L6 oracle proves that imported World and the caller's View/Projection reach the effect boundary
unchanged. `EveryPbrVertexPathConsumesWorldViewProjection` locks the next hop in every renderer:
the CPU composes XNA's row-vector `World * View * Projection`, uploads the combined matrix and an
independent World matrix, and both the rigid position and the post-skin position consume that
combined matrix for clip-space output. This is deliberately a PBR-path audit; a matching stock
basic or generic skinning shader elsewhere cannot satisfy it.

| Renderer | Combined clip-transform carrier | Independent World carrier |
|---|---|---|
| Bgfx | `u_wvp` | `u_world` |
| Diligent | `g_WorldViewProj` | `g_World` |
| DirectX 9 | `WorldViewProj` vertex constants | `World` vertex constants |
| DirectX 11 / 12 | PBR per-draw `Mvp` | PBR per-draw `World` |
| EasyGL | `uWVP` | `uWorld` |
| LLGL | `PbrParams::mvpMatrix` | `PbrParams::worldMatrix` |
| Magnum | `uWVP` | `uWorld` |
| Metal | rigid/skinned PBR transform `wvp` | transform `world` |
| OpenGL 2 | `uWVP` | `uWorld` |
| OpenGL 4 | `uWorldViewProj` | `uWorld` |
| SDL GPU | primary constants `mvp` | `LitLightParams::world` |
| Vulkan | push constants `mvp` | `PbrParams::world` |
| WebGPU | primary uniforms `mvp` | `LitLightParams::world` |
| Wicked | `cb.mvp0..3` | `cb.world0..3` |

The carriers are native-API representations, not a claim of byte-identical matrix storage. For
example, LLGL applies its required clip-range correction after XNA composition, while backends
using column-vector shaders transpose on upload. The audit pins each such conversion at the CPU
site and its matching shader multiplication, including explicit scoping of both inline WebGPU PBR
programs so unrelated WGSL cannot produce a false positive.

## Caller-owned double-sided culling (`GLTF-231`, `GLTF-232`, `GLTF-379`)

`doubleSided` is intentionally carried by `PbrEffect`/`SkinnedPbrEffect`, not applied as a hidden
side effect of `Model::Draw`. The application selects `RasterizerState::CullNone` for a two-sided
part (or the appropriate front-face cull state for a single-sided/mirrored placement), and
`GraphicsDevice::setRasterizerStateProperty` forwards that state to the active renderer. The final
cross-renderer audit locks the native endpoint and PBR route of that contract:

| Renderer | Caller-owned cull carrier consumed by PBR |
|---|---|
| Bgfx | draw `cullFlags_`; zero means no cull for rigid and skinned PBR submits |
| Diligent | `PipelineKey::raster` → `CULL_MODE_NONE`; PBR variants use the same keyed pipeline |
| DirectX 9 | device `D3DRS_CULLMODE` → `D3DCULL_NONE`; both PBR variants share `DrawPbrEffectEXT` |
| DirectX 11 | bound rasterizer state → `D3D11_CULL_NONE`; PBR does not replace it |
| DirectX 12 | `currentCullMode_` → PBR PSO key/descriptor → `D3D12_CULL_MODE_NONE` |
| EasyGL | GL cull-face enable; `CullNone` disables it before either selected PBR program |
| LLGL | `cullMode_` in the immutable PBR pipeline key → `LLGL::CullMode::Disabled` |
| Magnum | Magnum GL face-culling feature; disabled state survives PBR program selection |
| Metal | tracked `MTLCullModeNone`, rebound on the encoder used by rigid/skinned PBR |
| OpenGL 2 / 4 | `GL_CULL_FACE` disabled before the rigid/skinned PBR draw |
| SDL GPU | queued `RenderStateSnapshot` → rigid/skinned PBR pipeline rasterizer state |
| Vulkan | queued `cullMode` → distinct rigid/skinned PBR pipeline keys → `VK_CULL_MODE_NONE` |
| WebGPU | queued `cullMode` → distinct rigid/skinned PBR pipeline keys → `WGPUCullMode_None` |
| Wicked | caller state copied into the PBR `WickedPipelineKey` → `wig::CullMode::NONE` |

The immutable Vulkan and WebGPU rigid/skinned pipeline owners are scoped separately in the test;
a correct generic pipeline cannot mask a PBR hardcode. The discriminating runtime witness remains
`EasyGL_Gltf_AlphaBlend`: the ordinary single-sided state culls a deliberately reversed face, then
the imported `doubleSided` property selects `CullNone` and renders the same face on OPENGLES2 and
OPENGLES3. This proves the property is consumed without changing the architectural boundary.

## Skinned palette and influence count (`GLTF-258`, `GLTF-263`, `GLTF-379`)

Every skinned PBR backend receives the same 72-entry column-major palette and XNA's requested
`WeightsPerVertex` value (1, 2 or 4). The vertex shader always starts with pair zero, adds pair one
only at `>=2`, and adds pairs two and three only at `>=4`. This matters even though glTF imports
four stored components: an application may set the effect's public influence count after loading,
and stale tail values must not silently contribute. The per-renderer carriers are:

| Renderer | Palette carrier | Influence-count carrier used by the PBR vertex shader |
|---|---|---|
| Bgfx | `u_bones` | `u_weightsPerVertex.x` |
| Diligent | `g_Bones` | `g_PbrFlags.w` |
| DirectX 9 | `Bones` vertex constants | `FogParams.w` |
| DirectX 11 | PBR skinned `Bones` constant buffer | `EyePosWeights.w` |
| DirectX 12 | PBR skinned `Bones` constant buffer | `EyePosWeights.w` |
| EasyGL | `uBones[72]` | `uWeightsPerVertex` |
| LLGL | `bones` storage block | `roughnessWeightsPad.y` |
| Magnum | `uBones` | `uWeightsPerVertex` |
| Metal | vertex bone buffer | `SkinnedPbrTransform::skinParams.x` |
| OpenGL 2 | `uBones[72]` | `uWeightsPerVertex` |
| OpenGL 4 | `uBones[72]` | `uWeightsPerVertex` |
| SDL GPU | `bb.bones` | `lp.eyePos_weightsPerVertex.w` |
| Vulkan | `bb.bones` | `pbr.fogColorEnabled.w` |
| WebGPU | `sk.bones` | `sk.weightsPerVertex.x` |
| Wicked | `bones.boneColumns` | `bones.skinParams.x` |

`EverySkinnedPbrShaderConsumesThePaletteAndInfluenceCount` locks each CPU upload plus both gates.
It is paired with `EverySkinnedPbrShaderInverseTransposesTheJointMatrix`, whose PBR-specific
inventory proves that the gated blend is the matrix the actual PBR direction path consumes; a
generic stock skinning shader elsewhere in the same renderer cannot substitute for that evidence.

## PBR dielectric Fresnel endpoints (`GLTF-343`, `GLTF-344`)

`GpuDrawParams::pbrDielectricF0` carries the RGB normal-incidence endpoint after applying
`KHR_materials_ior` and factor-only `KHR_materials_specular`; `pbrDielectricF90` separately carries
the grazing endpoint after specular strength. Every PBR shader mixes F0 with base-colour albedo by
metallic, mixes F90 with one by metallic, and evaluates Schlick as `F0 + (F90 - F0) * (1-V·H)^5`.
Reconstructing F90 as one would make reduced `specularFactor` wrong at grazing angles even when the
normal-incidence image looked correct.

| Renderer | Native F0/F90 carrier | Focused evidence |
|---|---|---|
| Bgfx | `u_dielectricFresnel` | four shader dialects compile; OpenGL rigid/skinned pixel test |
| Diligent | fourth row of the 76-float PBR block, `g_PbrDielectricFresnel`; textured state in row five and four extension-transform rows | Vulkan and OpenGL rigid/skinned, material-map and seven-slot pixel tests |
| DirectX 9 | pixel constant `c13` | Microsoft `ps_3_0` compile/disassembly plus WineD3D diagnostic |
| DirectX 11 / 12 | `D3DPbrPerDrawConstants::DielectricFresnel` at byte 208; textured specular inputs at bytes 400–495 | Microsoft `ps_5_0` compile and both MinGW frontends; D3D11 WineD3D diagnostic |
| EasyGL | `uDielectricFresnel` | OPENGLES2/3 analytic rigid/skinned pixel test |
| LLGL | final `vec4` in the 92-float PBR block | Vulkan and OpenGL registrations plus shader compilation |
| Magnum | `uDielectricFresnel` | generated GLSL compile and rigid/skinned pixel test |
| Metal | `PbrUniforms::dielectricFresnel` | MSL source/ABI unit tests on Linux; device execution remains platform-owned |
| OpenGL 2 / 4 | `uDielectricFresnel` | each backend's rigid/skinned pixel test |
| SDL GPU | fourth `PbrParams` vec4 | regenerated SPIR-V and rigid/skinned pixel test |
| Vulkan | `dielectricFresnel` at bytes 240–255 of its 256-byte dynamic PBR UBO | rigid/skinned SPIR-V pixel test |
| WebGPU | fourth `PbrFactors` vec4 | rigid and skinned WGSL pixel test |
| Wicked | `cb.pbrDielectricFresnel` | runtime DXC plus rigid/skinned Vulkan pixel test |

The shared six-check oracle uses a fully rough black dielectric. At normal incidence the direct
light reduces to `F0/(4π)`, yielding byte 11 for core F0 and channel-separated `(2,9,43)` for the
authored extension factors. A grazing pair holds F0 at `.04` and changes only F90 from 1 to .3,
yielding bytes 33 and 15. `EveryPbrShaderHonorsTransportedFresnelEndpoints` separately inventories
all 15 CPU uploads, dielectric/metal endpoint mixes and Schlick expressions, with explicit counts
for separately stored rigid/skinned shader sources. The optional `specularTexture` and
`specularColorTexture` are not part of this factor-only slice. EasyGL, OpenGL2/4, DirectX9/11/12,
Bgfx, Diligent, Magnum, SDL GPU and Vulkan now consume both; the other four renderer bindings remain the named `GLTF-344`
limit. DirectX9's two ps_3_0 variants use 7 texture and 271 arithmetic instruction slots (278 total of the 512-slot
limit), with compiler-extracted c24–c29 constants and s5/s6 samplers.
Bgfx binds identity-white stages 5/6 and carries their independent transforms, sRGB colour decode
and seven-bit UV selector through rigid/skinned stride-48/60/68/76 layouts. All OpenGL, ESSL,
SPIR-V and WGSL variants compile; llvmpipe OpenGL passes 21 material-map, 22 texture-slot, 7
analytic Fresnel and 12 sRGB rigid/skinned pixel checks under Xvfb.
Magnum's shared rigid/skinned GLSL binds white units 5/6, and both its six-check analytic Fresnel
witness and existing six-check metallic-roughness PBR test pass on llvmpipe under Xvfb.
SDL GPU's shared rigid/skinned GLSL binds the two white fallbacks at bindings 5/6 with independent
imported sampler state, transforms and colour decode. Its regenerated 12,656-byte SPIR-V fragment,
both three-pixel PBR programs and the seven-check Fresnel/factor oracle pass under Xvfb.
Vulkan's rigid layout binds the five core maps at 0–4, its 496-byte PBR block at 5 and the two
extension maps at 6/7; skinned inserts the bone block at 5 and moves the PBR block and extension
maps to 6–8. Four rigid/skinned single/dual-UV shader pairs carry a seven-bit selector and separate
extension transforms. Validation-clean lavapipe runs pass the 22-check texture executable, both
8-check golden programs and the seven-check Fresnel/factor oracle under Xvfb.
Diligent declares seven independent named texture resources and attaches each public sampler slot
before binding. Its 304-byte constant block carries pre-clamp F0, scalar strength, colour decode,
the seven-bit UV selector and both extension transforms. The same HLSL builds as rigid/skinned
stride-48/68 and dual-UV stride-60/76 pipelines. Both Diligent Vulkan and OpenGL devices pass the
21-check material-map, 22-check texture-slot, 7-check analytic Fresnel and 12-check sRGB executables
under Xvfb; the Vulkan runs additionally enable Khronos validation.

## PBR alpha coverage (`GLTF-372`, `GLTF-379`)

The effect boundary carries one four-component alpha-test vector. `MASK` maps to
`{cutoff, 0, -1, +1}` and `OPAQUE`/`BLEND` to the never-discard value `{0, 0, +1, +1}`. Every PBR
fragment path chooses component `z` when `sampledAlpha < x` (or, when `y > 0`, when the alpha is
within tolerance `y` of `x`), otherwise component `w`, and discards when the selected value is
negative. A masked PBR draw must remain on the PBR path: routing it to a generic alpha-test effect
loses the PBR material and, on some APIs, selects a vertex layout that does not match the PBR buffer.

The first cross-renderer pass found both kinds of deviation. DirectX 11/12, LLGL, OpenGL 4, SDL GPU,
Vulkan, WebGPU and Wicked did not consume the vector in their PBR fragment program (DirectX 11/12
are two renderers sharing one shader source). DirectX 11/12, SDL GPU, Vulkan and WebGPU additionally
gave their generic alpha-test path priority over PBR. The corrected per-renderer transport is:

| Renderer | PBR alpha-test transport and consumption |
|---|---|
| Bgfx | existing PBR uniform and fragment discard |
| Diligent | existing shared per-draw constants and `FinishPixel` coverage test |
| DirectX 9 | existing pixel constant `c11` and fragment discard |
| DirectX 11 / 12 | `D3DPbrPerDrawConstants::AlphaTest` at byte 176 (`b0`), consumed by both rigid and skinned PBR fragments; PBR wins effect selection |
| EasyGL | existing `uAlphaTest` uniform and fragment discard |
| LLGL | final `vec4` of the 88-float PBR parameter block, consumed by both API shader variants |
| Magnum | existing `uAlphaTest` uniform and shared fragment coverage term |
| Metal | existing `PbrUniforms::alphaTest` and fragment discard |
| OpenGL 2 | existing `uAlphaTest` uniform and fragment discard |
| OpenGL 4 | `uAlphaTest` uniform uploaded by the PBR bind path and consumed by its fragment shader |
| SDL GPU | second `vec4` of `PbrParams`, consumed by the PBR fragment shader; PBR wins effect selection |
| Vulkan | `alphaTest` at bytes 192–207 of the 224-byte PBR UBO (dynamic stride 256), consumed by rigid and skinned fragments; PBR wins effect selection |
| WebGPU | second `vec4` of `PbrFactors`, consumed by rigid and skinned WGSL fragments; PBR wins effect selection |
| Wicked | existing `cb.alphaTest` now consumed by `PbrPS` |

`GltfRendererPbrFallbackPolicy.EveryPbrShaderConsumesTheAlphaCoverageVector` is an ordinary,
source-based inventory gate over all 15 renderer implementations, including separately stored rigid
and skinned variants. Vulkan adds four real-pixel cases (discarding and surviving texels, each rigid
and skinned) to its ten texture-slot cases. Those cases pass on llvmpipe. The changed shader
sources also compile in the native OpenGL 4, SDL GPU, Vulkan, LLGL and WebGPU backend targets and in
the MinGW DirectX 11/12 targets; Wicked's extracted `PbrPS` compiles to Vulkan SPIR-V. These checks
prove this alpha-coverage slice of `GLTF-379`; other §21.1 rows keep that broader audit open.

## PBR map scalars (`GLTF-224`, `GLTF-225`, `GLTF-379`)

glTF gives the normal and occlusion maps one scalar each. `normalTexture.scale` multiplies only the
tangent-space normal's X and Y components before the TBN transform and normalization. Multiplying Z
too would scale the complete vector and normalization would undo the change. `occlusionTexture.strength`
uses the specification equation `1 + strength * (sampled.r - 1)`: strength zero is fully unoccluded,
not black. Both scalars default to one.

The first cross-renderer audit found that only EasyGL consumed the two values even though every
`PbrEffect` and `SkinnedPbrEffect` already transported them through `GpuDrawParams`. The corrected
CPU-to-shader transport is:

| Renderer | Normal-scale / occlusion-strength transport |
|---|---|
| Bgfx | `u_metallicRoughnessFactor.z / .w` |
| Diligent | third PBR constant `float4`, `g_PbrMapScales.x / .y` |
| DirectX 9 | existing pixel constant `c3.z / .w` |
| DirectX 11 / 12 | `D3DPbrPerDrawConstants::PbrMapScales` at byte 192, `x / y` |
| EasyGL | existing `uNormalScale / uOcclusionStrength` uniforms |
| LLGL | `roughnessWeightsPad.z / .w` in the unchanged 88-float PBR block |
| Magnum | `uNormalScale / uOcclusionStrength` uniforms |
| Metal | `PbrUniforms::pbrFactors.z / .w` |
| OpenGL 2 / 4 | `uNormalScale / uOcclusionStrength` uniforms |
| SDL GPU | first `PbrParams` vec4's `z / w`; block size remains two vec4s |
| Vulkan | `pbrMapScales.x / .y` at bytes 208–223; PBR UBO grows from 208 to 224 bytes while its dynamic stride remains 256 |
| WebGPU | `PbrFactors::metallicRoughness.z / .w` |
| Wicked | `cb.pbrFactors.z / .w` |

`GltfRendererPbrFallbackPolicy.EveryPbrShaderConsumesNormalScaleAndOcclusionStrength` locks both
CPU values and both shader equations for all 15 implementations. The Vulkan pixel executable adds
four discriminating cases to its ten slot and four MASK cases: strength .5 with a red-channel byte
64 renders byte 160, while normal scale zero restores the geometric-normal result byte 79 instead
of the tilted sample's byte 35. Each case runs on rigid and skinned PBR, and all 18 cases pass on
llvmpipe. EasyGL's independent material-map test retains its wider three-value scalar sweeps.

## Skinned PBR normal matrices (`GLTF-264`, `GLTF-379`)

A skinned normal cannot use the same palette 3x3 as a position or tangent when a joint has
non-uniform scale. It must use the inverse transpose of the blended palette matrix, followed by the
world inverse transpose. The audit found that EasyGL already did this after `GLTF-264`, but every
other skinned PBR implementation still applied the palette 3x3 directly. The corrected paths are:

| Renderer | Skinned-normal transform |
|---|---|
| Bgfx | cofactor/inverse-transpose helper, then the existing world normal matrix |
| Diligent | HLSL row-vector cofactor helper, then `g_NormalMatrix` |
| DirectX 9 | HLSL row-vector cofactor helper, then the existing world inverse transpose |
| DirectX 11 / 12 | shared HLSL row-vector cofactor helper, then the per-draw normal matrix |
| EasyGL | existing GLSL ES 1.00-compatible cofactor helper for stock and PBR programs |
| LLGL | cofactor helper in both GLSL shader variants, then the world normal matrix |
| Magnum | generated GLSL cofactor helper, then `uNormalMatrix` |
| Metal | MSL cofactor helper plus three newly transported world-normal columns |
| OpenGL 2 | cofactor helper followed by `uNormalMatrix`, replacing the raw world 3x3 |
| OpenGL 4 | cofactor helper followed by the existing world normal matrix |
| SDL GPU | GLSL cofactor helper followed by the world normal matrix |
| Vulkan | GLSL cofactor helper followed by the world normal matrix |
| WebGPU | WGSL cofactor helper followed by the world normal matrix; both 32-byte `PbrFactors` bind-group declarations now advertise their full minimum size |
| Wicked | reconstructs the blended palette columns and applies the cofactor transform before the world normal matrix |

All helpers preserve the determinant sign, and keep the renderer's existing direct-transform
fallback for a near-singular palette. Tangents remain ordinary directions; TBN construction
re-orthogonalises them against the corrected normal. The source inventory test
`EverySkinnedPbrShaderInverseTransposesTheJointMatrix` covers all 15 implementations and every
separately stored shader variant. Vulkan's real-pixel case uses joint scale `[1,2,1]` and authored
normal `(0,.6,.8)`: the corrected inverse transpose renders byte 28, while the former direct
transform would be approximately 66. The expanded Vulkan executable passes 6/6 cases; WebGPU's
rigid and skinned PBR executables each pass 5/5 under the native backend, and the regenerated D3D9
shader passes its 6/6 WineD3D pixel suite.

## PBR tangent handedness (`GLTF-175`, `GLTF-176`, `GLTF-379`)

The vertex stream's `tangent.w` describes the local tangent frame. A direction transform with a
negative determinant reverses that frame, so the shader must multiply the authored sign by the
determinant sign of every transform applied to the tangent: World, an optional instance matrix and,
for skinned PBR, the blended joint matrix. A zero determinant keeps the incoming sign instead of
letting `sign(0)` erase it. The audit found that EasyGL already implemented this after `GLTF-176`;
the other 14 PBR implementations forwarded only the local sign. The corrected matrix is:

| Renderer | Rigid / skinned PBR handedness |
|---|---|
| Bgfx | World determinant / World × blended-skin determinant |
| Diligent | World determinant / World × blended-skin determinant, in HLSL row-vector form |
| DirectX 9 | World determinant / World × blended-skin determinant, in the regenerated SM3 shaders |
| DirectX 11 / 12 | World determinant / World × blended-skin determinant in the shared regenerated SM5 shaders |
| EasyGL | existing World × optional-instance / World × optional-instance × blended-skin determinants |
| LLGL | World / World × blended-skin in both Vulkan GLSL and OpenGL GLSL variants |
| Magnum | World × optional-instance / World × optional-instance × blended-skin determinants |
| Metal | World / World × blended-skin; the skinned tangent now also receives the previously missing World direction transform |
| OpenGL 2 | World / World × blended-skin, using a GLSL 1.10-compatible scalar triple product |
| OpenGL 4 | World / World × blended-skin |
| SDL GPU | World / World × blended-skin |
| Vulkan | World / World × blended-skin |
| WebGPU | World / World × blended-skin in the rigid and skinned WGSL modules |
| Wicked | World / World × reconstructed blended-skin columns |

`EveryPbrShaderComposesDirectionDeterminantsIntoTangentHandedness` locks both rigid and skinned
expressions for all 15 implementations. Vulkan supplies two independent real-pixel witnesses with
local `N=+Z`, `T=+X`, `w=+1` and a tangent-space approximately-`+Y` normal map. Mirroring World in
the rigid case and the only joint in the skinned case must both reconstruct world `B=+Y`; each
renders byte 79 under `L=+Y`, while omitting the relevant determinant makes it black. The focused
Vulkan executable is 8/8. EasyGL retains the fixture-driven mirror witness at byte 151 for both
placements. Bgfx's four target dialects, LLGL/Vulkan/SDL SPIR-V, D3D9 SM3 and shared D3D11/12 SM5
bytecode were regenerated from the edited sources; WebGPU's rigid/skinned runtime suites remain
5/5 each, D3D9 remains 6/6 and Wicked's two PBR vertex entries compile with glslang. Metal's MSL
path remains source-audited on this Linux host.

`PbrEffect` and `SkinnedPbrEffect` shaders sample five textures unconditionally. A missing glTF map
therefore cannot mean “leave the slot unbound”; every PBR renderer must bind a semantic identity:

| Slot | Missing-map texel | Why it is neutral |
|---|---|---|
| base colour | RGBA `(255,255,255,255)` | multiplying by white leaves `baseColorFactor` unchanged |
| normal | RGBA `(128,128,255,255)` | decoding `rgb*2-1` gives the closest RGBA8 representation of tangent-space `(0,0,1)` |
| metallic-roughness | RGBA `(255,255,255,255)` | the scalar factors remain the material values |
| emissive | RGBA `(255,255,255,255)` | multiplying by white leaves `emissiveFactor` unchanged |
| occlusion | RGBA `(255,255,255,255)` | `r=1` means fully unoccluded |

White is not a tolerable normal fallback: after decode and normalize it points along `(1,1,1)`,
54.7° away from the geometric normal. The conventional 8-bit `(128,128,255)` is within about
0.32° of `(0,0,1)`; exact zero X/Y is not representable in unsigned normalized 8-bit storage.

## Fallback audit (`GLTF-374`)

`GltfRendererPbrFallbackPolicy` discovers every renderer source directory that consumes
`GpuDrawParams::pbrNormalMap`. Its inventory must exactly equal the table below, each implementation
must create both canonical texels, and all six optional map fields must be paired with the correct
fallback. Thus adding a PBR backend without a policy entry, or changing normal to white, fails the
ordinary `CnaTests`/glTF conformance run even on a host that cannot build that backend.

| Renderer | Implementation state | Independent runtime evidence |
|---|---|---|
| EasyGL | flat normal + white, rigid and skinned | `EasyGL_Pbr_TextureSlots`, `EasyGL_PbrEffect_Golden`, `EasyGL_SkinnedPbrEffect_Golden` |
| Vulkan | flat normal + white, rigid and skinned | `Vulkan_Pbr_TextureSlots`, `Vulkan_PbrEffect_HandDerived` |
| Bgfx | flat normal + white, rigid and skinned | `Bgfx_PbrEffect`, `Bgfx_SkinnedPbrEffect`, `Bgfx_Pbr_MaterialMaps`, `Bgfx_Pbr_TextureSlots` |
| Diligent | flat normal + white, rigid and skinned | `Diligent_Pbr`, `Diligent_Pbr_MaterialMaps`, `Diligent_Pbr_TextureSlots` on Vulkan and OpenGL |
| DirectX 9 | flat normal + white, rigid and skinned | `DirectX9_Pbr` |
| DirectX 11 | flat normal + white, rigid and skinned | `DirectX11_Pbr_VertexColor` (emissive/no-map route) |
| DirectX 12 | flat normal + white, rigid and skinned | source-policy lock; no dedicated PBR pixel executable yet |
| LLGL | flat normal + white, rigid and skinned | `Llgl_PbrEffect_HandDerived` |
| Magnum | flat normal + white | `Magnum_PbrEffect` |
| OpenGL 2 | flat normal + white, rigid and skinned | `OpenGL2_PbrEffect`, `OpenGL2_PbrEffect_Golden` |
| OpenGL 4 | flat normal + white, rigid and skinned | `OpenGL4_PbrEffect` |
| SDL GPU | flat normal + white, rigid and skinned | `SdlGpu_PbrEffect`, `SdlGpu_SkinnedPbrEffect` |
| WebGPU | flat normal + white, rigid and skinned | `WebGPU_Pbr3D`, `WebGPU_SkinnedPbr3D` |
| Metal | named slot policy: normal→flat, all others→white | `MetalTextureBindingPolicy` unit matrix; real-device pixel gate remains platform-owned |
| Wicked | flat normal + white | `Wicked_Pbr_SrgbTransfer` (12/12 on Intel Vulkan) |

The source-policy lock proves implementation agreement, not hardware availability. Rows without a
runtime executable retain their renderer plan's own verification limitation; they do not weaken or
silently redefine the fallback.

## The stride-60 and stride-80 records and `COLOR_0` (`GLTF-462` / `GLTF-463` / `GLTF-465`)

Stride 60 is the rigid PBR vertex record: Position, Normal, Tangent, `TEXCOORD_0`, `TEXCOORD_1` and
a packed `COLOR_0`. `GLTF-182` created it for dual-UV materials and reserved its last four bytes
purely to keep the stride distinct from 56; `GLTF-462` gave those bytes a job, because §3.7.2.1 makes
`COLOR_0` "an additional linear multiplier to base color" — a term in the metallic-roughness product
rather than a reason to abandon the model, which is what CNA did before (a vertex-coloured primitive
fell to the stride-24 layout, which has **no Normal slot at all**, so it could not be lit).

Stride 80 is its skinned counterpart, added by `GLTF-463`: the whole stride-76 skinned PBR record as a
byte-for-byte prefix with the packed `COLOR_0` appended at offset 76. Before it existed, a skinned
vertex-coloured metallic-roughness primitive lost its material model entirely and fell back to
`SkinnedEffect`.

Auditing that change found a **pre-existing defect this table now prevents recurring**: most PBR
renderers had never learned stride 60 even though it has been live since `GLTF-182`. `OPENGL2` fell
through a `stride >= 32` catch-all that reads `TEXCOORD` at offset 24 — inside the tangent — so a
dual-UV PBR mesh textured itself from tangent bytes, silently. `OPENGL4`, `MAGNUM`, `LLGL` and
`DIRECTX9` degraded visibly instead (position-only, no attributes, or an outright refusal). `GLTF-465`
then found a second layer of the same defect in `OPENGL2` and `OPENGL4`: binding the record correctly
was **not** enough, because their PBR *program* selection was still keyed on the old stride sets, so a
stride-60 draw was shaded by the Blinn-Phong program (OpenGL 2) and a stride-76/80 draw would have run
the rigid PBR program over a skinned record (OpenGL 4).

Three dispositions, all machine-checked — read the tests, not this table, which is a snapshot:

- `GltfRendererPbrFallbackPolicy.EveryPbrRendererEitherBindsTheStride60RecordOrIsNamedAsNotYet`
- `GltfRendererPbrFallbackPolicy.EverySkinnedPbrRendererEitherBindsTheStride80RecordOrRefusesIt`
- `GltfRendererPbrFallbackPolicy.VertexColourReachesTheBaseColourProductOnlyWhereItIsImplemented`
- `GltfRendererPbrFallbackPolicy.EveryStrideGatedPbrRouteAdmitsBothColourCarryingStrides` (`GLTF-472`)
- `RendererStrideConformance.AColourCarryingPbrPrimitiveEitherDrawsOrRefusesByName` (`GLTF-472`, live draw)

| Renderer | Stride 60 | Stride 80 | `COLOR_0` in the base-colour product (RGB **and** alpha) |
|---|---|---|---|
| EasyGL (OPENGLES2/3, OPENGL33, WEBGL1/2) | yes | yes | **applies** — `uVertexColorEnabled` gates `albedo *= cnaVertexColor.rgb` and `alpha *= cnaVertexColor.a` in both the rigid and the skinned program |
| Software | yes | yes | **applies** — the interpolated vertex colour *is* the start of the CPU product, alpha included |
| IGL | yes, **without a stride row**: it builds its vertex input from the public `VertexDeclaration` | yes, same way | **applies, without per-renderer work** — its shader library is generated per feature set, so it declares `aColor` exactly when the declaration carries a Color and feeds the product to `cnaShadePbr` |
| OpenGL 2 | yes (`GLTF-462` **fixed a silent mis-binding**; `GLTF-465` fixed the program selection) | yes | **applies** |
| OpenGL 4 | yes (`GLTF-462` added the row; `GLTF-465` fixed the skinned program selection) | yes | **applies** |
| Vulkan | yes | yes (its own SPIR-V variant, since stride 76 has no colour slot to bind) | **applies** |
| DirectX 11 | yes | yes | **applies** — one shared HLSL pair and one shared `VertexColorFlags` constant serve both D3D families |
| DirectX 12 | yes | yes | **applies** — same shared HLSL and constant as DirectX 11 |
| Magnum | yes (`GLTF-465` added the colour attribute **and** the program selection: its `SelectStockProgram` had accepted only strides 48/68, so a stride-60 draw was refused outright) | yes (`GLTF-465` added the layout) | **applies** — generated GLSL, so the product is in the source that is compiled at runtime; `BindDrawParams` raises the flag only for strides 60/80, because one program serves 48/60 and an unsupplied attribute would read GL's generic default `(0,0,0,1)` |
| Diligent | yes | yes (`GLTF-465` added the stride-80 variant; **`GLTF-472` made it reachable** — the `switch` selected `SkinnedPbrColor3D` for stride 80 and a validation nine lines later still read `stride != 68 && stride != 76` and threw) | **applies** — its HLSL is a per-variant template expanded at pipeline creation, so the colour attribute, its interpolant and the product exist **only** in the variants whose input layout supplies them; `g_Flags.y` is the effect's switch |
| Bgfx | yes (its stride-60 `skip(4)` **was** the colour slot) | yes (`GLTF-465` added the layout) | **applies** — the `.sc` sources are compiled offline into all four backend bytecodes; only the three PBR shaders' 12 blobs changed, every other blob byte-identical, which is also the proof the rebuilt `shaderc` matches the one the committed header came from |
| DirectX 9 | binds it for non-PBR routes; its PBR path accepts only strides 48/68 | refuses | **refuses** (`GLTF-465`): `vs_3_0`/`ps_3_0` bytecode is regenerated only through the pinned native `d3dcompiler_47.dll` Wine prefix, which this environment does not have |
| LLGL | yes | yes (`GLTF-465` added strides 76 and 80) | **applies, with a precondition**: LLGL treats PbrEffect's base-colour map as mandatory and throws "needs Texture bound" without one, so it cannot draw glTF's own default material (`baseColorFactor` alone, §3.9.2) — which is what both `COLOR_0` corpus fixtures author, so the product is unreachable for them (`GLTF-474`). Where a base colour map *is* bound, the product applies: its PBR pipeline used to *strip* the colour attribute ("this shader never reads one"); it now passes it through, and `specularState.z` carries the effect's switch. Regenerating touched only the PBR blobs: every other one is byte-identical, so the committed header was a **mix** of `glslangValidator`- and libshaderc-produced SPIR-V, and only the shaders being changed moved to the documented tool |
| Metal | no layout | refuses | **refuses** (`GLTF-465`): no stride-60/80 pipeline, and Metal cannot be built or run on this host |
| SDL GPU | yes (`GLTF-465` added the layout **and** the pipeline axis; **`GLTF-472` made it reachable**) | yes (same) | **applies** — its PC block already carried the flag, commented "unused -- PbrEffect has no vertex-color path"; the shaders are compiled offline with libshaderc, so the colour-carrying variants are two more entries in the same list. `GLTF-465` left `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` selecting the PBR queue for `stride == 48`/`68` only, so none of that was reachable: a stride-60/80 draw matched no branch and was refused by the stride-16 coloured path. `GLTF-472` fixed the dispatch and gave sampler slot 0 the neutral-white fallback slots 1..6 already had, without which a factor-only material (both `COLOR_0` fixtures) is still refused |
| WebGPU | no layout | refuses | **refuses** (`GLTF-465`): needs new WGSL pipeline variants |
| Wicked | no layout | refuses | **refuses** (`GLTF-465`): needs WickedEngine shader work |

**There is no third column value, and that is the point.** A renderer either evaluates §3.9.2's
product or **refuses the draw**; what must not exist is a renderer that accepts a valid asset and
draws it with the opaque-white identity substituted for the authored colour — a visibly wrong surface
reported as a successful draw. The project owner rejected the unqualified `GLTF CORE 2.0 CORRECT`
milestone on 2026-08-18 on exactly that ground, and the partition is the answer to it.

**And "refuses" has a precondition, added the same day after `GLTF-473`:**

> An explicit refusal counts as one **only if it happens before any incompatible vertex-layout
> interpretation and before any GPU submission.**

Without that clause the second state is not actually safe, because "throws eventually" is not the
same as "did not read the data". A route that binds a client array, records a command or overwrites a
matrix and *then* throws has already half-executed the draw: it may have read the record through a
layout that does not describe it, and it leaves the device in a state the next frame inherits. The
rule is what makes the refusal a boundary rather than an epilogue, and it is why every guard in this
document is called at the top of its route rather than beside the code it protects — `GLTF-473`'s own
first attempt placed two of six calls after a `glMatrixMode`/`glLoadMatrixf` pair and had to be moved.
Its observable form is recovery: `NoPbrOrSkinnedRecordIsEverReadThroughAnIncompatibleLayout` requires
the very next valid draw to still render, which a refusal that kept state cannot satisfy.

The refusal is **one shared implementation**, not eight:
`CNA::Internal::Renderers::RequireVertexColourPbrSupportEXT`
(`modules/graphics/include/CNA/Internal/Renderers/Common/VertexColourPbrSupport.hpp`). It fires
exactly when `params.pbr && params.vertexColorEnabled && (stride == 60 || stride == 80)`, and its
message names the renderer, §3.9.2, `GLTF-465`, the renderers that do implement the product, and the
application's own opt-out — setting `VertexColorEnabledEXT=false` makes the identity a deliberate
choice, and the draw is then permitted. Three renderers (`bgfx`, `diligent`, `llgl`) genuinely
changed behaviour: each had a stride-60 layout row and would have drawn the asset with the identity.
The other five already failed such a draw further downstream, but as a stride/layout mismatch that
never mentioned the missing semantic.

`GltfRendererPbrFallbackPolicy.EveryPbrRendererEitherAppliesVertexColourOrRefusesTheDrawExplicitly`
is the machine-checked partition over all seventeen, and
`VertexColourPbrSupport.*` pins the predicate itself — including the four kinds of draw that must
stay accepted, so the guard cannot start refusing content that renders correctly today.

**What a refusing renderer costs, and what it does not.** An *uncoloured* primitive is unaffected
everywhere: the importer fills stride 60's slot with **opaque white**, the multiplier's identity, and
leaves `VertexColorEnabledEXT` false, so the guard is inert and such a draw renders exactly as it did
before `GLTF-462`. What a refusing renderer cannot draw is a *vertex-coloured* metallic-roughness
primitive — and the application can still ask for it by setting `VertexColorEnabledEXT = false`, which
makes the identity an explicit decision instead of a silent substitution. Everything else about the
material survives everywhere: the authored `NORMAL`, the tangent basis, every PBR factor and every PBR
map, which is strictly more than the stride-24 fallback carried before `GLTF-462`.

**How far each "applies" is verified, precisely.** Three tiers, not one, and `GLTF-472` added the
middle tier because the distance between the top and the bottom is where two defects lived.

*Pixel-level* — four of the thirteen, the renderers the L7 corpus oracle has policies for:
**EasyGL/OPENGLES3**, **Vulkan** (lavapipe), **SOFTWARE** and **DirectX11** (Wine + DXVK) each
rendered all 146 corpus assets twice on this revision, and their `skin-vertex-color-pbr` captures
carry the authored per-vertex alpha product while their `mat-vertex-color-pbr` captures carry the
rigid path's own green-channel witness.

*Live draw* — seven renderers, added by `GLTF-472`: **SOFTWARE**, **OpenGL 2**, **OpenGL 4**,
**SDL GPU**, **Magnum**, **LLGL** and **Diligent** each drew both colour-carrying corpus fixtures
through a real device in the multi-renderer tree, selected with `CNA_GRAPHICS_RENDERER` and
`SDL_VIDEODRIVER=x11` on an Xvfb display
(`RendererStrideConformance.AColourCarryingPbrPrimitiveEitherDrawsOrRefusesByName`). This tier is the
only one that says the shader is *reachable*, and it is the only one that could have caught SDL GPU's
and Diligent's route defects — both of which had a complete layout row, a complete shader and a
passing source audit.

*Source-verified only* — **DirectX 12**, which shares its HLSL, its constant buffer and its
input-element table with DirectX 11 and is therefore covered transitively by a pixel-proven pair, and
**Bgfx**, whose offline blobs this host can rebuild but whose draw it cannot run. **IGL** is verified
by construction and by its own generated shader library, and Magnum's four generated PBR sources
additionally compile under a real GLSL compiler (`glslangValidator`) rather than only being grepped.

**The lesson, stated so the next renderer does not repeat it.** Every audit in this document reads
*declarations* — a layout row, a shader expression, a guard call. None of them can see whether the
draw route that selects that layout still enumerates only the uncoloured strides. Three renderers
have now shipped exactly that shape (OpenGL 2 under `GLTF-465`, SDL GPU and Diligent under
`GLTF-472`), so a renderer whose PBR route is chosen from a stride list has its acceptance predicate
pinned in
`GltfRendererPbrFallbackPolicy.EveryStrideGatedPbrRouteAdmitsBothColourCarryingStrides`, and the live
tier settles it from a real draw. Adding a layout row without adding the stride to the route is not a
partial implementation — it is a renderer in neither allowed state.

**The rendered product itself is verified numerically, not by reading shader source.**
`GltfFixtureCorpus.EveryL7GoldenCarriesTheVertexColourAlphaProductRatherThanTheWhiteIdentity` reads
the committed L7 goldens of all four capture policies (EasyGL, Vulkan, SOFTWARE, DirectX11/DXVK). Base
colour **alpha** is the one part of the product with no view dependence, so a BLEND-mode PBR primitive
without a `COLOR_0` (`mat-factor-only-gold`) must capture exactly one alpha value everywhere, while
`skin-vertex-color-pbr` must capture a spread whose end points match `baseColorFactor.a` times the
authored per-vertex alphas. The rig's own alpha composite is calibrated from the control asset in the
same golden set rather than assumed, so the test survives a legitimate rig change and still fails if
the colour is dropped. The **rigid** stride-60 path gets its own witness in the same test, because alpha
cannot carry it: `mat-vertex-color-pbr` is opaque, but its three `COLOR_0` values are pure red, green and
blue and its emissive factor has no green in it, so its capture must contain a green channel of exactly
**0** at the red corner. Under the opaque-white identity that channel would be `baseColorFactor.g` = 0.4,
lit and never zero.
