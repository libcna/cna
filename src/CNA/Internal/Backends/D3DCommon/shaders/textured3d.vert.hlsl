// Shader Model 5.0 (vs_5_0). Ported line-by-line from
// src/CNA/Internal/Backends/Vulkan/shaders/textured3d.vert.glsl.
// Stride 20: VertexPositionTexture -- float3 pos + float2 uv.
// See colored3d.vert.hlsl for the matrix/mul() and Y-flip/depth-range convention notes.

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
    float4 FogColorEnabled;  // xyz = FogColor, w = fogEnabled
    float4 FogStartEnd;      // x = fogStart, y = fogEnd, zw = unused
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

    // Task 899: fog factor from raw object-space Z (matches the established Task 888 formula).
    output.FogFactor = (FogColorEnabled.w > 0.5)
        ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6))
        : 1.0;

    return output;
}
