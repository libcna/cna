// SPDX-License-Identifier: MS-PL
// Shared Shader Model 3 shadow lookup used by CNA's DirectX 9 stock-extension and PBR shaders.
// The layout mirrors the renderer-neutral GpuDrawParams contract and the EasyGL/WebGPU paths:
// Point/full layout: s7 is the directional map/cascade atlas and s8 the point cube.
// Spot layout: s7 is the spot map and s8 the directional map/cascade atlas.

#if CNA_D3D9_FNA_SHADOWS
sampler2D   CnaShadowMap       : register(s7);
#if !CNA_D3D9_FNA_DIRECTIONAL_ONLY
samplerCUBE CnaPunctualCube    : register(s8);
sampler2D   CnaPunctualMap     : register(s9);
#endif
#elif CNA_D3D9_PUNCTUAL_SHADOW_KIND == 2
sampler2D   CnaPunctualMap     : register(s7);
sampler2D   CnaShadowMap       : register(s8);
#else
sampler2D   CnaShadowMap       : register(s7);
samplerCUBE CnaPunctualCube   : register(s8);
#endif

float4x4 CnaLightViewProj       : register(c34);
float4x4 CnaCascadeMatrices[4]  : register(c38);
// c54-c57 cannot hold this matrix: the compiler proves its Z column dead, reclaims c56 for a
// shader literal (`def c56`), and a four-register API upload then corrupts that literal. Keeping
// the matrix beyond the compiler-owned c54-c71 literal range makes all four uploads independent.
float4x4 CnaPunctualViewProj    : register(c72);
float4 CnaShadowParams          : register(c58); // x=enabled, y=bias, z=PCF radius, w=cascade count
float4 CnaShadowTexelBlendDebug : register(c59); // xy=1/map size, z=blend width, w=debug tint
float4 CnaCascadeSplits         : register(c60);
float4 CnaCascadeViewZ          : register(c61);
float4 CnaPunctualShadowParams  : register(c62); // x=has map, y=bias, zw=1/spot-map size

#if defined(CNA_D3D9_DIRECTIONAL_SHADOW_MODE) && CNA_D3D9_DIRECTIONAL_SHADOW_MODE == 0
float CnaShadowFactor(float3 worldPos) { return 1.0; }
float3 CnaCascadeDebugTint(float3 worldPos) { return float3(1.0, 1.0, 1.0); }
#else
float CnaShadowTap(float3 uv, float2 uvMin, float2 uvMax)
{
    if (uv.z > 1.0)
        return 1.0;
    float lit = 0.0;
    float taps = 0.0;
    [loop]
    for (int y = -2; y <= 2; ++y)
    {
        [loop]
        for (int x = -2; x <= 2; ++x)
        {
            float ring = max(abs((float)x), abs((float)y));
            if (ring > CnaShadowParams.z + 0.5)
                continue;
            float2 at = clamp(
                uv.xy + float2((float)x, (float)y) * CnaShadowTexelBlendDebug.xy,
                uvMin, uvMax);
            float occluder = tex2D(CnaShadowMap, at).r;
            lit += (uv.z - CnaShadowParams.y <= occluder) ? 1.0 : 0.0;
            taps += 1.0;
        }
    }
    return lit / max(taps, 1.0);
}

float4x4 CnaCascadeMatrix(int index)
{
    float4x4 value = CnaCascadeMatrices[0];
    if (index == 1) value = CnaCascadeMatrices[1];
    if (index == 2) value = CnaCascadeMatrices[2];
    if (index == 3) value = CnaCascadeMatrices[3];
    return value;
}

float CnaCascadeSplit(int index)
{
    float value = CnaCascadeSplits.x;
    if (index == 1) value = CnaCascadeSplits.y;
    if (index == 2) value = CnaCascadeSplits.z;
    if (index == 3) value = CnaCascadeSplits.w;
    return value;
}

float CnaCascadeLookup(float3 worldPos, int index, float count)
{
    float4 atlas = mul(float4(worldPos, 1.0), CnaCascadeMatrix(index));
    float3 uv = atlas.xyz / atlas.w;
    float slice = 1.0 / count;
    float x0 = (float)index * slice;
    if (uv.x < x0 || uv.x > x0 + slice || uv.y < 0.0 || uv.y > 1.0)
        return 1.0;
    float2 uvMin = float2(x0 + CnaShadowTexelBlendDebug.x, CnaShadowTexelBlendDebug.y);
    float2 uvMax = float2(x0 + slice - CnaShadowTexelBlendDebug.x,
                          1.0 - CnaShadowTexelBlendDebug.y);
    return CnaShadowTap(uv, uvMin, uvMax);
}

int CnaSelectCascade(float viewDepth, float count)
{
    int chosen = (int)count - 1;
    [loop]
    for (int i = 0; i < 4; ++i)
    {
        if ((float)i >= count)
            break;
        if (viewDepth <= CnaCascadeSplit(i))
        {
            chosen = i;
            break;
        }
    }
    return chosen;
}

float CnaShadowFactor(float3 worldPos)
{
    if (CnaShadowParams.x < 0.5)
        return 1.0;
    if (CnaShadowParams.w < 0.5)
    {
        float4 lightSpace = mul(float4(worldPos, 1.0), CnaLightViewProj);
        float3 uv = lightSpace.xyz / lightSpace.w * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            return 1.0;
        return CnaShadowTap(uv, float2(0.0, 0.0), float2(1.0, 1.0));
    }

    float viewDepth = -dot(float4(worldPos, 1.0), CnaCascadeViewZ);
    int index = CnaSelectCascade(viewDepth, CnaShadowParams.w);
    float factor = CnaCascadeLookup(worldPos, index, CnaShadowParams.w);
    float split = CnaCascadeSplit(index);
    if (CnaShadowTexelBlendDebug.z > 0.0 &&
        (float)(index + 1) < CnaShadowParams.w &&
        viewDepth > split - CnaShadowTexelBlendDebug.z)
    {
        float t = saturate((viewDepth - (split - CnaShadowTexelBlendDebug.z)) /
                           CnaShadowTexelBlendDebug.z);
        factor = lerp(factor, CnaCascadeLookup(worldPos, index + 1, CnaShadowParams.w), t);
    }
    return factor;
}

float3 CnaCascadeDebugTint(float3 worldPos)
{
    if (CnaShadowTexelBlendDebug.w < 0.5 || CnaShadowParams.x < 0.5 ||
        CnaShadowParams.w < 0.5)
        return float3(1.0, 1.0, 1.0);
    float viewDepth = -dot(float4(worldPos, 1.0), CnaCascadeViewZ);
    int index = CnaSelectCascade(viewDepth, CnaShadowParams.w);
    if (index == 0) return float3(1.0, 0.6, 0.6);
    if (index == 1) return float3(0.6, 1.0, 0.6);
    if (index == 2) return float3(0.6, 0.6, 1.0);
    return float3(1.0, 1.0, 0.6);
}
#endif

float CnaPunctualShadow(float3 worldPos, float3 toLight, float distanceToLight)
{
#if defined(CNA_D3D9_FNA_DIRECTIONAL_ONLY) && CNA_D3D9_FNA_DIRECTIONAL_ONLY
    return 1.0;
#else
    if (CnaPunctualShadowParams.x < 0.5)
        return 1.0;
    float here = saturate(distanceToLight / PunctualPositionRange.w);
#if CNA_D3D9_FNA_SHADOWS
    if (PunctualDirectionKind.w > 1.5)
    {
        float4 clip = mul(float4(worldPos, 1.0), CnaPunctualViewProj);
        if (clip.w <= 0.0)
            return 1.0;
        float3 uv = clip.xyz / clip.w * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            return 1.0;
        float occluder = tex2D(CnaPunctualMap, uv.xy).r;
        return (here - CnaPunctualShadowParams.y <= occluder) ? 1.0 : 0.0;
    }
    float occluder = texCUBE(CnaPunctualCube, -toLight).r;
    return (here - CnaPunctualShadowParams.y <= occluder) ? 1.0 : 0.0;
#elif CNA_D3D9_PUNCTUAL_SHADOW_KIND == 2
    float4 clip = mul(float4(worldPos, 1.0), CnaPunctualViewProj);
    if (clip.w <= 0.0)
        return 1.0;
    float3 uv = clip.xyz / clip.w * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return 1.0;
    // The already-heavy PBR shader exceeds the guaranteed ps_3_0 dynamic-instruction budget
    // when the portable 3x3 loop is added. A hard spot shadow is the same bounded compromise as
    // the point-cube path below; directional shadows retain their configurable 1/9/25-tap PCF.
    float occluder = tex2D(CnaPunctualMap, uv.xy).r;
    return (here - CnaPunctualShadowParams.y <= occluder) ? 1.0 : 0.0;
#else
    float occluder = texCUBE(CnaPunctualCube, -toLight).r;
    return (here - CnaPunctualShadowParams.y <= occluder) ? 1.0 : 0.0;
#endif
#endif
}
