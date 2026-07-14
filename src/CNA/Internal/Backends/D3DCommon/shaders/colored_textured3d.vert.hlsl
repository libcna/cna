// Shader Model 5.0 (vs_5_0). Ported line-by-line from
// src/CNA/Internal/Backends/Vulkan/shaders/colored_textured3d.vert.glsl.
// Stride 24: VertexPositionColorTexture -- float3 pos + ubyte4 color (UNORM) + float2 uv.

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
    float4 Color    : COLOR0;    // normalized UNORM R8G8B8A8
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

    // Mix vertex color and diffuse based on VertexColorEnabled flag.
    output.Tint = (VertexColorEnabled > 0.5) ? input.Color * DiffuseColor : DiffuseColor;

    // Task 899: fog factor from raw object-space Z (matches the established Task 888 formula).
    output.FogFactor = (FogColorEnabled.w > 0.5)
        ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6))
        : 1.0;

    return output;
}
