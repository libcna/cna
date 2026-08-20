// Shader Model 5.0 (vs_5_0). PBR + skinning combo (SkinnedPbrEffect) -- HLSL port of
// EasyGLRenderer::EnsurePbrSkinnedProgram()'s vertex stage (plans/plan_cnj.md CNB-58 follow-up):
// pbr3d.vert.hlsl's own transform, with bone skinning (up to 72 bones, 1/2/4 weights per vertex)
// applied to Position/Normal/Tangent before the World transform, mirroring skinned3d.vert.hlsl's
// own skinning math exactly.
// Stride 68: VertexPositionNormalTangentTextureSkinned (the stride-48 PbrEffect layout with the
// stride-52 skinning suffix -- BlendWeight, BlendIndices -- appended). CNA_PBR_DUAL_UV adds the
// canonical stride-76 TEXCOORD_1 suffix after the complete skinned prefix.

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
    float4 FogColor;
    float4 FogVector;
};

struct VSInput
{
    float3 Position     : POSITION0;
    float3 Normal       : NORMAL0;
    float4 Tangent      : TANGENT0;
    float2 UV           : TEXCOORD0;
    float4 BoneWeights  : BLENDWEIGHT0;
    uint4  BoneIndices  : BLENDINDICES0;
#ifdef CNA_PBR_DUAL_UV
    float2 UV1          : TEXCOORD1;
#endif
#ifdef CNA_PBR_VERTEX_COLOR
    float4 Color    : COLOR0;
#endif
};

struct VSOutput
{
    float4 Position  : SV_Position;
    float3 Normal    : TEXCOORD0;
    float4 Tangent   : TEXCOORD1;
    float2 UV        : TEXCOORD2;
    float  FogFactor : TEXCOORD3;
    float3 WorldPos  : TEXCOORD4;
#ifdef CNA_PBR_DUAL_UV
    float2 UV1       : TEXCOORD5;
#endif
#ifdef CNA_PBR_VERTEX_COLOR
    float4 Color     : TEXCOORD6;
#endif
};

// REMED-GFX-006: HLSL has no built-in inverse()/mat3(mat4); this returns transpose(inverse(m))
// (the cofactor matrix over its determinant), matching pbr3d.vert.hlsl's own unskinned normal
// matrix and the corrected Vulkan pbr3d_skinned.vert.glsl's transpose(inverse(mat3(world))).
float3x3 InverseTranspose3x3(float3x3 m)
{
    float3 c0 = cross(m[1], m[2]);
    float3 c1 = cross(m[2], m[0]);
    float3 c2 = cross(m[0], m[1]);
    float det = dot(m[0], c0);
    return float3x3(c0, c1, c2) / det;
}

float3 CnaSkinNormal(float3x3 m, float3 n)
{
    float3 c0 = cross(m[1], m[2]);
    float3 c1 = cross(m[2], m[0]);
    float3 c2 = cross(m[0], m[1]);
    float det = dot(m[0], c0);
    float3 transformed = mul(n, float3x3(c0, c1, c2));
    return abs(det) > 1e-6 ? transformed * sign(det) : mul(n, m);
}

float CnaDirectionHandedness(float3x3 m)
{
    return dot(m[0], cross(m[1], m[2])) < 0.0 ? -1.0 : 1.0;
}

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

    // GLTF-264/REMED-GFX-006: inverse-transpose both the blended joint matrix and World. The
    // tangent stays on their raw direction matrices (glTF convention).
    float3x3 skinNormalMat = (float3x3)skinMat;
    float3x3 worldDirectionMat = (float3x3)World;
    output.Normal = normalize(mul(CnaSkinNormal(skinNormalMat, input.Normal), InverseTranspose3x3(worldDirectionMat)));
    output.Tangent = float4(mul(mul(input.Tangent.xyz, skinNormalMat), worldDirectionMat),
                            input.Tangent.w * CnaDirectionHandedness(worldDirectionMat)
                                * CnaDirectionHandedness(skinNormalMat));

    output.UV = input.UV;
#ifdef CNA_PBR_DUAL_UV
    output.UV1 = input.UV1;
#endif
#ifdef CNA_PBR_VERTEX_COLOR
    output.Color = input.Color;
#endif
    output.WorldPos = mul(skinnedPos, World).xyz;

    // REMED-GFX-005/010: FNA view-space fog. FogVector now carries EffectHelpers.SetFogVector
    // (World*View 3rd column baked CPU-side); keep = 1 - saturate(dot(pos, fogVector)) is the
    // corrected (non-mirrored) FNA factor in eye-space Z, not object-space. Zero vector = no fog.
    output.FogFactor = 1.0 - saturate(dot(float4(skinnedPos.xyz, 1.0), FogVector));

    return output;
}
