// Shader Model 5.0 (ps_5_0). Physically-based (metallic-roughness) unskinned pipeline -- HLSL
// port of EasyGLRenderer::EnsurePbrProgram()'s fragment stage (plans/plan_cnj.md CNB-58,
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
Texture2D    uSpecularMap              : register(t5);
SamplerState uSpecularMapSampler       : register(s5);
Texture2D    uSpecularColorMap         : register(t6);
SamplerState uSpecularColorMapSampler  : register(s6);

cbuffer PerDraw : register(b0)
{
    row_major float4x4 Mvp;
    row_major float4x4 World;
    float4 DiffuseColor;
    float4 AmbientMetallic;    // xyz = AmbientColor, w = MetallicFactor
    float4 EmissiveRoughness;  // xyz = EmissiveColor, w = RoughnessFactor
    float4 AlphaTest;           // reference, tolerance, pass weight, fail weight
    // x = normal scale, y = occlusion strength, z = decode base, w = decode emissive
    float4 PbrMapScales;
    float4 DielectricFresnel; // xyz = dielectric F0, w = dielectric F90
    float4 TextureTransformRows[10]; // two affine UV rows per PBR map
    float4 TextureCoordinateSets; // x = seven-bit per-map TEXCOORD_1 selector mask
    float4 SpecularFresnelInputs; // xyz = unclamped F0, w = specular factor
    float4 SpecularMapFlags; // x = decode specular-colour sample from sRGB
    float4 SpecularTextureTransformRows[4]; // two affine rows per specular map
    // plans/plan_gltf.md GLTF-465: x = COLOR_0 multiplier enable (PbrEffect/SkinnedPbrEffect's
    // VertexColorEnabledEXT). Read only by the variants whose vertex record has a colour slot.
    float4 VertexColorFlags;
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
#ifdef CNA_PBR_DUAL_UV
    float2 UV1       : TEXCOORD5;
#endif
#ifdef CNA_PBR_VERTEX_COLOR
    float4 Color     : TEXCOORD6;
#endif
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

float2 CnaPbrTransformUv(float2 uv, int slot)
{
    float3 value = float3(uv, 1.0);
    return float2(dot(value, TextureTransformRows[slot * 2].xyz),
                  dot(value, TextureTransformRows[slot * 2 + 1].xyz));
}

float2 CnaPbrSpecularTransformUv(float2 uv, int slot)
{
    float3 value = float3(uv, 1.0);
    return float2(dot(value, SpecularTextureTransformRows[slot * 2].xyz),
                  dot(value, SpecularTextureTransformRows[slot * 2 + 1].xyz));
}

#ifdef CNA_PBR_DUAL_UV
float2 CnaPbrUv(float2 uv0, float2 uv1, int slot)
{
    int mask = int(TextureCoordinateSets.x + 0.5);
    return (mask & (1 << slot)) != 0 ? uv1 : uv0;
}
#define CNA_PBR_UV(slot) CnaPbrUv(input.UV, input.UV1, slot)
#else
#define CNA_PBR_UV(slot) input.UV
#endif

// GGX/Trowbridge-Reitz D, Smith-Schlick-GGX visibility, Schlick Fresnel -- the glTF 2.0 spec's own
// reference BRDF, ported term-for-term from EasyGLRenderer::EnsurePbrProgram()'s PbrLight().
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
    float4 baseColorTex = uTexture.Sample(uTextureSampler, CnaPbrTransformUv(CNA_PBR_UV(0), 0));
    float3 baseColor = lerp(baseColorTex.rgb, CnaSrgbToLinear(baseColorTex.rgb), PbrMapScales.z);
    float3 albedo = baseColor * DiffuseColor.rgb;
    float alpha = baseColorTex.a * DiffuseColor.a;
#ifdef CNA_PBR_VERTEX_COLOR
    // plans/plan_gltf.md GLTF-465: glTF 2.0 §3.9.2 -- COLOR_0 is an additional linear multiplier on the
    // base colour product, its alpha included. VertexColorFlags.x is the effect's own switch, so a
    // record that carries the slot without an authored colour keeps the opaque-white identity.
    if (VertexColorFlags.x > 0.5)
    {
        albedo *= input.Color.rgb;
        alpha  *= input.Color.a;
    }
#endif
    bool passesAlphaTest = (AlphaTest.y > 0.0)
        ? (abs(alpha - AlphaTest.x) < AlphaTest.y)
        : (alpha < AlphaTest.x);
    if ((passesAlphaTest ? AlphaTest.z : AlphaTest.w) < 0.0) discard;

    float3 N = normalize(input.Normal);
    float3 T = normalize(input.Tangent.xyz - N * dot(N, input.Tangent.xyz));
    float3 B = cross(N, T) * input.Tangent.w;
    float3x3 TBN = float3x3(T, B, N);
    float3 sampledNormal = uNormalMap.Sample(uNormalMapSampler, CnaPbrTransformUv(CNA_PBR_UV(1), 1)).rgb * 2.0 - 1.0;
    sampledNormal.xy *= PbrMapScales.x;
    float3 finalNormal = normalize(mul(sampledNormal, TBN));

    float4 mr = uMetallicRoughnessMap.Sample(uMetallicRoughnessSampler, CnaPbrTransformUv(CNA_PBR_UV(2), 2));
    float roughness = clamp(mr.g * EmissiveRoughness.w, 0.045, 1.0);
    float metallic  = clamp(mr.b * AmbientMetallic.w, 0.0, 1.0);

    float3 V = normalize(EyePosWeights.xyz - input.WorldPos);
    float specularWeight = SpecularFresnelInputs.w * uSpecularMap.Sample(
        uSpecularMapSampler, CnaPbrSpecularTransformUv(CNA_PBR_UV(5), 0)).a;
    float3 specularColorTex = uSpecularColorMap.Sample(
        uSpecularColorMapSampler, CnaPbrSpecularTransformUv(CNA_PBR_UV(6), 1)).rgb;
    specularColorTex = lerp(
        specularColorTex, CnaSrgbToLinear(specularColorTex), SpecularMapFlags.x);
    float3 dielectricF0 = min(SpecularFresnelInputs.xyz * specularColorTex, 1.0)
        * specularWeight;
    float3 F0 = lerp(dielectricF0, albedo, metallic);
    float3 F90 = lerp(float3(specularWeight, specularWeight, specularWeight),
                      float3(1.0, 1.0, 1.0), metallic);

    float3 Lo = float3(0.0, 0.0, 0.0);
    Lo += PbrLight(finalNormal, V, normalize(-Light0DirPad.xyz), Light0DiffusePad.xyz, albedo, F0, F90, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-Light1DirPad.xyz), Light1DiffusePad.xyz, albedo, F0, F90, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-Light2DirPad.xyz), Light2DiffusePad.xyz, albedo, F0, F90, roughness, metallic);

    float occlusion = uOcclusionMap.Sample(uOcclusionMapSampler, CnaPbrTransformUv(CNA_PBR_UV(4), 4)).r;
    occlusion = 1.0 + PbrMapScales.y * (occlusion - 1.0);
    float3 ambient = AmbientMetallic.xyz * albedo * occlusion;
    float3 emissiveSample = uEmissiveMap.Sample(uEmissiveMapSampler, CnaPbrTransformUv(CNA_PBR_UV(3), 3)).rgb;
    emissiveSample = lerp(emissiveSample, CnaSrgbToLinear(emissiveSample), PbrMapScales.w);
    float3 emissive = EmissiveRoughness.xyz * emissiveSample;

    float4 outColor = float4(ambient + Lo + emissive, alpha);
    float3 fogLinear = lerp(FogColor.xyz, CnaSrgbToLinear(FogColor.xyz), FogColor.w);
    outColor.rgb = lerp(fogLinear, outColor.rgb, input.FogFactor);
    outColor.rgb = lerp(outColor.rgb, CnaLinearToSrgb(outColor.rgb), FogColor.w);
    return outColor;
}
