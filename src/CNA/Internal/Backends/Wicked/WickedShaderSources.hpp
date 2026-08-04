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
    float4 worldIT0;        // columns of the world inverse-transpose (normal matrix)
    float4 worldIT1;
    float4 worldIT2;
    float4 worldIT3;
    float4 envMapParams;    // x = amount, y = fresnel enabled, z = fresnel factor
    float4 envMapSpecular;  // rgb = EnvironmentMapEffect.EnvironmentMapSpecular
};

ConstantBuffer<CnaConstants> cb : register(b0);

Texture2D<float4> texture0 : register(t0);
Texture2D<float4> texture1 : register(t1);
TextureCube<float4> environmentMap : register(t2);
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
// Instanced variants.
//
// The per-instance stream is CNA's established 64-byte layout: a column-major float4x4 world
// matrix delivered as four float4 attributes at byte offsets 0/16/32/48, identical to the Vulkan
// backend's own instanced pipeline. `world * position` is therefore the column-vector product
// `sum(position[k] * column[k])`, which is exactly XNA's row-vector `position * matrix` once the
// matrix's raw bytes are read column-wise -- so an unmodified XNA Matrix in the instance buffer
// means the same thing on both backends.
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
