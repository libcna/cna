// Directional, cascaded, point and spot shadow reception for stock D3D pixel shaders.
// The constant-buffer layout matches the renderer-neutral GpuDrawParams shadow group.

Texture2D<float4> cnaShadowMap : register(t7);
TextureCube<float4> cnaPunctualCube : register(t8);
Texture2D<float4> cnaPunctualMap : register(t9);
SamplerState cnaShadowSampler : register(s7);
SamplerState cnaPunctualCubeSampler : register(s8);
SamplerState cnaPunctualMapSampler : register(s9);

cbuffer CnaShadowParams : register(b3)
{
    row_major float4x4 cnaLightViewProj;
    row_major float4x4 cnaCascadeMatrices[4];
    row_major float4x4 cnaPunctualViewProj;
    float4 cnaDirectional;
    float4 cnaShadowTexelBlendDebug;
    float4 cnaCascadeSplits;
    float4 cnaCascadeViewZ;
    float4 cnaPunctualPositionRange;
    float4 cnaPunctualDirectionKind;
    float4 cnaPunctualDiffuseHasShadow;
    float4 cnaPunctualConeBiasTexelX;
    float4 cnaPunctualTexelY;
};

float CnaShadowTap(float3 uv, float2 uvMin, float2 uvMax)
{
    if (uv.z > 1.0f) return 1.0f;
    float lit = 0.0f;
    float taps = 0.0f;
    [loop]
    for (int y = -2; y <= 2; ++y)
    {
        [loop]
        for (int x = -2; x <= 2; ++x)
        {
            const float ring = max(abs(float(x)), abs(float(y)));
            if (ring > cnaDirectional.z + 0.5f) continue;
            const float2 at = clamp(
                uv.xy + float2(float(x), float(y)) * cnaShadowTexelBlendDebug.xy,
                uvMin, uvMax);
            const float occluder = cnaShadowMap.SampleLevel(
                cnaShadowSampler, float2(at.x, 1.0f - at.y), 0.0f).r;
            lit += uv.z - cnaDirectional.y <= occluder ? 1.0f : 0.0f;
            taps += 1.0f;
        }
    }
    return lit / max(taps, 1.0f);
}

row_major float4x4 CnaCascadeMatrix(int index)
{
    row_major float4x4 result = cnaCascadeMatrices[0];
    if (index == 1) result = cnaCascadeMatrices[1];
    if (index == 2) result = cnaCascadeMatrices[2];
    if (index == 3) result = cnaCascadeMatrices[3];
    return result;
}

float CnaCascadeSplit(int index)
{
    float split = cnaCascadeSplits.x;
    if (index == 1) split = cnaCascadeSplits.y;
    if (index == 2) split = cnaCascadeSplits.z;
    if (index == 3) split = cnaCascadeSplits.w;
    return split;
}

float CnaCascadeLookup(float3 worldPos, int index, float count)
{
    const float4 atlas = mul(float4(worldPos, 1.0f), CnaCascadeMatrix(index));
    const float3 uv = atlas.xyz / atlas.w;
    const float slice = 1.0f / count;
    const float x0 = float(index) * slice;
    if (uv.x < x0 || uv.x > x0 + slice || uv.y < 0.0f || uv.y > 1.0f)
        return 1.0f;
    const float2 uvMin = float2(
        x0 + cnaShadowTexelBlendDebug.x, cnaShadowTexelBlendDebug.y);
    const float2 uvMax = float2(
        x0 + slice - cnaShadowTexelBlendDebug.x,
        1.0f - cnaShadowTexelBlendDebug.y);
    return CnaShadowTap(uv, uvMin, uvMax);
}

int CnaSelectCascade(float viewDepth, float count)
{
    int chosen = int(count) - 1;
    for (int i = 0; i < 4; ++i)
    {
        if (float(i) >= count) break;
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
    if (cnaDirectional.x < 0.5f) return 1.0f;
    if (cnaDirectional.w < 0.5f)
    {
        const float4 lightSpace = mul(float4(worldPos, 1.0f), cnaLightViewProj);
        const float3 uv = lightSpace.xyz / lightSpace.w * 0.5f + 0.5f;
        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
            return 1.0f;
        return CnaShadowTap(uv, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
    }
    const float viewDepth = -dot(float4(worldPos, 1.0f), cnaCascadeViewZ);
    const int index = CnaSelectCascade(viewDepth, cnaDirectional.w);
    float factor = CnaCascadeLookup(worldPos, index, cnaDirectional.w);
    const float split = CnaCascadeSplit(index);
    if (cnaShadowTexelBlendDebug.z > 0.0f &&
        float(index + 1) < cnaDirectional.w &&
        viewDepth > split - cnaShadowTexelBlendDebug.z)
    {
        const float t = clamp(
            (viewDepth - (split - cnaShadowTexelBlendDebug.z)) /
                cnaShadowTexelBlendDebug.z,
            0.0f, 1.0f);
        factor = lerp(
            factor,
            CnaCascadeLookup(worldPos, index + 1, cnaDirectional.w), t);
    }
    return factor;
}

float3 CnaCascadeDebugTint(float3 worldPos)
{
    if (cnaShadowTexelBlendDebug.w < 0.5f ||
        cnaDirectional.x < 0.5f || cnaDirectional.w < 0.5f)
        return float3(1.0f, 1.0f, 1.0f);
    const float viewDepth = -dot(float4(worldPos, 1.0f), cnaCascadeViewZ);
    const int index = CnaSelectCascade(viewDepth, cnaDirectional.w);
    if (index == 0) return float3(1.0f, 0.6f, 0.6f);
    if (index == 1) return float3(0.6f, 1.0f, 0.6f);
    if (index == 2) return float3(0.6f, 0.6f, 1.0f);
    return float3(1.0f, 1.0f, 0.6f);
}

float CnaPunctualShadow(float3 worldPos, float3 toLight, float distanceToLight)
{
    if (cnaPunctualDiffuseHasShadow.w < 0.5f) return 1.0f;
    const float here = clamp(
        distanceToLight / cnaPunctualPositionRange.w, 0.0f, 1.0f);
    if (cnaPunctualDirectionKind.w < 1.5f)
    {
        const float occluder = cnaPunctualCube.SampleLevel(
            cnaPunctualCubeSampler, -toLight, 0.0f).r;
        return here - cnaPunctualConeBiasTexelX.z <= occluder ? 1.0f : 0.0f;
    }
    const float4 clip = mul(float4(worldPos, 1.0f), cnaPunctualViewProj);
    if (clip.w <= 0.0f) return 1.0f;
    const float3 uv = clip.xyz / clip.w * 0.5f + 0.5f;
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
        return 1.0f;
    float lit = 0.0f;
    const float2 texel = float2(
        cnaPunctualConeBiasTexelX.w, cnaPunctualTexelY.x);
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 at = clamp(
                uv.xy + float2(float(x), float(y)) * texel,
                float2(0.0f, 0.0f), float2(1.0f, 1.0f));
            const float occluder = cnaPunctualMap.SampleLevel(
                cnaPunctualMapSampler, at, 0.0f).r;
            lit += here - cnaPunctualConeBiasTexelX.z <= occluder ? 1.0f : 0.0f;
        }
    }
    return lit / 9.0f;
}

float3 CnaPunctualLight(float3 worldPos, float3 normal)
{
    if (cnaPunctualDirectionKind.w < 0.5f)
        return float3(0.0f, 0.0f, 0.0f);
    const float3 offset = cnaPunctualPositionRange.xyz - worldPos;
    const float distanceToLight = length(offset);
    if (distanceToLight > cnaPunctualPositionRange.w || distanceToLight < 1e-5f)
        return float3(0.0f, 0.0f, 0.0f);
    const float3 toLight = offset / distanceToLight;
    const float t = distanceToLight / cnaPunctualPositionRange.w;
    const float window = clamp(1.0f - t * t * t * t, 0.0f, 1.0f);
    float attenuation = window * window / (1.0f + distanceToLight * distanceToLight);
    if (cnaPunctualDirectionKind.w > 1.5f)
    {
        const float cosAngle = dot(normalize(cnaPunctualDirectionKind.xyz), -toLight);
        const float cone = clamp(
            (cosAngle - cnaPunctualConeBiasTexelX.y) /
                max(cnaPunctualConeBiasTexelX.x - cnaPunctualConeBiasTexelX.y, 1e-4f),
            0.0f, 1.0f);
        attenuation *= cone * cone;
    }
    const float ndotl = max(dot(normal, toLight), 0.0f);
    return cnaPunctualDiffuseHasShadow.xyz * ndotl * attenuation *
           CnaPunctualShadow(worldPos, toLight, distanceToLight);
}
