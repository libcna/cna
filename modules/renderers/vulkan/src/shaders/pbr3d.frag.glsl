#version 450

// PbrEffect fragment shader — the glTF 2.0 metallic-roughness BRDF (GGX distribution + Smith-
// Schlick-GGX visibility + Schlick Fresnel), mirroring
// EasyGLRenderer::EnsurePbrProgram()'s fragment stage exactly. 4 additional texture units
// beyond base color: normal map, metallic-roughness map (G=roughness, B=metallic, glTF packing),
// emissive map, occlusion map.

layout(location = 0) in vec3  vNormal;
layout(location = 1) in vec3  vTangent;
layout(location = 2) in float vBitangentSign;
layout(location = 3) in vec2  vUV;
layout(location = 4) in float vFogFactor;
layout(location = 5) in vec3  vWorldPos;
#ifdef CNA_PBR_DUAL_UV
layout(location = 6) in vec2  vUV1;
#endif

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(set = 0, binding = 1) uniform sampler2D uNormalMap;
layout(set = 0, binding = 2) uniform sampler2D uMetallicRoughnessMap;
layout(set = 0, binding = 3) uniform sampler2D uEmissiveMap;
layout(set = 0, binding = 4) uniform sampler2D uOcclusionMap;

layout(push_constant) uniform PC {
    mat4  mvp;
    vec4  diffuseColor;
    vec3  ambientColor;
    float lightingEnabled;
    vec3  light0Dir;
    float textureEnabled;
    vec3  light0Diffuse;
    float vertexColorEnabled;
} pc;

layout(set = 0, binding = 5) uniform PbrParams {
    vec4 light1Dir_pad;
    vec4 light1Diffuse_pad;
    vec4 light2Dir_pad;
    vec4 light2Diffuse_pad;
    mat4 world;
    vec4 eyePos_metallic;
    vec4 emissive_roughness;
    vec4 fogColorEnabled;       // xyz = FogColor, w = WeightsPerVertex (REMED-GFX-010; skinned only)
    vec4 fogVector;             // REMED-GFX-010: FNA fog vector
    vec4 alphaTest;
    vec4 pbrMapScales;          // x = normal scale, y = occlusion strength
    vec4 srgbFlags;             // x = decode base, y = decode emissive, z = encode output
    vec4 dielectricFresnel;     // xyz = dielectric F0, w = dielectric F90
    vec4 textureTransformRows[10]; // two affine rows per PBR texture slot
#ifdef CNA_PBR_DUAL_UV
    vec4 textureCoordinateSets; // x = five-bit per-map TEXCOORD_1 selector mask
#endif
} pbr;

vec3 CnaSrgbToLinear(vec3 c) {
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

vec3 CnaLinearToSrgb(vec3 c) {
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

// GGX/Trowbridge-Reitz D, Smith-Schlick-GGX visibility (direct-lighting k=(roughness+1)^2/8), and
// Schlick Fresnel — the glTF 2.0 spec's own reference BRDF (Appendix B.3.3/B.3.4/B.3.2), byte-for-
// byte identical to EasyGLRenderer::EnsurePbrProgram()'s own PbrLight().
vec3 PbrLight(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, vec3 F0, vec3 F90, float roughness, float metallic) {
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
    vec3 diffuseColor = albedo * (1.0 - metallic);
    vec3 kd = vec3(1.0) - F;
    return (kd * diffuseColor / 3.14159265 + specular) * lightColor * NdotL;
}

vec2 CnaPbrTransformUV(vec2 uv, int slot) {
    vec3 value = vec3(uv, 1.0);
    return vec2(dot(value, pbr.textureTransformRows[slot * 2].xyz),
                dot(value, pbr.textureTransformRows[slot * 2 + 1].xyz));
}

#ifdef CNA_PBR_DUAL_UV
vec2 CnaPbrUV(int slot) {
    int mask = int(pbr.textureCoordinateSets.x + 0.5);
    return (mask & (1 << slot)) != 0 ? vUV1 : vUV;
}
#define CNA_PBR_UV(slot) CnaPbrUV(slot)
#else
#define CNA_PBR_UV(slot) vUV
#endif

void main() {
    vec4 baseColorTex = texture(uTexture, CnaPbrTransformUV(CNA_PBR_UV(0), 0));
    vec3 baseColor = mix(baseColorTex.rgb, CnaSrgbToLinear(baseColorTex.rgb), pbr.srgbFlags.x);
    vec3 albedo = baseColor * pc.diffuseColor.rgb;
    float alpha = baseColorTex.a * pc.diffuseColor.a;
    bool passesAlphaTest = (pbr.alphaTest.y > 0.0)
        ? (abs(alpha - pbr.alphaTest.x) < pbr.alphaTest.y)
        : (alpha < pbr.alphaTest.x);
    if ((passesAlphaTest ? pbr.alphaTest.z : pbr.alphaTest.w) < 0.0) discard;
    vec3 N = normalize(vNormal);
    vec3 T = normalize(vTangent - N * dot(N, vTangent));
    vec3 B = cross(N, T) * vBitangentSign;
    mat3 TBN = mat3(T, B, N);
    vec3 sampledNormal = texture(uNormalMap, CnaPbrTransformUV(CNA_PBR_UV(1), 1)).rgb * 2.0 - 1.0;
    sampledNormal.xy *= pbr.pbrMapScales.x;
    vec3 finalNormal = normalize(TBN * sampledNormal);
    vec4 mr = texture(uMetallicRoughnessMap, CnaPbrTransformUV(CNA_PBR_UV(2), 2));
    float roughness = clamp(mr.g * pbr.emissive_roughness.w, 0.045, 1.0);
    float metallic  = clamp(mr.b * pbr.eyePos_metallic.w, 0.0, 1.0);
    vec3 V = normalize(pbr.eyePos_metallic.xyz - vWorldPos);
    vec3 F0 = mix(pbr.dielectricFresnel.xyz, albedo, metallic);
    vec3 F90 = mix(vec3(pbr.dielectricFresnel.w), vec3(1.0), metallic);
    vec3 Lo = vec3(0.0);
    Lo += PbrLight(finalNormal, V, normalize(-pc.light0Dir), pc.light0Diffuse, albedo, F0, F90, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-pbr.light1Dir_pad.xyz), pbr.light1Diffuse_pad.xyz, albedo, F0, F90, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-pbr.light2Dir_pad.xyz), pbr.light2Diffuse_pad.xyz, albedo, F0, F90, roughness, metallic);
    float occlusionSample = texture(uOcclusionMap, CnaPbrTransformUV(CNA_PBR_UV(4), 4)).r;
    float occlusion = 1.0 + pbr.pbrMapScales.y * (occlusionSample - 1.0);
    vec3 ambient = pc.ambientColor * albedo * occlusion;
    vec3 emissiveSample = texture(uEmissiveMap, CnaPbrTransformUV(CNA_PBR_UV(3), 3)).rgb;
    emissiveSample = mix(emissiveSample, CnaSrgbToLinear(emissiveSample), pbr.srgbFlags.y);
    vec3 emissive = pbr.emissive_roughness.xyz * emissiveSample;
    outColor = vec4(ambient + Lo + emissive, alpha);
    vec3 fogLinear = mix(pbr.fogColorEnabled.xyz,
                         CnaSrgbToLinear(pbr.fogColorEnabled.xyz), pbr.srgbFlags.z);
    outColor.rgb = mix(fogLinear, outColor.rgb, vFogFactor);
    outColor.rgb = mix(outColor.rgb, CnaLinearToSrgb(outColor.rgb), pbr.srgbFlags.z);
}
