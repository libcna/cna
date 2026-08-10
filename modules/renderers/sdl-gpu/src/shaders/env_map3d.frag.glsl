#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec4 fragTint;
layout(location = 4) in vec4 fragFog;    // REMED-GFX-009

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D   uTexture;
layout(set = 2, binding = 1) uniform samplerCube uEnvMap;

layout(set = 3, binding = 0) uniform PC {
    mat4 mvp;             // vertex-stage only, unused here
    vec4 diffuseColor;    // unused here directly -- fragTint already carries it
    vec4 emissiveAmount;  // xyz = emissive+ambient*diffuse, w = envMapAmount
} pc;

layout(set = 3, binding = 1) uniform EnvMapParams {
    mat4 world;           // vertex-stage only, unused here
    vec4 eyePos_fresnelEnabled;
    vec4 light0Dir_fresnelFactor;
    vec4 light0Diffuse_pad;
    vec4 light1Dir_pad;
    vec4 light1Diffuse_pad;
    vec4 light2Dir_pad;
    vec4 light2Diffuse_pad;
    vec4 envMapSpecular_pad;
} ep;

// A disabled/never-configured DirectionalLight can forward Direction=(0,0,0) (matches
// lit_textured3d.frag.glsl's own established workaround) -- normalize() on a true zero vector is
// undefined and can poison the whole light sum with NaN.
vec3 safeNormalize(vec3 v) {
    float len2 = dot(v, v);
    return len2 > 0.0 ? v * inversesqrt(len2) : vec3(0.0);
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 E = safeNormalize(ep.eyePos_fresnelEnabled.xyz - fragWorldPos);

    vec3 nL0 = safeNormalize(ep.light0Dir_fresnelFactor.xyz);
    vec3 nL1 = safeNormalize(ep.light1Dir_pad.xyz);
    vec3 nL2 = safeNormalize(ep.light2Dir_pad.xyz);
    float NdotL0 = max(dot(N, -nL0), 0.0);
    float NdotL1 = max(dot(N, -nL1), 0.0);
    float NdotL2 = max(dot(N, -nL2), 0.0);
    vec3 lightSum = ep.light0Diffuse_pad.xyz * NdotL0
                  + ep.light1Diffuse_pad.xyz * NdotL1
                  + ep.light2Diffuse_pad.xyz * NdotL2;

    // REMED-GFX-007: FNA Lighting.fxh adds emissive UNSCALED (litRGB = lightSum*Diffuse +
    // Emissive), not (Emissive + lightSum)*Diffuse -- the latter re-scales the already
    // ambient-folded emissive by DiffuseColor a second time. Because the XNA layer pre-folds Alpha
    // into BOTH fragTint.rgb (== DiffuseColor*Alpha) and pc.emissiveAmount.xyz
    // (== (Emissive + Ambient*Diffuse)*Alpha), the wrong form additionally squared Alpha; adding
    // emissive unscaled removes that too.
    vec3 litRGB = lightSum * fragTint.rgb + pc.emissiveAmount.xyz;
    vec4 texColor = texture(uTexture, fragUV);
    vec3 reflDir = reflect(-E, N);
    vec4 envSample = texture(uEnvMap, reflDir);
    vec3 baseColor = litRGB * texColor.rgb;
    float combinedAlpha = fragTint.a * texColor.a;

    // Fresnel-weighted blend factor (matches VulkanGraphicsBackend's env_map3d.frag.glsl and
    // FNA's real PSEnvMap/PSEnvMapSpecular): a flat envMapAmount, or edge-weighted by view angle
    // when FresnelEnabled.
    float viewAngle = dot(E, N);
    float blendFactor = (ep.eyePos_fresnelEnabled.w > 0.5)
        ? pow(max(1.0 - abs(viewAngle), 0.0), ep.light0Dir_fresnelFactor.w) * pc.emissiveAmount.w
        : pc.emissiveAmount.w;

    // FNA's real PSEnvMap/PSEnvMapSpecular scale the whole envmap sample (both the base lerp
    // target and the specular term) by combinedAlpha before use.
    vec3 rgb = mix(baseColor, envSample.rgb * combinedAlpha, blendFactor)
             + ep.envMapSpecular_pad.xyz * envSample.a * combinedAlpha;
    outColor = vec4(rgb, combinedAlpha);
    // REMED-GFX-009: blend toward FogColor (RGB only). fragFog.a = keep (1 no fog, 0 full fog).
    outColor.rgb = mix(fragFog.rgb, outColor.rgb, fragFog.a);
}
