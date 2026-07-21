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
    float4 EmissiveColor;   // REMED-GFX-008: pre-folded (emissive + ambient*diffuse)*alpha
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

// REMED-GFX-006: HLSL has no built-in inverse()/mat3(mat4); this returns transpose(inverse(m))
// (the cofactor matrix over its determinant), matching lit_textured3d.vert.hlsl's own helper and
// the corrected Vulkan skinned3d.vert.glsl's transpose(inverse(mat3(world))).
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

    float weightsPerVertex = EyePosPad.w;
    float4x4 skinMat = Bones[input.BoneIndices.x] * input.BoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += Bones[input.BoneIndices.y] * input.BoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += Bones[input.BoneIndices.z] * input.BoneWeights.z
                                           + Bones[input.BoneIndices.w] * input.BoneWeights.w;
    float4 skinnedPos = mul(float4(input.Position, 1.0), skinMat);
    output.Position = mul(skinnedPos, Mvp);
    // REMED-GFX-006: compose the bone-skin 3x3 with the outer World inverse-transpose normal
    // matrix (was skin-only). Matches the corrected Vulkan skinned3d.vert.glsl exactly.
    output.Normal = normalize(mul(mul(input.Normal, (float3x3)skinMat), InverseTranspose3x3((float3x3)World)));
    output.UV = input.UV;
    output.WorldPos = mul(skinnedPos, World).xyz;
    output.Color = input.Color;
    // REMED-GFX-005/010: FNA view-space fog. FogStartEnd now carries EffectHelpers.SetFogVector
    // (World*View 3rd column baked CPU-side); keep = 1 - saturate(dot(pos, fogVector)) is the
    // corrected (non-mirrored) FNA factor in eye-space Z, not object-space. Zero vector = no fog.
    output.FogFactor = (FogColorEnabled.w > 0.5)
        ? 1.0 - saturate(dot(float4(skinnedPos.xyz, 1.0), FogStartEnd))
        : 1.0;

    return output;
}
