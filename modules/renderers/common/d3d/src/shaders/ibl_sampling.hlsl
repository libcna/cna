// Split-sum image-based lighting for the PBR stock pixel shaders.

TextureCube<float4> cnaIblIrradiance : register(t10);
TextureCube<float4> cnaIblSpecular : register(t11);
Texture2D<float4> cnaIblBrdfLut : register(t12);
SamplerState cnaIblIrradianceSampler : register(s10);
SamplerState cnaIblSpecularSampler : register(s11);
SamplerState cnaIblBrdfSampler : register(s12);

cbuffer CnaIblParams : register(b4)
{
    float4 cnaIblParams; // enabled, prefiltered mip count, intensity, padding
};

float3 CnaIblAmbient(float3 N, float3 V, float3 albedo, float3 F0,
                     float roughness, float metallic, float occlusion)
{
    if (cnaIblParams.x < 0.5f) return float3(0.0f, 0.0f, 0.0f);
    const float NdotV = clamp(dot(N, V), 1e-4f, 1.0f);
    const float3 kS = F0 + (max(float3(1.0f - roughness, 1.0f - roughness,
                                       1.0f - roughness), F0) - F0) *
                           pow(1.0f - NdotV, 5.0f);
    const float3 kD = (float3(1.0f, 1.0f, 1.0f) - kS) * (1.0f - metallic);
    const float3 diffuse = cnaIblIrradiance.Sample(
        cnaIblIrradianceSampler, N).rgb * albedo * kD;
    const float3 R = reflect(-V, N);
    const float lod = roughness * max(cnaIblParams.y - 1.0f, 0.0f);
    const float3 prefiltered = cnaIblSpecular.SampleLevel(
        cnaIblSpecularSampler, R, lod).rgb;
    const float2 ab = cnaIblBrdfLut.Sample(
        cnaIblBrdfSampler, float2(NdotV, roughness)).rg;
    const float3 specular = prefiltered * (kS * ab.x + ab.y);
    return (diffuse + specular) * cnaIblParams.z * occlusion;
}
