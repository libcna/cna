// Shader Model 5.0 (ps_5_0). Ported line-by-line from
// src/CNA/Internal/Backends/Vulkan/shaders/colored_textured3d.frag.glsl.

Texture2D    uTexture        : register(t0);
SamplerState uTextureSampler : register(s0);

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

// Task 899: fog forwarded via the shared colored3d/textured3d/colored_textured3d bundle's second
// constant buffer (GLSL set=0 binding=1) -- PerDraw above has zero spare bytes.
cbuffer FogParams : register(b1)
{
    float4 FogColor;  // xyz = FogColor, w = reserved padding
    float4 FogVector;      // CPU-prepared FNA view-space fog vector
};

struct PSInput
{
    float4 Position  : SV_Position;
    float2 UV        : TEXCOORD0;
    float4 Tint      : TEXCOORD1;
    float  FogFactor : TEXCOORD2;
};

float4 main(PSInput input) : SV_Target
{
    float4 tex = (TextureEnabled > 0.5) ? uTexture.Sample(uTextureSampler, input.UV) : float4(1.0, 1.0, 1.0, 1.0);
    float4 outColor = tex * input.Tint;
    // Task 899: mix toward FogColor as FogFactor -> 0 (matches the established Task 888 formula).
    outColor.rgb = lerp(FogColor.xyz, outColor.rgb, input.FogFactor);
    return outColor;
}
