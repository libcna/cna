// Shader Model 5.0 (vs_5_0). Ported line-by-line from
// src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl.

cbuffer PerDraw : register(b0)
{
    row_major float4x4 Mvp;
    row_major float4x4 World;
};

cbuffer EnvMapParams : register(b2)
{
    float4 EyePosPad;
    float4 DiffuseColor;
    float4 EmissiveEm;
    float4 Light0DirPad;
    float4 Light0DiffPad;
    float4 EnvMapSpecPad;
    // Task 899's noted cheap leftover: fog packed into this constant buffer's spare tail bytes.
    float4 FogColor;  // xyz = FogColor, w = reserved padding
    float4 FogVector;      // CPU-prepared FNA view-space fog vector
    // Task 890: DirectionalLight1/DirectionalLight2 diffuse forwarding.
    float4 Light1DirPad;
    float4 Light1DiffPad;
    float4 Light2DirPad;
    float4 Light2DiffPad;
};

struct VSInput
{
    float3 Position : POSITION0;
    float3 Normal   : NORMAL0;
    float2 UV       : TEXCOORD0;
};

struct VSOutput
{
    float4 Position     : SV_Position;
    float3 WorldNormal  : TEXCOORD0;
    float3 EyeDir       : TEXCOORD1;
    float2 UV           : TEXCOORD2;
    float  FogFactor    : TEXCOORD3;
};

// Returns transpose(inverse(m)) directly (the cofactor matrix over the determinant) -- see
// lit_textured3d.vert.hlsl's identical helper for the same documented HLSL-vs-GLSL deviation.
float3x3 InverseTranspose3x3(float3x3 m)
{
    float3 c0 = cross(m[1], m[2]);
    float3 c1 = cross(m[2], m[0]);
    float3 c2 = cross(m[0], m[1]);
    float det = dot(m[0], c0);
    return float3x3(c0, c1, c2) / det;
}

VSOutput main(VSInput input)
{
    VSOutput output;

    output.Position = mul(float4(input.Position, 1.0), Mvp);
    float3 worldPos = mul(float4(input.Position, 1.0), World).xyz;
    float3x3 nm = InverseTranspose3x3((float3x3)World);
    output.WorldNormal = normalize(mul(input.Normal, nm));
    output.EyeDir = EyePosPad.xyz - worldPos;
    output.UV = input.UV;
    // REMED-GFX-005/010/061: FNA view-space fog. FogVector carries EffectHelpers.SetFogVector
    // (World*View 3rd column baked CPU-side); keep = 1 - saturate(dot(pos, fogVector)) is the
    // corrected (non-mirrored) FNA factor in eye-space Z, not object-space. Zero vector = no fog.
    output.FogFactor = 1.0 - saturate(dot(float4(input.Position, 1.0), FogVector));

    return output;
}
