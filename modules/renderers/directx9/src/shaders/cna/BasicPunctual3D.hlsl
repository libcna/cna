// SPDX-License-Identifier: MS-PL
//
// CNA's DirectX 9 BasicEffect/SkinnedEffect extension path. Microsoft's original XNA 4.0 stock
// bytecode has no PunctualLightEXT input, so draws with an active point/spot source use this SM3
// pair. Draws without the extension continue through the untouched stock bytecode and remain
// eligible for exact XNA-oracle comparison.

sampler2D Texture : register(s0);

float4x4 WorldViewProj          : register(c0);
float4x4 World                  : register(c4);
float3x3 WorldInverseTranspose  : register(c8);
float4 FogVector                : register(c11);
float4x3 Bones[72]              : register(c12);
float4 SkinningParams           : register(c228); // x=weights per vertex

float4 DiffuseColor             : register(c0);
float4 EmissiveColor            : register(c1);
float4 SpecularColorPower       : register(c2);  // xyz=color, w=power
float4 EyePosition              : register(c3);
float4 Light0Direction          : register(c4);
float4 Light0Diffuse            : register(c5);
float4 Light0Specular           : register(c6);
float4 Light1Direction          : register(c7);
float4 Light1Diffuse            : register(c8);
float4 Light1Specular           : register(c9);
float4 Light2Direction          : register(c10);
float4 Light2Diffuse            : register(c11);
float4 Light2Specular           : register(c12);
float4 PunctualPositionRange    : register(c13); // xyz=world position, w=range
float4 PunctualDirectionKind    : register(c14); // xyz=spot direction, w=1 point / 2 spot
float4 PunctualDiffuse          : register(c15);
float4 PunctualCone             : register(c16); // x=cos(inner), y=cos(outer)
float4 FogColor                 : register(c17);

#if CNA_D3D9_SHADOWS
#include "ShadowSampling3D.fxh"
#else
float CnaShadowFactor(float3 worldPos) { return 1.0; }
float3 CnaCascadeDebugTint(float3 worldPos) { return float3(1.0, 1.0, 1.0); }
float CnaPunctualShadow(float3 worldPos, float3 toLight, float distanceToLight) { return 1.0; }
#endif

struct VSInputNm
{
    float4 Position : POSITION0;
    float3 Normal   : NORMAL0;
};

struct VSInputNmVc
{
    float4 Position : POSITION0;
    float3 Normal   : NORMAL0;
    float4 Color    : COLOR0;
};

struct VSInputNmTx
{
    float4 Position : POSITION0;
    float3 Normal   : NORMAL0;
    float2 TexCoord : TEXCOORD0;
};

struct VSInputNmTxVc
{
    float4 Position : POSITION0;
    float3 Normal   : NORMAL0;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSInputNmTxSkinned
{
    float4 Position    : POSITION0;
    float3 Normal      : NORMAL0;
    float2 TexCoord    : TEXCOORD0;
    float4 BoneWeights : BLENDWEIGHT0;
    int4 BoneIndices   : BLENDINDICES0;
};

struct VSInputNmTxSkinnedVc
{
    float4 Position    : POSITION0;
    float3 Normal      : NORMAL0;
    float2 TexCoord    : TEXCOORD0;
    float4 BoneWeights : BLENDWEIGHT0;
    int4 BoneIndices   : BLENDINDICES0;
    float4 Color       : COLOR0;
};

struct VSOutput
{
    float4 PositionPS : POSITION0;
    float3 PositionWS : TEXCOORD1;
    float3 NormalWS   : TEXCOORD2;
    float2 TexCoord   : TEXCOORD0;
    float4 Color      : COLOR0;
    float  FogFactor  : TEXCOORD3;
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

VSOutput VSBasicPunctual(VSInputNm vin)
{
    return BuildOutput(vin.Position, vin.Normal, float2(0.0, 0.0), float4(1.0, 1.0, 1.0, 1.0));
}

VSOutput VSBasicPunctualVc(VSInputNmVc vin)
{
    return BuildOutput(vin.Position, vin.Normal, float2(0.0, 0.0), vin.Color);
}

VSOutput VSBasicPunctualTx(VSInputNmTx vin)
{
    return BuildOutput(vin.Position, vin.Normal, vin.TexCoord, float4(1.0, 1.0, 1.0, 1.0));
}

VSOutput VSBasicPunctualTxVc(VSInputNmTxVc vin)
{
    return BuildOutput(vin.Position, vin.Normal, vin.TexCoord, vin.Color);
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
    return BuildOutput(float4(skinnedPosition, 1.0), skinnedNormal, uv, color);
}

VSOutput VSBasicPunctualSkinned(VSInputNmTxSkinned vin)
{
    return BuildSkinnedOutput(vin.Position, vin.Normal, vin.TexCoord,
                              float4(1.0, 1.0, 1.0, 1.0),
                              vin.BoneWeights, vin.BoneIndices);
}

VSOutput VSBasicPunctualSkinnedVc(VSInputNmTxSkinnedVc vin)
{
    return BuildSkinnedOutput(vin.Position, vin.Normal, vin.TexCoord, vin.Color,
                              vin.BoneWeights, vin.BoneIndices);
}

float3 DirectionalDiffuse(float3 normal)
{
    return Light0Diffuse.xyz * max(dot(normal, -Light0Direction.xyz), 0.0) +
           Light1Diffuse.xyz * max(dot(normal, -Light1Direction.xyz), 0.0) +
           Light2Diffuse.xyz * max(dot(normal, -Light2Direction.xyz), 0.0);
}

float3 DirectionalSpecular(float3 normal, float3 eye)
{
    float dot0 = dot(normal, -Light0Direction.xyz);
    float dot1 = dot(normal, -Light1Direction.xyz);
    float dot2 = dot(normal, -Light2Direction.xyz);
    float s0 = pow(max(dot(normalize(eye - Light0Direction.xyz), normal), 0.0) * step(0.0, dot0),
                   SpecularColorPower.w);
    float s1 = pow(max(dot(normalize(eye - Light1Direction.xyz), normal), 0.0) * step(0.0, dot1),
                   SpecularColorPower.w);
    float s2 = pow(max(dot(normalize(eye - Light2Direction.xyz), normal), 0.0) * step(0.0, dot2),
                   SpecularColorPower.w);
    return (s0 * Light0Specular.xyz + s1 * Light1Specular.xyz + s2 * Light2Specular.xyz) *
           SpecularColorPower.xyz;
}

float3 CnaPunctualLight(float3 worldPos, float3 normal)
{
    float3 offset = PunctualPositionRange.xyz - worldPos;
    float distanceToLight = length(offset);
    if (distanceToLight > PunctualPositionRange.w || distanceToLight < 1e-5)
        return float3(0.0, 0.0, 0.0);
    float3 toLight = offset / distanceToLight;
    float t = distanceToLight / PunctualPositionRange.w;
    float window = saturate(1.0 - t * t * t * t);
    float attenuation = window * window / (1.0 + distanceToLight * distanceToLight);
    if (PunctualDirectionKind.w > 1.5)
    {
        float cosAngle = dot(normalize(PunctualDirectionKind.xyz), -toLight);
        float cone = saturate((cosAngle - PunctualCone.y) /
                              max(PunctualCone.x - PunctualCone.y, 1e-4));
        attenuation *= cone * cone;
    }
    return PunctualDiffuse.xyz * max(dot(normal, toLight), 0.0) * attenuation *
           CnaPunctualShadow(worldPos, toLight, distanceToLight);
}

float4 ShadeBasicPunctual(VSOutput pin, float4 texel)
{
    float3 normal = normalize(pin.NormalWS);
    float3 eye = normalize(EyePosition.xyz - pin.PositionWS);
    float shadow = CnaShadowFactor(pin.PositionWS);
    float3 diffuseLight = DirectionalDiffuse(normal) * shadow +
                          CnaPunctualLight(pin.PositionWS, normal);
    float3 lit = diffuseLight * DiffuseColor.rgb + EmissiveColor.rgb;
    float4 color = texel * float4(lit, DiffuseColor.a) * pin.Color;
    color.rgb *= CnaCascadeDebugTint(pin.PositionWS);
    color.rgb += DirectionalSpecular(normal, eye) * shadow * color.a;
    color.rgb = lerp(color.rgb, FogColor.rgb * color.a, pin.FogFactor);
    return color;
}

float4 PSBasicPunctual(VSOutput pin) : SV_Target0
{
    return ShadeBasicPunctual(pin, float4(1.0, 1.0, 1.0, 1.0));
}

float4 PSBasicPunctualTx(VSOutput pin) : SV_Target0
{
    return ShadeBasicPunctual(pin, tex2D(Texture, pin.TexCoord));
}
