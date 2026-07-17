// Shader Model 5.0 (vs_5_0). skinned3d_vertexlit.vert.hlsl's own stride-56 sibling: adds a
// per-vertex Color (COLOR0) input, matching skinned_colored3d.vert.hlsl's own per-pixel-lit
// sibling. Ported from EasyGLGraphicsBackend::EnsureSkinnedVertexLitProgram()'s vertex stage
// (CNB-67, Phase 13C) -- identical skinning + per-vertex Blinn-Phong math, plus Color pass-through
// (vertex color is a final-output modulation, not part of the light sum itself, so it is simply
// forwarded unlit here and applied in the pixel shader).
// Stride 56: the stride-52 SkinnedVertex layout with a per-vertex Color appended at offset 52.

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
    float4 Position    : SV_Position;
    float2 UV          : TEXCOORD0;
    float  FogFactor   : TEXCOORD1;
    float3 LitRGB      : TEXCOORD2;
    float3 SpecularRGB : TEXCOORD3;
    float  Alpha       : TEXCOORD4;
    float4 Color       : TEXCOORD5;
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
    output.UV = input.UV;
    output.Color = input.Color;

    float3 N = normalize(mul(input.Normal, (float3x3)skinMat));
    float3 worldPos = mul(skinnedPos, World).xyz;
    float3 E = normalize(EyePosPad.xyz - worldPos);

    float dotL0 = dot(N, -normalize(Light0Dir));        float zeroL0 = step(0.0, dotL0);       float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -normalize(Light1DirPad.xyz)); float zeroL1 = step(0.0, dotL1);       float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -normalize(Light2DirPad.xyz)); float zeroL2 = step(0.0, dotL2);       float NdotL2 = max(dotL2, 0.0);
    float3 lightSum = Light0Diffuse * NdotL0
                     + Light1DiffPad.xyz * NdotL1
                     + Light2DiffPad.xyz * NdotL2;
    output.LitRGB = (AmbientColor + lightSum) * DiffuseColor.rgb;

    float specularPower = SpecularColorPower.w;
    float3 h0 = normalize(E - normalize(Light0Dir));        float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, specularPower);
    float3 h1 = normalize(E - normalize(Light1DirPad.xyz)); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, specularPower);
    float3 h2 = normalize(E - normalize(Light2DirPad.xyz)); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, specularPower);
    output.SpecularRGB = (spec0 * Light0SpecPad.xyz + spec1 * Light1SpecPad.xyz
                          + spec2 * Light2SpecPad.xyz) * SpecularColorPower.xyz;

    output.Alpha = DiffuseColor.a;
    output.FogFactor = (FogColorEnabled.w > 0.5)
        ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6))
        : 1.0;

    return output;
}
