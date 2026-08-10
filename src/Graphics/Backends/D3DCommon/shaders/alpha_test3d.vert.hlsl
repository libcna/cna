// Shader Model 5.0 (vs_5_0). Ported line-by-line from
// src/CNA/Internal/Backends/Vulkan/shaders/alpha_test3d.vert.glsl.
//
// AlphaTestEffect vertex shader. Reads position (POSITION0) and UV (TEXCOORD0). UV byte offset
// varies per stride (20/24/32); D3DVertexFormatHelper.hpp's per-stride D3D11_INPUT_ELEMENT_DESC
// arrays handle the mapping, this shader itself is stride-agnostic.

cbuffer PerDraw : register(b0)
{
    row_major float4x4 Mvp;    // offset  0  (64 bytes)
    float4 DiffuseColor;       // offset 64  (16 bytes)
    float4 AlphaTestParams;    // offset 80  (16 bytes) -- unused here, read only in the PS
    // REMED-GFX-005/010: FNA view-space fog vector (was the VertexColorEnabled/FogEnabled/FogStart/
    // FogEnd scalar quartet). Dotted with float4(pos,1); zero vector = no fog.
    float4 FogVector;          // offset 96
    float3 FogColor;           // offset 112
    float  VertexColorEnabled; // offset 124 -- unused here (this variant has no color attribute)
};

struct VSInput
{
    float3 Position : POSITION0;
    float2 UV       : TEXCOORD0;
};

struct VSOutput
{
    float4 Position  : SV_Position;
    float2 UV        : TEXCOORD0;
    float4 Tint      : TEXCOORD1;
    float  FogFactor : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 pos = mul(float4(input.Position, 1.0), Mvp);
    output.Position = pos;
    output.UV = input.UV;
    output.Tint = DiffuseColor;

    // REMED-GFX-005/010: FNA view-space fog. keep = 1 - saturate(dot(objectPos, fogVector)); the
    // vector bakes World*View's 3rd column (eye-space Z), the corrected non-mirrored FNA factor.
    // 1.0 = no fog, 0.0 = full fog; a zero fog vector (disabled) yields keep = 1.
    output.FogFactor = 1.0 - saturate(dot(float4(input.Position, 1.0), FogVector));

    return output;
}
