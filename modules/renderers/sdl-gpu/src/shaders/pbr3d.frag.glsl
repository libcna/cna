#version 450

layout(location = 0) in vec2  fragUV;
layout(location = 1) in vec3  fragNormal;
layout(location = 2) in vec3  fragTangent;
layout(location = 3) in float fragBitangentSign;
layout(location = 4) in vec3  fragWorldPos;
layout(location = 5) in vec4 fragFog;    // REMED-GFX-009

layout(location = 0) out vec4 outColor;

// PbrEffect's base color texture plus the 4 glTF metallic-roughness maps (normal,
// metallic-roughness [G=roughness,B=metallic], emissive, occlusion). Each aux map falls back to
// a default texture whose sampled value is the correct "map absent" constant for its own
// semantic when the effect leaves it unbound (see SdlGpuRenderer::EnsureDefaultPbrTextures'
// own doc comment) -- mirrors EasyGLRenderer::BindDrawParams()'s identical fallback set.
layout(set = 2, binding = 0) uniform sampler2D uTexture;
layout(set = 2, binding = 1) uniform sampler2D uNormalMap;
layout(set = 2, binding = 2) uniform sampler2D uMetallicRoughnessMap;
layout(set = 2, binding = 3) uniform sampler2D uEmissiveMap;
layout(set = 2, binding = 4) uniform sampler2D uOcclusionMap;

layout(set = 3, binding = 0) uniform PC {
    mat4  mvp;             // vertex-stage only, unused here
    vec4  diffuseColor;    // PbrEffect's base color factor (RGB tint, alpha independent of BRDF)
    vec3  ambientColor;    // PbrEffect's AmbientLightColor
    float lightingEnabled; // unused -- PBR is always "lit"
    vec3  light0Dir;
    float textureEnabled;  // unused -- PbrEffect always samples the base color texture
    vec3  light0Diffuse;
    float vertexColorEnabled; // unused -- PbrEffect has no vertex-color path
} pc;

layout(set = 3, binding = 1) uniform LitLightParams {
    vec4 light1Dir_pad;
    vec4 light1Diffuse_pad;
    vec4 light2Dir_pad;
    vec4 light2Diffuse_pad;
    vec4 emissiveColor_pad;
    mat4 world;              // vertex-stage only, unused here
    vec4 eyePos_pad;
    vec4 light0Specular_pad;  // unused -- PBR has no Blinn-Phong specular slot
    vec4 light1Specular_pad;
    vec4 light2Specular_pad;
    vec4 specularColorPower;  // unused
} lp;

// New tertiary uniform block (PbrEffect-only fields that don't fit the PC/LitLightParams shapes
// every other SDL_GPU 3D shader in this renderer already shares).
layout(set = 3, binding = 2) uniform PbrParams {
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    vec4 alphaTest;
    vec4 srgbFlags; // x=decode base, y=decode emissive, z=encode output
} pbrp;

vec3 cnaSrgbToLinear(vec3 c) {
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

vec3 cnaLinearToSrgb(vec3 c) {
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

// GGX/Trowbridge-Reitz D, Smith-Schlick-GGX visibility (direct-lighting k=(roughness+1)^2/8), and
// Schlick Fresnel -- the glTF 2.0 spec's own reference BRDF (Appendix B.3.3/B.3.4/B.3.2). Mirrors
// EasyGLRenderer::EnsurePbrProgram()'s PbrLight() formula-for-formula.
vec3 PbrLight(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, vec3 F0, float roughness, float metallic) {
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
    vec3 F = F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    vec3 diffuseColor = albedo * (1.0 - metallic);
    vec3 kd = vec3(1.0) - F;
    return (kd * diffuseColor / 3.14159265 + specular) * lightColor * NdotL;
}

// A disabled/never-configured DirectionalLight can forward Direction=(0,0,0) (matches
// lit_textured3d.frag.glsl's own established workaround) -- normalize() on a true zero vector is
// undefined and can poison the whole light sum with NaN. Not present in EasyGLRenderer's
// own PbrLight callers (an OpenGL-driver-specific tolerance this Vulkan-backed renderer does not
// share) -- a deliberate, documented improvement over the reference, not a behavior change for
// any enabled light (NdotL already collapses the whole per-light term to zero when L=0).
vec3 safeNormalize(vec3 v) {
    float len2 = dot(v, v);
    return len2 > 0.0 ? v * inversesqrt(len2) : vec3(0.0);
}

void main() {
    vec4 baseColorTex = texture(uTexture, fragUV);
    vec3 baseColor = mix(baseColorTex.rgb, cnaSrgbToLinear(baseColorTex.rgb), pbrp.srgbFlags.x);
    vec3 albedo = baseColor * pc.diffuseColor.rgb;
    float alpha = baseColorTex.a * pc.diffuseColor.a;
    bool passesAlphaTest = (pbrp.alphaTest.y > 0.0)
        ? (abs(alpha - pbrp.alphaTest.x) < pbrp.alphaTest.y)
        : (alpha < pbrp.alphaTest.x);
    if ((passesAlphaTest ? pbrp.alphaTest.z : pbrp.alphaTest.w) < 0.0) discard;

    vec3 N = normalize(fragNormal);
    vec3 T = normalize(fragTangent - N * dot(N, fragTangent));
    vec3 B = cross(N, T) * fragBitangentSign;
    mat3 TBN = mat3(T, B, N);
    vec3 sampledNormal = texture(uNormalMap, fragUV).rgb * 2.0 - 1.0;
    sampledNormal.xy *= pbrp.normalScale;
    vec3 finalNormal = normalize(TBN * sampledNormal);

    vec4 mr = texture(uMetallicRoughnessMap, fragUV);
    float roughness = clamp(mr.g * pbrp.roughnessFactor, 0.045, 1.0);
    float metallic  = clamp(mr.b * pbrp.metallicFactor, 0.0, 1.0);

    vec3 V = safeNormalize(lp.eyePos_pad.xyz - fragWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);
    Lo += PbrLight(finalNormal, V, safeNormalize(-pc.light0Dir), pc.light0Diffuse, albedo, F0, roughness, metallic);
    Lo += PbrLight(finalNormal, V, safeNormalize(-lp.light1Dir_pad.xyz), lp.light1Diffuse_pad.xyz, albedo, F0, roughness, metallic);
    Lo += PbrLight(finalNormal, V, safeNormalize(-lp.light2Dir_pad.xyz), lp.light2Diffuse_pad.xyz, albedo, F0, roughness, metallic);

    float occlusionSample = texture(uOcclusionMap, fragUV).r;
    float occlusion = 1.0 + pbrp.occlusionStrength * (occlusionSample - 1.0);
    vec3 ambient = pc.ambientColor * albedo * occlusion;
    vec3 emissiveSample = texture(uEmissiveMap, fragUV).rgb;
    emissiveSample = mix(emissiveSample, cnaSrgbToLinear(emissiveSample), pbrp.srgbFlags.y);
    vec3 emissive = lp.emissiveColor_pad.xyz * emissiveSample;

    outColor = vec4(ambient + Lo + emissive, alpha);
    // REMED-GFX-009: blend toward FogColor (RGB only). fragFog.a = keep (1 no fog, 0 full fog).
    vec3 fogLinear = mix(fragFog.rgb, cnaSrgbToLinear(fragFog.rgb), pbrp.srgbFlags.z);
    outColor.rgb = mix(fogLinear, outColor.rgb, fragFog.a);
    outColor.rgb = mix(outColor.rgb, cnaLinearToSrgb(outColor.rgb), pbrp.srgbFlags.z);
}
