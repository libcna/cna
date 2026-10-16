// SPDX-License-Identifier: MS-PL
//
// CNA-owned FNA3D extension for BasicEffect/SkinnedEffect punctual lighting.
// FNA3D accepts only compiled Direct3D 9 Effect Framework binaries, so the corresponding
// Three compiled variants are committed beside this source and embedded with the stock effects;
// their exact commands and hashes are recorded in README.md.

texture Texture;
sampler2D TextureSampler = sampler_state
{
    Texture = <Texture>;
};

float4x4 WorldViewProj;
float4x4 World;
float3x3 WorldInverseTranspose;
float4 FogVector;
float4x3 Bones[72];
float4 SkinningParams; // x = weights per vertex

float4 DiffuseColor;
float3 EmissiveColor;
float3 SpecularColor;
float SpecularPower;
float3 EyePosition;
float3 DirLight0Direction;
float3 DirLight0DiffuseColor;
float3 DirLight0SpecularColor;
float3 DirLight1Direction;
float3 DirLight1DiffuseColor;
float3 DirLight1SpecularColor;
float3 DirLight2Direction;
float3 DirLight2DiffuseColor;
float3 DirLight2SpecularColor;
float4 PunctualPositionRange; // xyz = world position, w = range
float4 PunctualDirectionKind; // xyz = spot direction, w = 1 point / 2 spot
float3 PunctualDiffuse;
float2 PunctualCone; // x = cos(inner), y = cos(outer)
float3 FogColor;

#define CNA_D3D9_FNA_SHADOWS 1
#include "../../directx9/src/shaders/cna/ShadowSampling3D.fxh"

struct VSInputNm
{
    float4 Position : POSITION0;
    float3 Normal : NORMAL0;
};

struct VSInputNmVc
{
    float4 Position : POSITION0;
    float3 Normal : NORMAL0;
    float4 Color : COLOR0;
};

struct VSInputNmTx
{
    float4 Position : POSITION0;
    float3 Normal : NORMAL0;
    float2 TexCoord : TEXCOORD0;
};

struct VSInputNmTxVc
{
    float4 Position : POSITION0;
    float3 Normal : NORMAL0;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR0;
};

struct VSInputNmTxSkinned
{
    float4 Position : POSITION0;
    float3 Normal : NORMAL0;
    float2 TexCoord : TEXCOORD0;
    float4 BoneWeights : BLENDWEIGHT0;
    int4 BoneIndices : BLENDINDICES0;
};

struct VSInputNmTxSkinnedVc
{
    float4 Position : POSITION0;
    float3 Normal : NORMAL0;
    float2 TexCoord : TEXCOORD0;
    float4 BoneWeights : BLENDWEIGHT0;
    int4 BoneIndices : BLENDINDICES0;
    float4 Color : COLOR0;
};

struct VSOutput
{
    float4 PositionPS : POSITION0;
    float3 PositionWS : TEXCOORD1;
    float3 NormalWS : TEXCOORD2;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR0;
    float FogFactor : TEXCOORD3;
};

VSOutput BuildOutput(float4 position, float3 normal, float2 uv, float4 color)
{
    VSOutput result;
    result.PositionPS = mul(position, WorldViewProj);
    result.PositionWS = mul(position, World).xyz;
    result.NormalWS = normalize(mul(normal, WorldInverseTranspose));
    result.TexCoord = uv;
    result.Color = color;
    result.FogFactor = saturate(dot(position, FogVector));
    return result;
}

VSOutput VSBasic(VSInputNm input)
{
    return BuildOutput(input.Position, input.Normal, float2(0, 0), float4(1, 1, 1, 1));
}

VSOutput VSBasicVc(VSInputNmVc input)
{
    return BuildOutput(input.Position, input.Normal, float2(0, 0), input.Color);
}

VSOutput VSBasicTx(VSInputNmTx input)
{
    return BuildOutput(input.Position, input.Normal, input.TexCoord, float4(1, 1, 1, 1));
}

VSOutput VSBasicTxVc(VSInputNmTxVc input)
{
    return BuildOutput(input.Position, input.Normal, input.TexCoord, input.Color);
}

VSOutput BuildSkinnedOutput(float4 position, float3 normal, float2 uv, float4 color,
                            float4 weights, int4 indices)
{
    float4x3 skinning = Bones[indices.x] * weights.x;
    if (SkinningParams.x >= 2.0)
        skinning += Bones[indices.y] * weights.y;
    if (SkinningParams.x >= 4.0)
        skinning += Bones[indices.z] * weights.z + Bones[indices.w] * weights.w;
    float3 skinnedPosition = mul(position, skinning);
    float3 skinnedNormal = mul(normal, (float3x3)skinning);
    return BuildOutput(float4(skinnedPosition, 1), skinnedNormal, uv, color);
}

VSOutput VSSkinned(VSInputNmTxSkinned input)
{
    return BuildSkinnedOutput(input.Position, input.Normal, input.TexCoord, float4(1, 1, 1, 1),
                              input.BoneWeights, input.BoneIndices);
}

VSOutput VSSkinnedVc(VSInputNmTxSkinnedVc input)
{
    return BuildSkinnedOutput(input.Position, input.Normal, input.TexCoord, input.Color,
                              input.BoneWeights, input.BoneIndices);
}

float3 DirectionalDiffuse(float3 normal)
{
    return DirLight0DiffuseColor * max(dot(normal, -DirLight0Direction), 0) +
           DirLight1DiffuseColor * max(dot(normal, -DirLight1Direction), 0) +
           DirLight2DiffuseColor * max(dot(normal, -DirLight2Direction), 0);
}

float3 DirectionalSpecular(float3 normal, float3 eye)
{
    float dot0 = dot(normal, -DirLight0Direction);
    float dot1 = dot(normal, -DirLight1Direction);
    float dot2 = dot(normal, -DirLight2Direction);
    float s0 = pow(max(dot(normalize(eye - DirLight0Direction), normal), 0) * step(0, dot0),
                   SpecularPower);
    float s1 = pow(max(dot(normalize(eye - DirLight1Direction), normal), 0) * step(0, dot1),
                   SpecularPower);
    float s2 = pow(max(dot(normalize(eye - DirLight2Direction), normal), 0) * step(0, dot2),
                   SpecularPower);
    return (s0 * DirLight0SpecularColor + s1 * DirLight1SpecularColor +
            s2 * DirLight2SpecularColor) * SpecularColor;
}

float3 PunctualLight(float3 worldPos, float3 normal)
{
#if defined(CNA_FNA_DIRECTIONAL_ONLY) && CNA_FNA_DIRECTIONAL_ONLY
    return float3(0, 0, 0);
#else
    if (PunctualDirectionKind.w < 0.5)
        return float3(0, 0, 0);
    float3 offset = PunctualPositionRange.xyz - worldPos;
    float distanceToLight = length(offset);
    if (distanceToLight > PunctualPositionRange.w || distanceToLight < 0.00001)
        return float3(0, 0, 0);
    float3 toLight = offset / distanceToLight;
    float t = distanceToLight / PunctualPositionRange.w;
    float window = saturate(1 - t * t * t * t);
    float attenuation = window * window / (1 + distanceToLight * distanceToLight);
    if (PunctualDirectionKind.w > 1.5)
    {
        float cosAngle = dot(normalize(PunctualDirectionKind.xyz), -toLight);
        float cone = saturate((cosAngle - PunctualCone.y) /
                              max(PunctualCone.x - PunctualCone.y, 0.0001));
        attenuation *= cone * cone;
    }
    return PunctualDiffuse * max(dot(normal, toLight), 0) * attenuation *
           CnaPunctualShadow(worldPos, toLight, distanceToLight);
#endif
}

float4 Shade(VSOutput input, float4 texel)
{
    float3 normal = normalize(input.NormalWS);
    float3 eye = normalize(EyePosition - input.PositionWS);
    float shadow = CnaShadowFactor(input.PositionWS);
    float3 lit = (DirectionalDiffuse(normal) * shadow +
                  PunctualLight(input.PositionWS, normal)) * DiffuseColor.rgb + EmissiveColor;
#if defined(CNA_FNA_DIRECTIONAL_ONLY) && CNA_FNA_DIRECTIONAL_ONLY && \
    !(defined(CNA_FNA_DIRECTIONAL_VERTEX_COLOR_ONLY) && CNA_FNA_DIRECTIONAL_VERTEX_COLOR_ONLY)
    float4 color = texel * float4(lit, DiffuseColor.a);
#else
    float4 color = texel * float4(lit, DiffuseColor.a) * input.Color;
#endif
    color.rgb *= CnaCascadeDebugTint(input.PositionWS);
    color.rgb += DirectionalSpecular(normal, eye) * shadow * color.a;
    color.rgb = lerp(color.rgb, FogColor * color.a, input.FogFactor);
    return color;
}

float4 PSBasic(VSOutput input) : COLOR0
{
    return Shade(input, float4(1, 1, 1, 1));
}

float4 PSBasicTx(VSOutput input) : COLOR0
{
    return Shade(input, tex2D(TextureSampler, input.TexCoord));
}

technique CnaPunctual
{
    pass Basic
    {
        VertexShader = compile vs_3_0 VSBasic();
        PixelShader = compile ps_3_0 PSBasic();
    }
    pass BasicVc
    {
        VertexShader = compile vs_3_0 VSBasicVc();
        PixelShader = compile ps_3_0 PSBasic();
    }
    pass BasicTx
    {
        VertexShader = compile vs_3_0 VSBasicTx();
        PixelShader = compile ps_3_0 PSBasicTx();
    }
    pass BasicTxVc
    {
        VertexShader = compile vs_3_0 VSBasicTxVc();
        PixelShader = compile ps_3_0 PSBasicTx();
    }
    pass Skinned
    {
        VertexShader = compile vs_3_0 VSSkinned();
        PixelShader = compile ps_3_0 PSBasicTx();
    }
    pass SkinnedVc
    {
        VertexShader = compile vs_3_0 VSSkinnedVc();
        PixelShader = compile ps_3_0 PSBasicTx();
    }
}
