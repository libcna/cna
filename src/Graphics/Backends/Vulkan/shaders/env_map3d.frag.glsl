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
    vec4 fogVector;          // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
    // Task 890: DirectionalLight1/DirectionalLight2 diffuse forwarding.
    vec4 light1Dir_pad;
    vec4 light1Diff_pad;
    vec4 light2Dir_pad;
    vec4 light2Diff_pad;
} ep;

void main() {
    vec3 N       = normalize(vWorldNormal);
    vec3 E       = normalize(vEyeDir);
    float NdotL0 = max(dot(N, -ep.light0Dir_pad.xyz), 0.0);
    float NdotL1 = max(dot(N, -ep.light1Dir_pad.xyz), 0.0);
    float NdotL2 = max(dot(N, -ep.light2Dir_pad.xyz), 0.0);
    vec3 lightSum = ep.light0Diff_fresnelEn.xyz * NdotL0
                  + ep.light1Diff_pad.xyz * NdotL1
                  + ep.light2Diff_pad.xyz * NdotL2;
    // REMED-GFX-007: FNA Lighting.fxh adds emissive UNSCALED (litRGB = lightSum*Diffuse +
    // Emissive), not (Emissive + lightSum)*Diffuse -- the latter re-scales the already
    // ambient-folded emissive by DiffuseColor a second time. emissive_em.xyz is the CPU-side
    // pre-folded (EmissiveColor + AmbientLightColor*DiffuseColor)*alpha (EnvironmentMapEffect.cpp).
    vec3 litRGB = lightSum * ep.diffuseColor.rgb + ep.emissive_em.xyz;
    vec4 texColor  = texture(uTexture,  vUV);
    vec3 reflDir   = reflect(-E, N);
    vec4 envSample = texture(uEnvMap, reflDir);
    vec3 baseColor = litRGB * texColor.rgb;
    float combinedAlpha = ep.diffuseColor.a * texColor.a;
    float viewAngle = dot(E, N);
    float blendFactor = (ep.light0Diff_fresnelEn.w > 0.5)
        ? pow(max(1.0 - abs(viewAngle), 0.0), ep.envMapSpec_fresnelF.w) * ep.emissive_em.w
        : ep.emissive_em.w;
    // Task 891: FNA's real PSEnvMap/PSEnvMapSpecular scale the whole `envmap` sample (both
    // the base lerp target and the specular term, the latter already fixed by Task 395) by
    // combinedAlpha before use -- the base lerp's envSample.rgb was still unscaled here.
    vec3 rgb = mix(baseColor, envSample.rgb * combinedAlpha, blendFactor) + ep.envMapSpec_fresnelF.xyz * envSample.a * combinedAlpha;
    // Mix toward FogColor as vFogFactor -> 0 (matches the established Task 888 formula).
    rgb = mix(ep.fogColorEnabled.xyz, rgb, vFogFactor);
    outColor = vec4(rgb, combinedAlpha);
}
