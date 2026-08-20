// SPDX-License-Identifier: MS-PL
// PbrEffect fragment shader, OpenGL flavour. See pbr3d.frag.glsl for the Vulkan flavour and the
// formula this mirrors (transliterated from the Vulkan renderer's own pbr3d.frag.glsl).

#version 450 core

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
    vec4 dielectricFresnel;    // xyz = unclamped dielectric F0, w = specular factor
    vec4 textureTransformRows[10];
    vec4 specularState;        // x = TEXCOORD_1 selector mask, y = decode specular colour, z = VertexColorEnabled (GLTF-465)
    vec4 specularTextureTransformRows[4];
};

layout(binding = 2) uniform sampler2D colorMap;
layout(binding = 4) uniform sampler2D normalMap;
layout(binding = 6) uniform sampler2D metallicRoughnessMap;
layout(binding = 8) uniform sampler2D emissiveMap;
layout(binding = 10) uniform sampler2D occlusionMap;
layout(binding = 12) uniform sampler2D specularMap;
layout(binding = 14) uniform sampler2D specularColorMap;

layout(location = 0) in vec2  vTexCoord;
layout(location = 1) in vec3  vNormal;
layout(location = 2) in vec3  vTangent;
layout(location = 3) in float vBitangentSign;
layout(location = 4) in vec3  vWorldPos;
layout(location = 5) in float vFogFactor;
layout(location = 6) in vec2  vTexCoord1;
layout(location = 7) in vec4  vColor;

layout(location = 0) out vec4 fragColor;

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

vec2 cnaPbrSpecularTransformUV(vec2 uv, int slot)
{
    vec3 value = vec3(uv, 1.0);
    return vec2(dot(value, specularTextureTransformRows[slot * 2].xyz),
                dot(value, specularTextureTransformRows[slot * 2 + 1].xyz));
}

vec2 cnaPbrUv(int slot)
{
    float selector = mod(floor(specularState.x / exp2(float(slot))), 2.0);
    return mix(vTexCoord, vTexCoord1, selector);
}

void main()
{
    vec4 baseColorTex = texture(colorMap, cnaPbrTransformUV(cnaPbrUv(0), 0));
    vec3 baseColor = mix(baseColorTex.rgb, cnaSrgbToLinear(baseColorTex.rgb), ambientColorPad.w);
    // plans/plan_gltf.md GLTF-465: COLOR_0 multiplies the base colour product, ALPHA INCLUDED -- the alpha
    // half is where a BLEND-mode vertex-coloured primitive's transparency comes from. specularState.z
    // is the effect's own VertexColorEnabledEXT, and the variants without a colour attribute pass
    // opaque white anyway, so this is inert for them either way.
    vec4 cnaVertexColor = (specularState.z > 0.5) ? vColor : vec4(1.0);
    vec3 albedo = baseColor * diffuseColor.rgb * cnaVertexColor.rgb;
    float alpha = baseColorTex.a * diffuseColor.a * cnaVertexColor.a;
    bool passesAlphaTest = (alphaTest.y > 0.0)
        ? (abs(alpha - alphaTest.x) < alphaTest.y)
        : (alpha < alphaTest.x);
    if ((passesAlphaTest ? alphaTest.z : alphaTest.w) < 0.0) discard;

    vec3 N = normalize(vNormal);
    vec3 T = normalize(vTangent - N * dot(N, vTangent));
    vec3 B = cross(N, T) * vBitangentSign;
    mat3 TBN = mat3(T, B, N);
    vec3 sampledNormal = texture(normalMap, cnaPbrTransformUV(cnaPbrUv(1), 1)).rgb * 2.0 - 1.0;
    sampledNormal.xy *= roughnessWeightsPad.z;
    vec3 finalNormal = normalize(TBN * sampledNormal);

    vec4 mr = texture(metallicRoughnessMap, cnaPbrTransformUV(cnaPbrUv(2), 2));
    float roughness = clamp(mr.g * roughnessWeightsPad.x, 0.045, 1.0);
    float metallic  = clamp(mr.b * emissiveMetallic.w, 0.0, 1.0);

    vec3 V = safeNormalize(eyePositionWorldPad.xyz - vWorldPos);
    float specularWeight = dielectricFresnel.w * texture(
        specularMap, cnaPbrSpecularTransformUV(cnaPbrUv(5), 0)).a;
    vec3 specularColor = texture(
        specularColorMap, cnaPbrSpecularTransformUV(cnaPbrUv(6), 1)).rgb;
    specularColor = mix(specularColor, cnaSrgbToLinear(specularColor), specularState.y);
    vec3 dielectricF0 = min(dielectricFresnel.xyz * specularColor, vec3(1.0)) * specularWeight;
    vec3 F0 = mix(dielectricF0, albedo, metallic);
    vec3 F90 = mix(vec3(specularWeight), vec3(1.0), metallic);

    vec3 Lo = vec3(0.0);
    Lo += PbrLight(finalNormal, V, safeNormalize(-light0DirPad.xyz), light0DiffusePad.xyz, albedo, F0, F90, roughness, metallic);
    Lo += PbrLight(finalNormal, V, safeNormalize(-light1DirPad.xyz), light1DiffusePad.xyz, albedo, F0, F90, roughness, metallic);
    Lo += PbrLight(finalNormal, V, safeNormalize(-light2DirPad.xyz), light2DiffusePad.xyz, albedo, F0, F90, roughness, metallic);

    float occlusionSample = texture(occlusionMap, cnaPbrTransformUV(cnaPbrUv(4), 4)).r;
    float occlusion = 1.0 + roughnessWeightsPad.w * (occlusionSample - 1.0);
    vec3 ambient = ambientColorPad.xyz * albedo * occlusion;
    vec3 emissiveSample = texture(emissiveMap, cnaPbrTransformUV(cnaPbrUv(3), 3)).rgb;
    emissiveSample = mix(emissiveSample, cnaSrgbToLinear(emissiveSample), eyePositionWorldPad.w);
    vec3 emissive = emissiveMetallic.xyz * emissiveSample;

    vec3 rgb = ambient + Lo + emissive;
    vec3 fogLinear = mix(fogColor.rgb, cnaSrgbToLinear(fogColor.rgb), light0DirPad.w);
    rgb = mix(rgb, fogLinear, vFogFactor);
    rgb = mix(rgb, cnaLinearToSrgb(rgb), light0DirPad.w);

    fragColor = vec4(rgb, alpha);
}
