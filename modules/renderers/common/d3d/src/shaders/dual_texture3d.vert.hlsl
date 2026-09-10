// Shader Model 5.0 (vs_5_0). Ported line-by-line from
// src/CNA/Internal/Renderers/Vulkan/shaders/dual_texture3d.vert.glsl.
// Exact input signature is selected at compile time for optional COLOR0 and TEXCOORD1 semantics.
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
    float4 FogColor;  // xyz = FogColor, w = reserved padding
    float4 FogVector;      // CPU-prepared FNA view-space fog vector
};

struct VSInput
{
    float3 Position : POSITION0;
#ifdef CNA_DUAL_TEXTURE_COLOR_INPUT
    float4 Color    : COLOR0;
#endif
    float2 UV0      : TEXCOORD0;
#ifdef CNA_DUAL_TEXTURE_DUAL_UV_INPUT
    float2 UV1      : TEXCOORD1;
#endif
};

struct VSOutput
{
    float4 Position  : SV_Position;
    float2 UV0       : TEXCOORD0;
    float2 UV1       : TEXCOORD1;
    float4 Tint      : TEXCOORD2;
    float  FogFactor : TEXCOORD3;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 pos = mul(float4(input.Position, 1.0), Mvp);
    output.Position = pos;
    output.UV0 = input.UV0;
#ifdef CNA_DUAL_TEXTURE_DUAL_UV_INPUT
    output.UV1 = input.UV1;
#else
    output.UV1 = input.UV0;
#endif
#ifdef CNA_DUAL_TEXTURE_COLOR_INPUT
    output.Tint = DiffuseColor * lerp(1.0.xxxx, input.Color, VertexColorEnabled);
#else
    output.Tint = DiffuseColor;
#endif

    // REMED-GFX-005/010/061: FNA view-space fog. FogVector carries EffectHelpers.SetFogVector
    // (World*View 3rd column baked CPU-side); keep = 1 - saturate(dot(pos, fogVector)) is the
    // corrected (non-mirrored) FNA factor in eye-space Z, not object-space. Zero vector = no fog.
    output.FogFactor = 1.0 - saturate(dot(float4(input.Position, 1.0), FogVector));

    return output;
}
