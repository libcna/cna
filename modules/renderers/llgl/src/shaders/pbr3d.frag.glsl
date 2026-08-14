// SPDX-License-Identifier: MS-PL
// PbrEffect fragment shader, Vulkan flavour (SPIR-V). See pbr3d.vert.glsl for the PbrParams
// uniform block this shares. PbrLight() is transliterated verbatim from the Vulkan renderer's own
// already-correct pbr3d.frag.glsl (the glTF 2.0 metallic-roughness BRDF: GGX distribution +
// Smith-Schlick-GGX visibility + Schlick Fresnel), not re-derived from first principles.

#version 450

layout(std140, binding = 1) uniform PbrParams
{
    mat4 mvpMatrix;
    mat4 worldMatrix;
    vec4 diffuseColor;
    vec4 ambientColorPad;      // xyz=ambient, w=decode base colour
    vec4 emissiveMetallic;
    vec4 roughnessWeightsPad;  // x=roughness, y=skin weights, z=normal scale, w=occlusion strength
    vec4 light0DirPad;         // xyz=direction, w=encode output
    vec4 light0DiffusePad;
    vec4 light1DirPad;
    vec4 light1DiffusePad;
    vec4 light2DirPad;
    vec4 light2DiffusePad;
    vec4 eyePositionWorldPad;  // xyz=eye position, w=decode emissive
    vec4 fogColor;
    vec4 fogVector;
    vec4 alphaTest;
    vec4 dielectricFresnel;    // xyz = dielectric F0, w = dielectric F90
    vec4 textureTransformRows[10];
};

layout(binding = 2) uniform texture2D colorMap;
layout(binding = 3) uniform sampler samplerState;
layout(binding = 4) uniform texture2D normalMap;
layout(binding = 5) uniform sampler normalMapSampler;
layout(binding = 6) uniform texture2D metallicRoughnessMap;
layout(binding = 7) uniform sampler metallicRoughnessMapSampler;
layout(binding = 8) uniform texture2D emissiveMap;
layout(binding = 9) uniform sampler emissiveMapSampler;
layout(binding = 10) uniform texture2D occlusionMap;
layout(binding = 11) uniform sampler occlusionMapSampler;

layout(location = 0) in vec2  vTexCoord;
layout(location = 1) in vec3  vNormal;
layout(location = 2) in vec3  vTangent;
layout(location = 3) in float vBitangentSign;
layout(location = 4) in vec3  vWorldPos;
layout(location = 5) in float vFogFactor;

layout(location = 0) out vec4 fragColor;

// A disabled/never-configured DirectionalLight can forward Direction=(0,0,0) (matches FNA's own
// zeroing) -- normalize() on a true zero vector is undefined and can poison the whole light sum.
vec3 safeNormalize(vec3 v)
{
    float len2 = dot(v, v);
    return len2 > 0.0 ? v * inversesqrt(len2) : vec3(0.0);
}

vec3 cnaSrgbToLinear(vec3 c)
{
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

vec3 cnaLinearToSrgb(vec3 c)
{
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

// GGX/Trowbridge-Reitz D, Smith-Schlick-GGX visibility (direct-lighting k=(roughness+1)^2/8), and
// Schlick Fresnel -- the glTF 2.0 spec's own reference BRDF (Appendix B.3.2/B.3.3/B.3.4).
vec3 PbrLight(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, vec3 F0, vec3 F90, float roughness, float metallic)
{
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float a2 = pow(roughness, 4.0);
    float dTerm = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    float D = a2 / (3.14159265 * dTerm * dTerm + 1e-7);
    float k = (roughness + 1.0); k = k * k / 8.0;
    float G = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
    vec3 F = F0 + (F90 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    vec3 diffuseTerm = albedo * (1.0 - metallic);
    vec3 kd = vec3(1.0) - F;
    return (kd * diffuseTerm / 3.14159265 + specular) * lightColor * NdotL;
}

vec2 cnaPbrTransformUV(vec2 uv, int slot)
{
    vec3 value = vec3(uv, 1.0);
    return vec2(dot(value, textureTransformRows[slot * 2].xyz),
                dot(value, textureTransformRows[slot * 2 + 1].xyz));
}

void main()
{
    vec4 baseColorTex = texture(sampler2D(colorMap, samplerState), cnaPbrTransformUV(vTexCoord, 0));
    // Base colour factor and alpha are kept independent (not premultiplied) -- the PBR BRDF's
    // albedo and alpha are separate quantities per glTF's own baseColorFactor convention, unlike
    // most other CNA stock effects' DiffuseColor.
    vec3 baseColor = mix(baseColorTex.rgb, cnaSrgbToLinear(baseColorTex.rgb), ambientColorPad.w);
    vec3 albedo = baseColor * diffuseColor.rgb;
    float alpha = baseColorTex.a * diffuseColor.a;
    bool passesAlphaTest = (alphaTest.y > 0.0)
        ? (abs(alpha - alphaTest.x) < alphaTest.y)
        : (alpha < alphaTest.x);
    if ((passesAlphaTest ? alphaTest.z : alphaTest.w) < 0.0) discard;

    vec3 N = normalize(vNormal);
    vec3 T = normalize(vTangent - N * dot(N, vTangent));
    vec3 B = cross(N, T) * vBitangentSign;
    mat3 TBN = mat3(T, B, N);
    vec3 sampledNormal = texture(sampler2D(normalMap, normalMapSampler), cnaPbrTransformUV(vTexCoord, 1)).rgb * 2.0 - 1.0;
    sampledNormal.xy *= roughnessWeightsPad.z;
    vec3 finalNormal = normalize(TBN * sampledNormal);

    // glTF packing: G=roughness, B=metallic. A 1x1 white default (both channels 1.0) leaves the
    // constant Metallic/RoughnessFactor unperturbed when no map is bound.
    vec4 mr = texture(sampler2D(metallicRoughnessMap, metallicRoughnessMapSampler), cnaPbrTransformUV(vTexCoord, 2));
    float roughness = clamp(mr.g * roughnessWeightsPad.x, 0.045, 1.0);
    float metallic  = clamp(mr.b * emissiveMetallic.w, 0.0, 1.0);

    vec3 V = safeNormalize(eyePositionWorldPad.xyz - vWorldPos);
    vec3 F0 = mix(dielectricFresnel.xyz, albedo, metallic);
    vec3 F90 = mix(vec3(dielectricFresnel.w), vec3(1.0), metallic);

    // Direction fields point FROM the light, so negate for the "surface to light" vector PbrLight
    // expects.
    vec3 Lo = vec3(0.0);
    Lo += PbrLight(finalNormal, V, safeNormalize(-light0DirPad.xyz), light0DiffusePad.xyz, albedo, F0, F90, roughness, metallic);
    Lo += PbrLight(finalNormal, V, safeNormalize(-light1DirPad.xyz), light1DiffusePad.xyz, albedo, F0, F90, roughness, metallic);
    Lo += PbrLight(finalNormal, V, safeNormalize(-light2DirPad.xyz), light2DiffusePad.xyz, albedo, F0, F90, roughness, metallic);

    // R channel, 1x1 white default = fully lit (no darkening) when no occlusion map is bound.
    float occlusionSample = texture(sampler2D(occlusionMap, occlusionMapSampler), cnaPbrTransformUV(vTexCoord, 4)).r;
    float occlusion = 1.0 + roughnessWeightsPad.w * (occlusionSample - 1.0);
    vec3 ambient = ambientColorPad.xyz * albedo * occlusion;
    vec3 emissiveSample = texture(sampler2D(emissiveMap, emissiveMapSampler), cnaPbrTransformUV(vTexCoord, 3)).rgb;
    emissiveSample = mix(emissiveSample, cnaSrgbToLinear(emissiveSample), eyePositionWorldPad.w);
    vec3 emissive = emissiveMetallic.xyz * emissiveSample;

    vec3 rgb = ambient + Lo + emissive;
    // vFogFactor is "how much fog" (0 = none, 1 = full), matching this renderer's own convention
    // (see lit_textured3d.frag.glsl) -- NOT the plain Vulkan renderer's opposite "how much of the
    // original colour to keep" convention.
    vec3 fogLinear = mix(fogColor.rgb, cnaSrgbToLinear(fogColor.rgb), light0DirPad.w);
    rgb = mix(rgb, fogLinear, vFogFactor);
    rgb = mix(rgb, cnaLinearToSrgb(rgb), light0DirPad.w);

    fragColor = vec4(rgb, alpha);
}
