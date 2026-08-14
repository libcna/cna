// Shader Model 5.0 (ps_5_0). PBR + skinning combo (SkinnedPbrEffect) -- identical BRDF math to
// pbr3d.frag.hlsl (see that file for the full GGX/Smith-Schlick-GGX/Schlick-Fresnel derivation
// notes); this pixel shader is not itself skinning-aware (skinning only affects the vertex
// stage's Position/Normal/Tangent/WorldPos, already resolved by the time PSInput arrives here).
// PbrLights lives at register(b2) here instead of (b1) -- BoneBlock (vertex-stage only) claims
// (b1), same "next free slot" precedent skinned3d.frag.hlsl's own FogParams register already set.

Texture2D    uTexture                  : register(t0);
SamplerState uTextureSampler           : register(s0);
Texture2D    uNormalMap                : register(t1);
SamplerState uNormalMapSampler         : register(s1);
Texture2D    uMetallicRoughnessMap     : register(t2);
SamplerState uMetallicRoughnessSampler : register(s2);
Texture2D    uEmissiveMap              : register(t3);
SamplerState uEmissiveMapSampler       : register(s3);
Texture2D    uOcclusionMap             : register(t4);
SamplerState uOcclusionMapSampler      : register(s4);

cbuffer PerDraw : register(b0)
{
    row_major float4x4 Mvp;
    row_major float4x4 World;
    float4 DiffuseColor;
    float4 AmbientMetallic;
    float4 EmissiveRoughness;
    float4 AlphaTest;
    // x = normal scale, y = occlusion strength, z = decode base, w = decode emissive
    float4 PbrMapScales;
    float4 DielectricFresnel; // xyz = dielectric F0, w = dielectric F90
};

cbuffer PbrLights : register(b2)
{
    float4 EyePosWeights;
    float4 Light0DirPad;
    float4 Light0DiffusePad;
    float4 Light1DirPad;
    float4 Light1DiffusePad;
    float4 Light2DirPad;
    float4 Light2DiffusePad;
    float4 FogColor;
    float4 FogVector;
};

struct PSInput
{
    float4 Position  : SV_Position;
    float3 Normal    : TEXCOORD0;
    float4 Tangent   : TEXCOORD1;
    float2 UV        : TEXCOORD2;
    float  FogFactor : TEXCOORD3;
    float3 WorldPos  : TEXCOORD4;
};

static const float kPi = 3.14159265;

float3 CnaSrgbToLinear(float3 color)
{
    float3 low = color / 12.92;
    float3 high = pow((color + 0.055) / 1.055, float3(2.4, 2.4, 2.4));
    return lerp(low, high, step(float3(0.04045, 0.04045, 0.04045), color));
}

float3 CnaLinearToSrgb(float3 color)
{
    float3 low = color * 12.92;
    float exponent = 1.0 / 2.4;
    float3 high = 1.055 * pow(max(color, 0.0), float3(exponent, exponent, exponent)) - 0.055;
    return lerp(low, high, step(float3(0.0031308, 0.0031308, 0.0031308), color));
}

float3 PbrLight(float3 N, float3 V, float3 L, float3 lightColor, float3 albedo, float3 F0,
                float3 F90, float roughness, float metallic)
{
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float a2 = pow(roughness, 4.0);
    float dTerm = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    float D = a2 / (kPi * dTerm * dTerm + 1e-7);
    float k = (roughness + 1.0); k = k * k / 8.0;
    float G = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
    float3 F = F0 + (F90 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    float3 diffuseColor = albedo * (1.0 - metallic);
    float3 kd = float3(1.0, 1.0, 1.0) - F;
    return (kd * diffuseColor / kPi + specular) * lightColor * NdotL;
}

float4 main(PSInput input) : SV_Target
{
    float4 baseColorTex = uTexture.Sample(uTextureSampler, input.UV);
    float3 baseColor = lerp(baseColorTex.rgb, CnaSrgbToLinear(baseColorTex.rgb), PbrMapScales.z);
    float3 albedo = baseColor * DiffuseColor.rgb;
    float alpha = baseColorTex.a * DiffuseColor.a;
    bool passesAlphaTest = (AlphaTest.y > 0.0)
        ? (abs(alpha - AlphaTest.x) < AlphaTest.y)
        : (alpha < AlphaTest.x);
    if ((passesAlphaTest ? AlphaTest.z : AlphaTest.w) < 0.0) discard;

    float3 N = normalize(input.Normal);
    float3 T = normalize(input.Tangent.xyz - N * dot(N, input.Tangent.xyz));
    float3 B = cross(N, T) * input.Tangent.w;
    float3x3 TBN = float3x3(T, B, N);
    float3 sampledNormal = uNormalMap.Sample(uNormalMapSampler, input.UV).rgb * 2.0 - 1.0;
    sampledNormal.xy *= PbrMapScales.x;
    float3 finalNormal = normalize(mul(sampledNormal, TBN));

    float4 mr = uMetallicRoughnessMap.Sample(uMetallicRoughnessSampler, input.UV);
    float roughness = clamp(mr.g * EmissiveRoughness.w, 0.045, 1.0);
    float metallic  = clamp(mr.b * AmbientMetallic.w, 0.0, 1.0);

    float3 V = normalize(EyePosWeights.xyz - input.WorldPos);
    float3 F0 = lerp(DielectricFresnel.xyz, albedo, metallic);
    float3 F90 = lerp(float3(DielectricFresnel.w, DielectricFresnel.w, DielectricFresnel.w),
                      float3(1.0, 1.0, 1.0), metallic);

    float3 Lo = float3(0.0, 0.0, 0.0);
    Lo += PbrLight(finalNormal, V, normalize(-Light0DirPad.xyz), Light0DiffusePad.xyz, albedo, F0, F90, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-Light1DirPad.xyz), Light1DiffusePad.xyz, albedo, F0, F90, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-Light2DirPad.xyz), Light2DiffusePad.xyz, albedo, F0, F90, roughness, metallic);

    float occlusion = uOcclusionMap.Sample(uOcclusionMapSampler, input.UV).r;
    occlusion = 1.0 + PbrMapScales.y * (occlusion - 1.0);
    float3 ambient = AmbientMetallic.xyz * albedo * occlusion;
    float3 emissiveSample = uEmissiveMap.Sample(uEmissiveMapSampler, input.UV).rgb;
    emissiveSample = lerp(emissiveSample, CnaSrgbToLinear(emissiveSample), PbrMapScales.w);
    float3 emissive = EmissiveRoughness.xyz * emissiveSample;

    float4 outColor = float4(ambient + Lo + emissive, alpha);
    float3 fogLinear = lerp(FogColor.xyz, CnaSrgbToLinear(FogColor.xyz), FogColor.w);
    outColor.rgb = lerp(fogLinear, outColor.rgb, input.FogFactor);
    outColor.rgb = lerp(outColor.rgb, CnaLinearToSrgb(outColor.rgb), FogColor.w);
    return outColor;
}
