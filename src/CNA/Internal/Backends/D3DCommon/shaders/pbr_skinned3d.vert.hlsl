// Shader Model 5.0 (vs_5_0). PBR + skinning combo (SkinnedPbrEffect) -- HLSL port of
// EasyGLGraphicsBackend::EnsurePbrSkinnedProgram()'s vertex stage (plan_cnj.md CNB-58 follow-up):
// pbr3d.vert.hlsl's own transform, with bone skinning (up to 72 bones, 1/2/4 weights per vertex)
// applied to Position/Normal/Tangent before the World transform, mirroring skinned3d.vert.hlsl's
// own skinning math exactly.
// Stride 68: VertexPositionNormalTangentTextureSkinned (the stride-48 PbrEffect layout with the
// stride-52 skinning suffix -- BlendWeight, BlendIndices -- appended).

cbuffer PerDraw : register(b0)
{
    row_major float4x4 Mvp;
    row_major float4x4 World;
    float4 DiffuseColor;
    float4 AmbientMetallic;    // xyz = AmbientColor, w = MetallicFactor
    float4 EmissiveRoughness;  // xyz = EmissiveColor, w = RoughnessFactor
};

cbuffer BoneBlock : register(b1)
{
    row_major float4x4 Bones[72];
};

cbuffer PbrLights : register(b2)
{
    float4 EyePosWeights;   // xyz = EyePosition, w = WeightsPerVertex (Task 895 convention)
    float4 Light0DirPad;
    float4 Light0DiffusePad;
    float4 Light1DirPad;
    float4 Light1DiffusePad;
    float4 Light2DirPad;
    float4 Light2DiffusePad;
    float4 FogColorEnabled;
    float4 FogStartEnd;
};

struct VSInput
{
    float3 Position     : POSITION0;
    float3 Normal       : NORMAL0;
    float4 Tangent      : TANGENT0;
    float2 UV           : TEXCOORD0;
    float4 BoneWeights  : BLENDWEIGHT0;
    uint4  BoneIndices  : BLENDINDICES0;
};

struct VSOutput
{
    float4 Position  : SV_Position;
    float3 Normal    : TEXCOORD0;
    float4 Tangent   : TEXCOORD1;
    float2 UV        : TEXCOORD2;
    float  FogFactor : TEXCOORD3;
    float3 WorldPos  : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    // Task 895 convention (matches skinned3d.vert.hlsl exactly): FNA's real Skin(vin, boneCount)
    // only sums the first WeightsPerVertex (1, 2, or 4) weight/index pairs.
    float weightsPerVertex = EyePosWeights.w;
    float4x4 skinMat = Bones[input.BoneIndices.x] * input.BoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += Bones[input.BoneIndices.y] * input.BoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += Bones[input.BoneIndices.z] * input.BoneWeights.z
                                           + Bones[input.BoneIndices.w] * input.BoneWeights.w;

    float4 skinnedPos = mul(float4(input.Position, 1.0), skinMat);
    output.Position = mul(skinnedPos, Mvp);

    // PBR + skinning combo: matches EasyGLGraphicsBackend::EnsurePbrSkinnedProgram() exactly --
    // plain World (NOT the inverse-transpose pbr3d.vert.hlsl's unskinned sibling uses), the same
    // simplification skinned3d.vert.hlsl's own normal transform already makes for skinned meshes.
    float3x3 skinNormalMat = (float3x3)skinMat;
    output.Normal = normalize(mul(mul(input.Normal, skinNormalMat), (float3x3)World));
    output.Tangent = float4(mul(mul(input.Tangent.xyz, skinNormalMat), (float3x3)World), input.Tangent.w);

    output.UV = input.UV;
    output.WorldPos = mul(skinnedPos, World).xyz;

    output.FogFactor = (FogColorEnabled.w > 0.5)
        ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6))
        : 1.0;

    return output;
}
