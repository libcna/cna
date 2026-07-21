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
    float4 EmissiveColor;   // REMED-GFX-008: pre-folded (emissive + ambient*diffuse)*alpha
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

    // Task 895: FNA's real Skin(vin, boneCount) only sums the first WeightsPerVertex (1, 2, or 4)
    // weight/index pairs -- matches XNA's own validated property range, so >=2/>=4 gating suffices.
    float weightsPerVertex = EyePosPad.w;
    float4x4 skinMat = Bones[input.BoneIndices.x] * input.BoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += Bones[input.BoneIndices.y] * input.BoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += Bones[input.BoneIndices.z] * input.BoneWeights.z
                                           + Bones[input.BoneIndices.w] * input.BoneWeights.w;
    float4 skinnedPos = mul(float4(input.Position, 1.0), skinMat);
    output.Position = mul(skinnedPos, Mvp);
    // REMED-GFX-006: compose the bone-skin 3x3 with the outer World inverse-transpose normal
    // matrix (was skin-only, so any rotated / non-uniformly-scaled skinned model was lit as if
    // World were identity). Matches the corrected Vulkan skinned3d.vert.glsl exactly.
    output.Normal = normalize(mul(mul(input.Normal, (float3x3)skinMat), InverseTranspose3x3((float3x3)World)));
    output.UV = input.UV;
    output.WorldPos = mul(skinnedPos, World).xyz;
    // REMED-GFX-005/010: FNA view-space fog. FogStartEnd now carries EffectHelpers.SetFogVector
    // (World*View 3rd column baked CPU-side); keep = 1 - saturate(dot(pos, fogVector)) is the
    // corrected (non-mirrored) FNA factor in eye-space Z, not object-space. Zero vector = no fog.
    output.FogFactor = (FogColorEnabled.w > 0.5)
        ? 1.0 - saturate(dot(float4(skinnedPos.xyz, 1.0), FogStartEnd))
        : 1.0;

    return output;
}
