// Shader Model 5.0 (vs_5_0). Ported line-by-line from
// src/CNA/Internal/Backends/Vulkan/shaders/skinned3d.vert.glsl.
// Stride 52: VertexPositionNormalTextureSkinned.

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

// Task 899: fog -- BoneBlock has zero spare capacity, so fog gets its own dedicated constant
// buffer at register(b2).
cbuffer FogParams : register(b2)
{
    float4 FogColorEnabled;  // xyz = FogColor, w = fogEnabled
    float4 FogStartEnd;      // x = fogStart, y = fogEnd, zw = unused
    // Task 893: DirectionalLight1/DirectionalLight2 diffuse forwarding.
    float4 Light1DirPad;
    float4 Light1DiffPad;
    float4 Light2DirPad;
    float4 Light2DiffPad;
    // Task 894: World matrix + EyePosition + specular (World doesn't fit in the already-128-byte
    // PerDraw constant buffer, so it lives here instead).
    row_major float4x4 World;
    float4 EyePosPad;         // w = WeightsPerVertex (Task 895, packed into otherwise-unused padding)
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
};

struct VSOutput
{
    float4 Position  : SV_Position;
    float3 Normal    : TEXCOORD0;
    float2 UV        : TEXCOORD1;
    float  FogFactor : TEXCOORD2;
    float3 WorldPos  : TEXCOORD3;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    // Task 895: FNA's real Skin(vin, boneCount) only sums the first WeightsPerVertex (1, 2, or 4)
    // weight/index pairs -- matches XNA's own validated property range, so >=2/>=4 gating suffices.
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
    // Task 899: fog factor from the PRE-SKIN raw object-space Z (matches EasyGL/Bgfx's
    // established SkinnedEffect fog formula exactly -- Task 900/899 bonus scope).
    output.FogFactor = (FogColorEnabled.w > 0.5)
        ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6))
        : 1.0;

    return output;
}
