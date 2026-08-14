# glTF PBR texture bindings and fallbacks across renderers

Every `PbrEffect`/`SkinnedPbrEffect` material has the same five logical resources, in this order:
base colour, normal, metallic-roughness, emissive and occlusion. “The same order” does not mean
every native API has the same binding numbers. `GltfRendererPbrFallbackPolicy` locks the complete
CPU-field → native binding → shader declaration chain for each implementation, including both
rigid and skinned fragment variants where a renderer stores them separately.

## Texture-slot ABI (`GLTF-373`)

| Renderer | Native binding of base / normal / MR / emissive / occlusion |
|---|---|
| Bgfx | stages `0 / 1 / 2 / 3 / 4` (`s_texColor`, `s_texNormal`, `s_texMetallicRoughness`, `s_texEmissive`, `s_texOcclusion`) |
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
texture-slot portion runs ten cases each (five maps × rigid/skinned) with semantic sentinel texels: red base colour, green
metallic-roughness (`G=1`, `B=0`), blue emissive, channel-asymmetric occlusion and a tilted normal.
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
| Diligent | fourth PBR `float4`, `g_PbrDielectricFresnel` | Vulkan and OpenGL rigid/skinned pixel tests |
| DirectX 9 | pixel constant `c13` | Microsoft `ps_3_0` compile/disassembly plus WineD3D diagnostic |
| DirectX 11 / 12 | `D3DPbrPerDrawConstants::DielectricFresnel` at byte 208 | Microsoft `ps_5_0` compile and both MinGW frontends; D3D11 WineD3D diagnostic |
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
`specularColorTexture` are not part of this factor-only slice and remain the named `GLTF-344`
limit.

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
must create both canonical texels, and all four optional map fields must be paired with the correct
fallback. Thus adding a PBR backend without a policy entry, or changing normal to white, fails the
ordinary `CnaTests`/glTF conformance run even on a host that cannot build that backend.

| Renderer | Implementation state | Independent runtime evidence |
|---|---|---|
| EasyGL | flat normal + white, rigid and skinned | `EasyGL_Pbr_TextureSlots`, `EasyGL_PbrEffect_Golden`, `EasyGL_SkinnedPbrEffect_Golden` |
| Vulkan | flat normal + white, rigid and skinned | `Vulkan_Pbr_TextureSlots`, `Vulkan_PbrEffect_HandDerived` |
| Bgfx | flat normal + white, rigid and skinned | `Bgfx_PbrEffect`, `Bgfx_SkinnedPbrEffect` |
| Diligent | flat normal + white, rigid and skinned | `Diligent_Pbr` |
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
