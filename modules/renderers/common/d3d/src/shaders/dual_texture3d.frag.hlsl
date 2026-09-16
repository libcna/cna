// Shader Model 5.0 (ps_5_0). Ported line-by-line from
// src/CNA/Internal/Renderers/Vulkan/shaders/dual_texture3d.frag.glsl.

Texture2D    uTexture         : register(t0);
SamplerState uTextureSampler  : register(s0);
Texture2D    uTexture2        : register(t1);
SamplerState uTexture2Sampler : register(s1);

cbuffer PerDraw : register(b0)
{
    row_major float4x4 Mvp;
    float4 DiffuseColor;
    float3 AmbientColor;
    float  LightingEnabled;
    float3 Light0Dir;
    float  TextureEnabled;
    float3 Light0Diffuse;
    float  VertexColorEnabled;
};

// Task 899: fog constant buffer at register(b2) (t0/s0 and t1/s1 are the two texture samplers).
cbuffer FogParams : register(b2)
{
    float4 FogColor;  // xyz = FogColor, w = reserved padding
    float4 FogVector;      // CPU-prepared FNA view-space fog vector
    // WINCLOSE-0026: Direct3D 9's expansion of the texture's missing channels (see
    // D3DCommon::D3D9ChannelExpansion); identity for a four-channel format.
    float4 Texture0ChannelMask;
    float4 Texture0ChannelFill;
    float4 Texture1ChannelMask;
    float4 Texture1ChannelFill;
};

struct PSInput
{
    float4 Position  : SV_Position;
    float2 UV0       : TEXCOORD0;
    float2 UV1       : TEXCOORD1;
    float4 Tint      : TEXCOORD2;
    float  FogFactor : TEXCOORD3;
};

float4 main(PSInput input) : SV_Target
{
    float4 tex1 = uTexture.Sample(uTextureSampler, input.UV0) * Texture0ChannelMask + Texture0ChannelFill;
    float4 tex2 = uTexture2.Sample(uTexture2Sampler, input.UV1) * Texture1ChannelMask + Texture1ChannelFill;
    tex1.rgb *= 2.0;
    float4 outColor = tex1 * tex2 * input.Tint;
    // Task 899: mix toward FogColor as FogFactor -> 0 (matches the established Task 888 formula).
    outColor.rgb = lerp(FogColor.xyz, outColor.rgb, input.FogFactor);
    return outColor;
}
