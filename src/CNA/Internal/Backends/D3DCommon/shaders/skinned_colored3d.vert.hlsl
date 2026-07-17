// Shader Model 5.0 (vs_5_0). skinned3d.vert.hlsl's own stride-56 sibling: adds a per-vertex Color
// (COLOR0) input, matching AlphaTest3d/AlphaTestColored3d's established precedent of a dedicated
// vertex-color-carrying shader variant selected by stride rather than a runtime branch on the
// vertex layout. Ported from EasyGLGraphicsBackend::EnsureSkinnedProgram()'s vertex stage (CNB-67,
// Phase 13C) -- identical skinning math, plus Color pass-through.
// Stride 56: the stride-52 SkinnedVertex layout (VertexPositionNormalTextureSkinned) with a
// per-vertex Color (normalized ubyte4) appended at offset 52.

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

cbuffer BoneBlock : register(b1)
{
    row_major float4x4 Bones[72];
};

cbuffer FogParams : register(b2)
{
    float4 FogColorEnabled;
    float4 FogStartEnd;
    float4 Light1DirPad;
    float4 Light1DiffPad;
    float4 Light2DirPad;
    float4 Light2DiffPad;
    row_major float4x4 World;
    float4 EyePosPad;         // w = WeightsPerVertex
    float4 SpecularColorPower;
    float4 Light0SpecPad;
    float4 Light1SpecPad;
    float4 Light2SpecPad;
};

struct VSInput
{
    float3 Position     : POSITION0;
    float3 Normal       : NORMAL0;
    float2 UV           : TEXCOORD0;
    float4 BoneWeights  : BLENDWEIGHT0;
    uint4  BoneIndices  : BLENDINDICES0;
    float4 Color        : COLOR0;
};

struct VSOutput
{
    float4 Position  : SV_Position;
    float3 Normal    : TEXCOORD0;
    float2 UV        : TEXCOORD1;
    float  FogFactor : TEXCOORD2;
    float3 WorldPos  : TEXCOORD3;
    float4 Color     : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float weightsPerVertex = EyePosPad.w;
    float4x4 skinMat = Bones[input.BoneIndices.x] * input.BoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += Bones[input.BoneIndices.y] * input.BoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += Bones[input.BoneIndices.z] * input.BoneWeights.z
                                           + Bones[input.BoneIndices.w] * input.BoneWeights.w;
    float4 skinnedPos = mul(float4(input.Position, 1.0), skinMat);
    output.Position = mul(skinnedPos, Mvp);
    output.Normal = normalize(mul(input.Normal, (float3x3)skinMat));
    output.UV = input.UV;
    output.WorldPos = mul(skinnedPos, World).xyz;
    output.Color = input.Color;
    output.FogFactor = (FogColorEnabled.w > 0.5)
        ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6))
        : 1.0;

    return output;
}
