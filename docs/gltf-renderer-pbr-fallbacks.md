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
showing merely that “some texture” was sampled. `EasyGL_Pbr_MaterialMaps` additionally covers the
EasyGL-only output-transfer and scalar semantics at the same binding boundary.

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
| Vulkan | final `vec4` of the 208-byte PBR UBO (dynamic stride 256), consumed by rigid and skinned fragments; PBR wins effect selection |
| WebGPU | second `vec4` of `PbrFactors`, consumed by rigid and skinned WGSL fragments; PBR wins effect selection |
| Wicked | existing `cb.alphaTest` now consumed by `PbrPS` |

`GltfRendererPbrFallbackPolicy.EveryPbrShaderConsumesTheAlphaCoverageVector` is an ordinary,
source-based inventory gate over all 15 renderer implementations, including separately stored rigid
and skinned variants. Vulkan adds four real-pixel cases (discarding and surviving texels, each rigid
and skinned) to its ten texture-slot cases. Those 14 cases pass on llvmpipe. The changed shader
sources also compile in the native OpenGL 4, SDL GPU, Vulkan, LLGL and WebGPU backend targets and in
the MinGW DirectX 11/12 targets; Wicked's extracted `PbrPS` compiles to Vulkan SPIR-V. These checks
prove this alpha-coverage slice of `GLTF-379`; the remaining §21.1 semantics still require the full
matrix.

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
| Wicked | flat normal + white | source-policy lock; renderer has not yet passed its own real-GPU gate |

The source-policy lock proves implementation agreement, not hardware availability. Rows without a
runtime executable retain their renderer plan's own verification limitation; they do not weaken or
silently redefine the fallback.
