#version 450

layout(location = 0) in vec3 vWorldNormal;
layout(location = 1) in vec3 vEyeDir;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vFogFactor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D   uTexture;
layout(set = 0, binding = 1) uniform samplerCube uEnvMap;

layout(set = 0, binding = 2) uniform EnvMapParams {
    vec4 eyePos_pad;
    vec4 diffuseColor;
    vec4 emissive_em;         // xyz = emissive, w = envMapAmount
    vec4 light0Dir_pad;       // xyz = light dir, w = pad
    vec4 light0Diff_fresnelEn; // xyz = light diffuse, w = fresnelEnabled (0/1)
    vec4 envMapSpec_fresnelF;  // xyz = envMapSpecular, w = fresnelFactor
    // Task 899's noted cheap leftover: fog packed into this UBO's spare tail bytes.
    vec4 fogColorEnabled;      // xyz = FogColor, w = fogEnabled
    vec4 fogStartEnd;          // x = fogStart, y = fogEnd, zw = unused
} ep;

void main() {
    vec3 N      = normalize(vWorldNormal);
    vec3 E      = normalize(vEyeDir);
    float NdotL = max(dot(N, -ep.light0Dir_pad.xyz), 0.0);
    vec3 litRGB = (ep.emissive_em.xyz + ep.light0Diff_fresnelEn.xyz * NdotL) * ep.diffuseColor.rgb;
    vec4 texColor  = texture(uTexture,  vUV);
    vec3 reflDir   = reflect(-E, N);
    vec4 envSample = texture(uEnvMap, reflDir);
    vec3 baseColor = litRGB * texColor.rgb;
    float combinedAlpha = ep.diffuseColor.a * texColor.a;
    float viewAngle = dot(E, N);
    float blendFactor = (ep.light0Diff_fresnelEn.w > 0.5)
        ? pow(max(1.0 - abs(viewAngle), 0.0), ep.envMapSpec_fresnelF.w) * ep.emissive_em.w
        : ep.emissive_em.w;
    vec3 rgb = mix(baseColor, envSample.rgb, blendFactor) + ep.envMapSpec_fresnelF.xyz * envSample.a * combinedAlpha;
    // Mix toward FogColor as vFogFactor -> 0 (matches the established Task 888 formula).
    rgb = mix(ep.fogColorEnabled.xyz, rgb, vFogFactor);
    outColor = vec4(rgb, combinedAlpha);
}
