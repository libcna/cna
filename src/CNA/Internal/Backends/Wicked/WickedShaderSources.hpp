// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace CNA::Internal::Backends::Wicked
{
    /**
     * @brief NOXNA. The HLSL source of every built-in shader this backend compiles.
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
     *     D3D-shaped (Y down in window space, Z in [0,1]) on both device backends.
     *   - Vertex colours arrive as `R8G8B8A8_UNORM`, matching XNA's `Color` byte order in memory.
     */
    NOXNA inline constexpr const char* kWickedShaderSource = R"HLSL(
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
};

ConstantBuffer<CnaConstants> cb : register(b0);

Texture2D<float4> texture0 : register(t0);
Texture2D<float4> texture1 : register(t1);
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
)HLSL";
}
