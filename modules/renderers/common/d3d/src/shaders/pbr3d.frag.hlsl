// Shader Model 5.0 (ps_5_0). Physically-based (metallic-roughness) unskinned pipeline -- HLSL
// port of EasyGLRenderer::EnsurePbrProgram()'s fragment stage (plan_cnj.md CNB-58,
// PbrEffect): GGX/Trowbridge-Reitz normal distribution, Smith-Schlick-GGX visibility
// (direct-lighting k=(roughness+1)^2/8), Schlick Fresnel -- glTF 2.0's own reference BRDF
// (Appendix B.3.2/B.3.3/B.3.4), driven by the same 3-DirectionalLight + AmbientLightColor
// convention every other CNA stock effect already uses. Normal mapping via a per-pixel TBN basis
// built from the vertex tangent (re-orthogonalized against the interpolated normal) and glTF's
// own bitangent-handedness-sign convention.
// glTF MASK coverage belongs to this PBR program, not to the separate AlphaTestEffect path: that
// path cannot represent the stride-48 tangent frame and would discard the PBR material model.

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
    float4 AmbientMetallic;    // xyz = AmbientColor, w = MetallicFactor
    float4 EmissiveRoughness;  // xyz = EmissiveColor, w = RoughnessFactor
    float4 AlphaTest;           // reference, tolerance, pass weight, fail weight
};

cbuffer PbrLights : register(b1)
{
    float4 EyePosWeights;   // xyz = EyePosition, w = WeightsPerVertex (unused here, unskinned)
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

// GGX/Trowbridge-Reitz D, Smith-Schlick-GGX visibility, Schlick Fresnel -- the glTF 2.0 spec's own
// reference BRDF, ported term-for-term from EasyGLRenderer::EnsurePbrProgram()'s PbrLight().
float3 PbrLight(float3 N, float3 V, float3 L, float3 lightColor, float3 albedo, float3 F0,
                float roughness, float metallic)
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
    float3 F = F0 + (float3(1.0, 1.0, 1.0) - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    float3 diffuseColor = albedo * (1.0 - metallic);
    float3 kd = float3(1.0, 1.0, 1.0) - F;
    return (kd * diffuseColor / kPi + specular) * lightColor * NdotL;
}

float4 main(PSInput input) : SV_Target
{
    float4 baseColorTex = uTexture.Sample(uTextureSampler, input.UV);
    float3 albedo = baseColorTex.rgb * DiffuseColor.rgb;
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
    float3 finalNormal = normalize(mul(sampledNormal, TBN));

    float4 mr = uMetallicRoughnessMap.Sample(uMetallicRoughnessSampler, input.UV);
    float roughness = clamp(mr.g * EmissiveRoughness.w, 0.045, 1.0);
    float metallic  = clamp(mr.b * AmbientMetallic.w, 0.0, 1.0);

    float3 V = normalize(EyePosWeights.xyz - input.WorldPos);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float3 Lo = float3(0.0, 0.0, 0.0);
    Lo += PbrLight(finalNormal, V, normalize(-Light0DirPad.xyz), Light0DiffusePad.xyz, albedo, F0, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-Light1DirPad.xyz), Light1DiffusePad.xyz, albedo, F0, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-Light2DirPad.xyz), Light2DiffusePad.xyz, albedo, F0, roughness, metallic);

    float occlusion = uOcclusionMap.Sample(uOcclusionMapSampler, input.UV).r;
    float3 ambient = AmbientMetallic.xyz * albedo * occlusion;
    float3 emissive = EmissiveRoughness.xyz * uEmissiveMap.Sample(uEmissiveMapSampler, input.UV).rgb;

    float4 outColor = float4(ambient + Lo + emissive, alpha);
    outColor.rgb = lerp(FogColor.xyz, outColor.rgb, input.FogFactor);
    return outColor;
}
