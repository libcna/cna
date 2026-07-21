// Shader Model 5.0 (vs_5_0). Ported line-by-line from
// src/CNA/Internal/Backends/Vulkan/shaders/dual_texture3d.vert.glsl.
// Stride 20: VertexPositionTexture -- float3 pos + float2 uv.
//
// Task 899: dedicated vertex shader (mirrors the GLSL source's own history -- previously
// DualTextureEffect reused textured3d's compiled shader directly; textured3d now declares its own
// fog constant buffer at register(b1), which conflicts with dual_texture3d's own 2-sampler layout
// (extended here with its own fog constant buffer at register(b2), since t0/s0 and t1/s1 are
// already the two texture samplers) -- so this pipeline needs its own vertex shader file, split
// off with identical MVP/DiffuseColor logic.

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

cbuffer FogParams : register(b2)
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
    // REMED-GFX-005/010: FNA view-space fog. FogStartEnd now carries EffectHelpers.SetFogVector
    // (World*View 3rd column baked CPU-side); keep = 1 - saturate(dot(pos, fogVector)) is the
    // corrected (non-mirrored) FNA factor in eye-space Z, not object-space. Zero vector = no fog.
    output.FogFactor = (FogColorEnabled.w > 0.5)
        ? 1.0 - saturate(dot(float4(input.Position, 1.0), FogStartEnd))
        : 1.0;

    return output;
}
