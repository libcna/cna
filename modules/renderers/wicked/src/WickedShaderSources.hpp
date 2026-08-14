// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace CNA::Internal::Renderers::Wicked
{
    /**
     * @brief CNAEXT. The HLSL source of every built-in shader this renderer compiles.
     *
     * `wi::shadercompiler::Compile()` reads its input from a file path, so this source is written
     * into a per-process temporary directory at device creation and compiled from there
     * (plan_wicked.md design decision 4). Keeping it here rather than in a deployed asset file means
     * a CNA game needs no shader-asset deployment step at all, and the compiled output matches
     * whatever binary format `GraphicsDevice::GetShaderFormat()` reports -- SPIR-V for the Vulkan
     * device, DXIL for D3D12 -- from one source.
     *
     * Conventions, all chosen so the HLSL carries no packing assumptions:
     *   - The two matrices arrive as their four COLUMNS, so the shader evaluates XNA's own
     *     row-vector product `clip = position * matrix` as four `dot()`s.
     *   - Wicked Engine's Vulkan device binds viewports with a negative height, so clip space is
     *     D3D-shaped (Y down in window space, Z in [0,1]) on both device renderers.
     *   - Vertex colours arrive as `R8G8B8A8_UNORM`, matching XNA's `Color` byte order in memory.
     */
    CNAEXT inline constexpr const char* kWickedShaderSource = R"HLSL(
struct CnaConstants
{
    float4 mvp0;
    float4 mvp1;
    float4 mvp2;
    float4 mvp3;
    float4 world0;
    float4 world1;
    float4 world2;
    float4 world3;
    float4 diffuse;
    float4 emissive;        // rgb = emissive colour, w = specular power
    float4 specular;        // rgb = material specular colour
    float4 alphaTest;       // x = reference, y = tolerance, z = pass weight, w = fail weight
    float4 fogColor;        // rgb = fog colour, w != 0 when fog is enabled
    float4 fogVector;
    float4 ambient;         // rgb = ambient light colour
    float4 lightDir0;
    float4 lightDir1;
    float4 lightDir2;
    float4 lightDiffuse0;
    float4 lightDiffuse1;
    float4 lightDiffuse2;
    float4 lightSpecular0;
    float4 lightSpecular1;
    float4 lightSpecular2;
    float4 eyePosition;
    float4 flags;           // x = texture, y = vertex colour, z = lighting, w = dual texture
    float4 worldIT0;        // columns of the world inverse-transpose (normal matrix)
    float4 worldIT1;
    float4 worldIT2;
    float4 worldIT3;
    float4 envMapParams;    // x = amount, y = fresnel enabled, z = fresnel factor
    float4 envMapSpecular;  // rgb = EnvironmentMapEffect.EnvironmentMapSpecular
    float4 pbrFactors;      // x=metallic, y=roughness, z=normal scale, w=occlusion strength
};

ConstantBuffer<CnaConstants> cb : register(b0);

Texture2D<float4> texture0 : register(t0);
Texture2D<float4> texture1 : register(t1);
TextureCube<float4> environmentMap : register(t2);
Texture2D<float4> normalMap : register(t3);
Texture2D<float4> metallicRoughnessMap : register(t4);
Texture2D<float4> emissiveMap : register(t5);
Texture2D<float4> occlusionMap : register(t6);
SamplerState sampler0 : register(s0);

struct VSOut
{
    float4 position : SV_Position;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD0;
    float3 normalWS : TEXCOORD1;
    float3 positionWS : TEXCOORD2;
    float4 positionOS : TEXCOORD3;
};

float4 TransformPosition(float3 p)
{
    float4 v = float4(p, 1.0f);
    return float4(dot(v, cb.mvp0), dot(v, cb.mvp1), dot(v, cb.mvp2), dot(v, cb.mvp3));
}

float3 TransformToWorld(float3 p)
{
    float4 v = float4(p, 1.0f);
    return float3(dot(v, cb.world0), dot(v, cb.world1), dot(v, cb.world2));
}

float3 TransformNormalToWorld(float3 n)
{
    float4 v = float4(n, 0.0f);
    return float3(dot(v, cb.world0), dot(v, cb.world1), dot(v, cb.world2));
}

VSOut FillCommon(float3 position)
{
    VSOut o = (VSOut)0;
    o.position = TransformPosition(position);
    o.positionOS = float4(position, 1.0f);
    o.positionWS = TransformToWorld(position);
    o.color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    o.uv = float2(0.0f, 0.0f);
    o.normalWS = float3(0.0f, 0.0f, 1.0f);
    return o;
}

// Stride 16 -- VertexPositionColor.
VSOut Basic16VS(float3 position : POSITION, float4 color : COLOR)
{
    VSOut o = FillCommon(position);
    o.color = color;
    return o;
}

// Stride 20 -- VertexPositionTexture.
VSOut Basic20VS(float3 position : POSITION, float2 uv : TEXCOORD0)
{
    VSOut o = FillCommon(position);
    o.uv = uv;
    return o;
}

// Stride 24 -- VertexPositionColorTexture (also the SpriteBatch and internal quad layout).
VSOut Basic24VS(float3 position : POSITION, float4 color : COLOR, float2 uv : TEXCOORD0)
{
    VSOut o = FillCommon(position);
    o.color = color;
    o.uv = uv;
    return o;
}

// Stride 32 -- VertexPositionNormalTexture.
VSOut Basic32VS(float3 position : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD0)
{
    VSOut o = FillCommon(position);
    o.normalWS = TransformNormalToWorld(normal);
    o.uv = uv;
    return o;
}

// ---------------------------------------------------------------------------------------------
// Tangent and skinned geometry layouts.
//
// These entry points accept the wider stock vertex layouts so geometry authored for PbrEffect or
// SkinnedEffect can be drawn with the ordinary shading above. The tangent, blend-weight and
// blend-index attributes are DECLARED but not consumed: declaring them is what makes the input
// layout match the buffer, and consuming them is the job of the PbrEffect/SkinnedEffect programs
// that plan_wicked.md WICKED-56b adds. A draw that actually asks for skinning is refused by the
// renderer rather than silently rendered in bind pose here.
// ---------------------------------------------------------------------------------------------

// Stride 48 -- VertexPositionNormalTangentTexture.
VSOut Basic48VS(float3 position : POSITION, float3 normal : NORMAL,
                float4 tangent : TANGENT, float2 uv : TEXCOORD0)
{
    VSOut o = FillCommon(position);
    o.normalWS = TransformNormalToWorld(normal);
    o.uv = uv;
    return o;
}

// Stride 52 -- VertexPositionNormalTextureSkinned.
VSOut Basic52VS(float3 position : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD0,
                float4 blendWeights : BLENDWEIGHT, uint4 blendIndices : BLENDINDICES)
{
    VSOut o = FillCommon(position);
    o.normalWS = TransformNormalToWorld(normal);
    o.uv = uv;
    return o;
}

// Stride 56 -- the stride-52 layout with a per-vertex Color appended.
VSOut Basic56VS(float3 position : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD0,
                float4 blendWeights : BLENDWEIGHT, uint4 blendIndices : BLENDINDICES,
                float4 color : COLOR)
{
    VSOut o = FillCommon(position);
    o.normalWS = TransformNormalToWorld(normal);
    o.uv = uv;
    o.color = color;
    return o;
}

// Stride 68 -- VertexPositionNormalTangentTextureSkinned.
VSOut Basic68VS(float3 position : POSITION, float3 normal : NORMAL,
                float4 tangent : TANGENT, float2 uv : TEXCOORD0,
                float4 blendWeights : BLENDWEIGHT, uint4 blendIndices : BLENDINDICES)
{
    VSOut o = FillCommon(position);
    o.normalWS = TransformNormalToWorld(normal);
    o.uv = uv;
    return o;
}

// ---------------------------------------------------------------------------------------------
// SkinnedEffect.
//
// Bone matrices arrive as CNA's raw per-bone `Matrix` bytes, read column-wise exactly like the
// instance matrices above, so `bone * position` is the column-vector product that equals XNA's
// row-vector `position * matrix`.
//
// FNA's Skin(vin, boneCount) sums only the first WeightsPerVertex (1, 2 or 4) weight/index pairs,
// which is what the gating below reproduces -- summing all four unconditionally would blend in
// bones the effect never selected.
//
// Skinning happens in object space, before the world transform, so `cb.mvp` (world * view *
// projection) still applies unchanged and the fog term uses the POST-skin object-space position.
// ---------------------------------------------------------------------------------------------

struct CnaBones
{
    float4 boneColumns[72 * 4];
    float4 skinParams;      // x = WeightsPerVertex
};

ConstantBuffer<CnaBones> bones : register(b1);

float3 ApplyBonePosition(uint bone, float3 p)
{
    const uint base = bone * 4;
    return (bones.boneColumns[base + 0] * p.x
          + bones.boneColumns[base + 1] * p.y
          + bones.boneColumns[base + 2] * p.z
          + bones.boneColumns[base + 3]).xyz;
}

float3 ApplyBoneNormal(uint bone, float3 n)
{
    const uint base = bone * 4;
    return (bones.boneColumns[base + 0] * n.x
          + bones.boneColumns[base + 1] * n.y
          + bones.boneColumns[base + 2] * n.z).xyz;
}

void SkinVertex(float3 position, float3 normal, float4 weights, uint4 indices,
                out float3 skinnedPosition, out float3 skinnedNormal)
{
    const float weightsPerVertex = bones.skinParams.x;

    skinnedPosition = ApplyBonePosition(indices.x, position) * weights.x;
    skinnedNormal   = ApplyBoneNormal(indices.x, normal) * weights.x;

    if (weightsPerVertex >= 2.0f)
    {
        skinnedPosition += ApplyBonePosition(indices.y, position) * weights.y;
        skinnedNormal   += ApplyBoneNormal(indices.y, normal) * weights.y;
    }
    if (weightsPerVertex >= 4.0f)
    {
        skinnedPosition += ApplyBonePosition(indices.z, position) * weights.z
                         + ApplyBonePosition(indices.w, position) * weights.w;
        skinnedNormal   += ApplyBoneNormal(indices.z, normal) * weights.z
                         + ApplyBoneNormal(indices.w, normal) * weights.w;
    }
}

VSOut FillSkinned(float3 position, float3 normal, float2 uv, float4 weights, uint4 indices)
{
    float3 skinnedPosition;
    float3 skinnedNormal;
    SkinVertex(position, normal, weights, indices, skinnedPosition, skinnedNormal);

    VSOut o = (VSOut)0;
    o.position = TransformPosition(skinnedPosition);
    // The fog term is evaluated on the post-skin object-space position, so a skinned vertex fogs
    // by where it actually ended up rather than by its bind-pose location.
    o.positionOS = float4(skinnedPosition, 1.0f);
    o.positionWS = TransformToWorld(skinnedPosition);

    // The bone-skin rotation composes with the outer world normal matrix; leaving the world factor
    // out would light a rotated or non-uniformly scaled model as if World were identity.
    const float4 n = float4(skinnedNormal, 0.0f);
    o.normalWS = float3(dot(n, cb.worldIT0), dot(n, cb.worldIT1), dot(n, cb.worldIT2));

    o.color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    o.uv = uv;
    return o;
}

// Stride 52 -- VertexPositionNormalTextureSkinned.
VSOut Skinned52VS(float3 position : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD0,
                  float4 blendWeights : BLENDWEIGHT, uint4 blendIndices : BLENDINDICES)
{
    return FillSkinned(position, normal, uv, blendWeights, blendIndices);
}

// Stride 56 -- the stride-52 layout with a per-vertex Color appended.
VSOut Skinned56VS(float3 position : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD0,
                  float4 blendWeights : BLENDWEIGHT, uint4 blendIndices : BLENDINDICES,
                  float4 color : COLOR)
{
    VSOut o = FillSkinned(position, normal, uv, blendWeights, blendIndices);
    o.color = color;
    return o;
}

// Stride 68 -- VertexPositionNormalTangentTextureSkinned.
VSOut Skinned68VS(float3 position : POSITION, float3 normal : NORMAL,
                  float4 tangent : TANGENT, float2 uv : TEXCOORD0,
                  float4 blendWeights : BLENDWEIGHT, uint4 blendIndices : BLENDINDICES)
{
    return FillSkinned(position, normal, uv, blendWeights, blendIndices);
}

// ---------------------------------------------------------------------------------------------
// PbrEffect -- the glTF 2.0 metallic-roughness BRDF (GGX distribution, Smith-Schlick-GGX
// visibility, Schlick Fresnel), term for term the same reference BRDF the other CNA renderers
// evaluate. Four texture slots beyond the base colour: normal, metallic-roughness (glTF packing,
// G = roughness, B = metallic), emissive and occlusion.
//
// The tangent transforms as a plain direction under the world matrix rather than under its
// inverse-transpose -- correct for uniform scale, and the same documented simplification the
// established CNA PBR path makes.
// ---------------------------------------------------------------------------------------------

struct PbrVSOut
{
    float4 position      : SV_Position;
    float3 normalWS      : TEXCOORD0;
    float3 tangentWS     : TEXCOORD1;
    float  bitangentSign : TEXCOORD2;
    float2 uv            : TEXCOORD3;
    float3 positionWS    : TEXCOORD4;
    float4 positionOS    : TEXCOORD5;
};

PbrVSOut FillPbr(float3 position, float3 normal, float4 tangent, float2 uv)
{
    PbrVSOut o;
    o.position = TransformPosition(position);
    o.positionOS = float4(position, 1.0f);
    o.positionWS = TransformToWorld(position);

    const float4 n = float4(normal, 0.0f);
    o.normalWS = normalize(float3(dot(n, cb.worldIT0), dot(n, cb.worldIT1), dot(n, cb.worldIT2)));
    o.tangentWS = TransformNormalToWorld(tangent.xyz);
    o.bitangentSign = tangent.w;
    o.uv = uv;
    return o;
}

// Stride 48 -- VertexPositionNormalTangentTexture.
PbrVSOut Pbr48VS(float3 position : POSITION, float3 normal : NORMAL,
                 float4 tangent : TANGENT, float2 uv : TEXCOORD0)
{
    return FillPbr(position, normal, tangent, uv);
}

// Stride 68 -- VertexPositionNormalTangentTextureSkinned, drawn unskinned.
PbrVSOut Pbr68VS(float3 position : POSITION, float3 normal : NORMAL,
                 float4 tangent : TANGENT, float2 uv : TEXCOORD0,
                 float4 blendWeights : BLENDWEIGHT, uint4 blendIndices : BLENDINDICES)
{
    return FillPbr(position, normal, tangent, uv);
}

// Stride 68 -- SkinnedPbrEffect.
PbrVSOut PbrSkinned68VS(float3 position : POSITION, float3 normal : NORMAL,
                        float4 tangent : TANGENT, float2 uv : TEXCOORD0,
                        float4 blendWeights : BLENDWEIGHT, uint4 blendIndices : BLENDINDICES)
{
    float3 skinnedPosition;
    float3 skinnedNormal;
    SkinVertex(position, normal, blendWeights, blendIndices, skinnedPosition, skinnedNormal);

    float3 skinnedTangent;
    float3 ignoredNormal;
    SkinVertex(tangent.xyz, normal, blendWeights, blendIndices, skinnedTangent, ignoredNormal);
    // SkinVertex applies the bone translation; a tangent is a direction, so remove it again by
    // skinning the origin and subtracting.
    float3 skinnedOrigin;
    SkinVertex(float3(0.0f, 0.0f, 0.0f), normal, blendWeights, blendIndices,
               skinnedOrigin, ignoredNormal);
    skinnedTangent -= skinnedOrigin;

    PbrVSOut o;
    o.position = TransformPosition(skinnedPosition);
    o.positionOS = float4(skinnedPosition, 1.0f);
    o.positionWS = TransformToWorld(skinnedPosition);

    const float4 n = float4(skinnedNormal, 0.0f);
    o.normalWS = normalize(float3(dot(n, cb.worldIT0), dot(n, cb.worldIT1), dot(n, cb.worldIT2)));
    o.tangentWS = TransformNormalToWorld(skinnedTangent);
    o.bitangentSign = tangent.w;
    o.uv = uv;
    return o;
}

float3 PbrLight(float3 N, float3 V, float3 L, float3 lightColor, float3 albedo, float3 F0,
                float roughness, float metallic)
{
    const float3 H = normalize(V + L);
    const float NdotL = max(dot(N, L), 0.0f);
    const float NdotV = max(dot(N, V), 1e-4f);
    const float NdotH = max(dot(N, H), 0.0f);
    const float VdotH = max(dot(V, H), 0.0f);

    const float a2 = pow(roughness, 4.0f);
    const float dTerm = (NdotH * NdotH * (a2 - 1.0f) + 1.0f);
    const float D = a2 / (3.14159265f * dTerm * dTerm + 1e-7f);

    float k = (roughness + 1.0f);
    k = k * k / 8.0f;
    const float G = (NdotV / (NdotV * (1.0f - k) + k)) * (NdotL / (NdotL * (1.0f - k) + k));

    const float3 F = F0 + (float3(1.0f, 1.0f, 1.0f) - F0) * pow(saturate(1.0f - VdotH), 5.0f);
    const float3 specular = (D * G * F) / max(4.0f * NdotV * NdotL, 1e-4f);
    const float3 diffuseColor = albedo * (1.0f - metallic);
    const float3 kd = float3(1.0f, 1.0f, 1.0f) - F;
    return (kd * diffuseColor / 3.14159265f + specular) * lightColor * NdotL;
}

float4 PbrPS(PbrVSOut input) : SV_Target
{
    const float4 baseColorTex = texture0.Sample(sampler0, input.uv);
    const float3 albedo = baseColorTex.rgb * cb.diffuse.rgb;
    const float alpha = baseColorTex.a * cb.diffuse.a;

    const float3 N = normalize(input.normalWS);
    const float3 T = normalize(input.tangentWS - N * dot(N, input.tangentWS));
    const float3 B = cross(N, T) * input.bitangentSign;
    const float3x3 TBN = float3x3(T, B, N);
    float3 sampledNormal = normalMap.Sample(sampler0, input.uv).rgb * 2.0f - 1.0f;
    sampledNormal.xy *= cb.pbrFactors.z;
    const float3 finalNormal = normalize(mul(sampledNormal, TBN));

    const float4 mr = metallicRoughnessMap.Sample(sampler0, input.uv);
    const float roughness = clamp(mr.g * cb.pbrFactors.y, 0.045f, 1.0f);
    const float metallic = saturate(mr.b * cb.pbrFactors.x);

    const float3 V = normalize(cb.eyePosition.xyz - input.positionWS);
    const float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    float3 Lo = float3(0.0f, 0.0f, 0.0f);
    Lo += PbrLight(finalNormal, V, normalize(-cb.lightDir0.xyz), cb.lightDiffuse0.rgb,
                   albedo, F0, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-cb.lightDir1.xyz), cb.lightDiffuse1.rgb,
                   albedo, F0, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-cb.lightDir2.xyz), cb.lightDiffuse2.rgb,
                   albedo, F0, roughness, metallic);

    const float occlusionSample = occlusionMap.Sample(sampler0, input.uv).r;
    const float occlusion = 1.0f + cb.pbrFactors.w * (occlusionSample - 1.0f);
    const float3 ambient = cb.ambient.rgb * albedo * occlusion;
    const float3 emissive = cb.emissive.rgb * emissiveMap.Sample(sampler0, input.uv).rgb;

    const float selected = (cb.alphaTest.y > 0.0f)
        ? ((abs(alpha - cb.alphaTest.x) < cb.alphaTest.y) ? cb.alphaTest.z : cb.alphaTest.w)
        : ((alpha < cb.alphaTest.x) ? cb.alphaTest.z : cb.alphaTest.w);
    clip(selected);

    float3 rgb = ambient + Lo + emissive;
    if (cb.fogColor.w != 0.0f)
    {
        const float keep = 1.0f - saturate(dot(input.positionOS, cb.fogVector));
        rgb = lerp(cb.fogColor.rgb, rgb, keep);
    }
    return float4(rgb, alpha);
}

// ---------------------------------------------------------------------------------------------
// Instanced variants.
//
// The per-instance stream is CNA's established 64-byte layout: a column-major float4x4 world
// matrix delivered as four float4 attributes at byte offsets 0/16/32/48, identical to the Vulkan
// renderer's own instanced pipeline. `world * position` is therefore the column-vector product
// `sum(position[k] * column[k])`, which is exactly XNA's row-vector `position * matrix` once the
// matrix's raw bytes are read column-wise -- so an unmodified XNA Matrix in the instance buffer
// means the same thing on both renderers.
//
// On these entry points `cb.mvp` holds VIEW * PROJECTION only; the per-instance matrix supplies
// the world transform that `cb.world` carries on the non-instanced entry points.
// ---------------------------------------------------------------------------------------------

float3 InstanceTransformPosition(float3 p, float4 i0, float4 i1, float4 i2, float4 i3)
{
    float4 world = i0 * p.x + i1 * p.y + i2 * p.z + i3;
    return world.xyz;
}

float3 InstanceTransformNormal(float3 n, float4 i0, float4 i1, float4 i2, float4 i3)
{
    float4 world = i0 * n.x + i1 * n.y + i2 * n.z;
    return world.xyz;
}

VSOut FillInstanced(float3 position, float4 i0, float4 i1, float4 i2, float4 i3)
{
    VSOut o = (VSOut)0;
    const float3 worldPosition = InstanceTransformPosition(position, i0, i1, i2, i3);
    const float4 v = float4(worldPosition, 1.0f);
    o.position = float4(dot(v, cb.mvp0), dot(v, cb.mvp1), dot(v, cb.mvp2), dot(v, cb.mvp3));
    o.positionOS = float4(position, 1.0f);
    o.positionWS = worldPosition;
    o.color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    o.uv = float2(0.0f, 0.0f);
    o.normalWS = float3(0.0f, 0.0f, 1.0f);
    return o;
}

VSOut Basic16InstVS(float3 position : POSITION, float4 color : COLOR,
                    float4 i0 : TEXCOORD4, float4 i1 : TEXCOORD5,
                    float4 i2 : TEXCOORD6, float4 i3 : TEXCOORD7)
{
    VSOut o = FillInstanced(position, i0, i1, i2, i3);
    o.color = color;
    return o;
}

VSOut Basic20InstVS(float3 position : POSITION, float2 uv : TEXCOORD0,
                    float4 i0 : TEXCOORD4, float4 i1 : TEXCOORD5,
                    float4 i2 : TEXCOORD6, float4 i3 : TEXCOORD7)
{
    VSOut o = FillInstanced(position, i0, i1, i2, i3);
    o.uv = uv;
    return o;
}

VSOut Basic24InstVS(float3 position : POSITION, float4 color : COLOR, float2 uv : TEXCOORD0,
                    float4 i0 : TEXCOORD4, float4 i1 : TEXCOORD5,
                    float4 i2 : TEXCOORD6, float4 i3 : TEXCOORD7)
{
    VSOut o = FillInstanced(position, i0, i1, i2, i3);
    o.color = color;
    o.uv = uv;
    return o;
}

VSOut Basic32InstVS(float3 position : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD0,
                    float4 i0 : TEXCOORD4, float4 i1 : TEXCOORD5,
                    float4 i2 : TEXCOORD6, float4 i3 : TEXCOORD7)
{
    VSOut o = FillInstanced(position, i0, i1, i2, i3);
    o.normalWS = InstanceTransformNormal(normal, i0, i1, i2, i3);
    o.uv = uv;
    return o;
}

float3 ApplyLighting(float3 albedo, float3 normalWS, float3 positionWS)
{
    float3 n = normalize(normalWS);
    float3 lightSum = cb.ambient.rgb;
    float3 specularSum = float3(0.0f, 0.0f, 0.0f);
    float3 eyeDir = normalize(cb.eyePosition.xyz - positionWS);

    float3 dirs[3] = { cb.lightDir0.xyz, cb.lightDir1.xyz, cb.lightDir2.xyz };
    float3 diffuses[3] = { cb.lightDiffuse0.rgb, cb.lightDiffuse1.rgb, cb.lightDiffuse2.rgb };
    float3 speculars[3] = { cb.lightSpecular0.rgb, cb.lightSpecular1.rgb, cb.lightSpecular2.rgb };

    [unroll]
    for (int i = 0; i < 3; ++i)
    {
        float3 toLight = -dirs[i];
        float ndotl = saturate(dot(n, toLight));
        lightSum += diffuses[i] * ndotl;

        float3 halfVector = normalize(toLight + eyeDir);
        float ndoth = saturate(dot(n, halfVector));
        float power = max(cb.emissive.w, 1.0f);
        specularSum += speculars[i] * pow(ndoth, power) * step(0.0001f, ndotl);
    }

    return albedo * lightSum + cb.specular.rgb * specularSum + cb.emissive.rgb;
}

float4 BasicPS(VSOut input) : SV_Target
{
    float4 color = cb.diffuse;

    if (cb.flags.y != 0.0f)
    {
        color *= input.color;
    }
    if (cb.flags.x != 0.0f)
    {
        color *= texture0.Sample(sampler0, input.uv);
    }
    if (cb.flags.w != 0.0f)
    {
        color *= texture1.Sample(sampler0, input.uv);
    }

    // FNA's alpha-test comparison: a tolerance above zero means "equal/not-equal within
    // tolerance", otherwise it is a plain less-than comparison. A negative selected weight
    // discards the pixel.
    float selected = (cb.alphaTest.y > 0.0f)
        ? ((abs(color.a - cb.alphaTest.x) < cb.alphaTest.y) ? cb.alphaTest.z : cb.alphaTest.w)
        : ((color.a < cb.alphaTest.x) ? cb.alphaTest.z : cb.alphaTest.w);
    clip(selected);

    if (cb.flags.z != 0.0f)
    {
        color.rgb = ApplyLighting(color.rgb, input.normalWS, input.positionWS);
    }

    if (cb.fogColor.w != 0.0f)
    {
        float keep = 1.0f - saturate(dot(input.positionOS, cb.fogVector));
        color.rgb = lerp(cb.fogColor.rgb, color.rgb, keep);
    }

    return color;
}

// ---------------------------------------------------------------------------------------------
// EnvironmentMapEffect.
//
// Deliberately its own entry-point pair rather than another BasicPS branch: FNA's environment-map
// shading is not "BasicEffect plus a cube sample". It folds ambient into the emissive term on the
// CPU and adds it UNSCALED after the light sum is multiplied by DiffuseColor, has no material
// specular term at all, and scales the whole cube sample by the combined alpha. Every one of those
// is a different expression from the one BasicPS evaluates, and the arithmetic below matches the
// established CNA env-map shading exactly.
// ---------------------------------------------------------------------------------------------

struct EnvMapVSOut
{
    float4 position   : SV_Position;
    float3 normalWS   : TEXCOORD0;
    float3 eyeDir     : TEXCOORD1;
    float2 uv         : TEXCOORD2;
    float4 positionOS : TEXCOORD3;
};

EnvMapVSOut EnvMapVS(float3 position : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD0)
{
    EnvMapVSOut o;
    o.position = TransformPosition(position);
    o.positionOS = float4(position, 1.0f);

    const float4 p = float4(position, 1.0f);
    const float3 worldPosition =
        float3(dot(p, cb.world0), dot(p, cb.world1), dot(p, cb.world2));

    // The normal matrix is the world inverse-transpose, computed on the CPU so a non-uniform
    // scale is handled exactly instead of being approximated by the world matrix's upper 3x3.
    const float4 n = float4(normal, 0.0f);
    o.normalWS = normalize(float3(dot(n, cb.worldIT0), dot(n, cb.worldIT1), dot(n, cb.worldIT2)));

    o.eyeDir = cb.eyePosition.xyz - worldPosition;
    o.uv = uv;
    return o;
}

float4 EnvMapPS(EnvMapVSOut input) : SV_Target
{
    const float3 N = normalize(input.normalWS);
    const float3 E = normalize(input.eyeDir);

    const float3 lightSum =
          cb.lightDiffuse0.rgb * max(dot(N, -cb.lightDir0.xyz), 0.0f)
        + cb.lightDiffuse1.rgb * max(dot(N, -cb.lightDir1.xyz), 0.0f)
        + cb.lightDiffuse2.rgb * max(dot(N, -cb.lightDir2.xyz), 0.0f);

    // cb.emissive.rgb is the CPU-side pre-folded (EmissiveColor + AmbientLightColor * DiffuseColor)
    // * alpha, added unscaled -- multiplying it by DiffuseColor again would re-scale the ambient
    // term a second time.
    const float3 litRGB = lightSum * cb.diffuse.rgb + cb.emissive.rgb;

    const float4 texColor = texture0.Sample(sampler0, input.uv);
    const float combinedAlpha = cb.diffuse.a * texColor.a;
    const float3 baseColor = litRGB * texColor.rgb;

    const float3 reflectionDir = reflect(-E, N);
    const float4 envSample = environmentMap.Sample(sampler0, reflectionDir);

    const float viewAngle = dot(E, N);
    const float blendFactor = (cb.envMapParams.y > 0.5f)
        ? pow(max(1.0f - abs(viewAngle), 0.0f), cb.envMapParams.z) * cb.envMapParams.x
        : cb.envMapParams.x;

    float3 rgb = lerp(baseColor, envSample.rgb * combinedAlpha, blendFactor)
               + cb.envMapSpecular.rgb * envSample.a * combinedAlpha;

    if (cb.fogColor.w != 0.0f)
    {
        const float keep = 1.0f - saturate(dot(input.positionOS, cb.fogVector));
        rgb = lerp(cb.fogColor.rgb, rgb, keep);
    }

    return float4(rgb, combinedAlpha);
}
)HLSL";
}
