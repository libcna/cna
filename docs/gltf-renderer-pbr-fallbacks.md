# glTF PBR map fallbacks across renderers

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

## Renderer audit (`GLTF-374`)

`GltfRendererPbrFallbackPolicy` discovers every renderer source directory that consumes
`GpuDrawParams::pbrNormalMap`. Its inventory must exactly equal the table below, each implementation
must create both canonical texels, and all four optional map fields must be paired with the correct
fallback. Thus adding a PBR backend without a policy entry, or changing normal to white, fails the
ordinary `CnaTests`/glTF conformance run even on a host that cannot build that backend.

| Renderer | Implementation state | Independent runtime evidence |
|---|---|---|
| EasyGL | flat normal + white, rigid and skinned | `EasyGL_PbrEffect_Golden`, `EasyGL_SkinnedPbrEffect_Golden` |
| Vulkan | flat normal + white, rigid and skinned | `Vulkan_PbrEffect_HandDerived` |
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
