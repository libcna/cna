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

`EasyGL_Pbr_TextureSlots` and `Vulkan_Pbr_TextureSlots` add independent real-pixel evidence. Each
runs ten cases (five maps × rigid/skinned) with semantic sentinel texels: red base colour, green
metallic-roughness (`G=1`, `B=0`), blue emissive, channel-asymmetric occlusion and a tilted normal.
The linearly rendered expected bytes are analytically stated in the test; a swap cannot pass by
showing merely that “some texture” was sampled. `EasyGL_Pbr_MaterialMaps` additionally covers the
EasyGL-only output-transfer and scalar semantics at the same binding boundary.

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
