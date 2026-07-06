#version 450

layout(location = 0) in vec3 vWorldNormal;
layout(location = 1) in vec3 vEyeDir;
layout(location = 2) in vec2 vUV;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D   uTexture;
layout(set = 0, binding = 1) uniform samplerCube uEnvMap;

layout(set = 0, binding = 2) uniform EnvMapParams {
    vec4 eyePos_pad;
    vec4 diffuseColor;
    vec4 emissive_em;       // xyz = emissive, w = envMapAmount
    vec4 light0Dir_pad;     // xyz = light dir, w = pad
    vec4 light0Diff_pad;    // xyz = light diffuse, w = pad
    vec4 envMapSpec_pad;    // xyz = envMapSpecular, w = pad
} ep;

void main() {
    vec3 N      = normalize(vWorldNormal);
    vec3 E      = normalize(vEyeDir);
    float NdotL = max(dot(N, -ep.light0Dir_pad.xyz), 0.0);
    vec3 litRGB = (ep.emissive_em.xyz + ep.light0Diff_pad.xyz * NdotL) * ep.diffuseColor.rgb;
    vec4 texColor = texture(uTexture,  vUV);
    vec3 reflDir  = reflect(-E, N);
    vec3 envColor = texture(uEnvMap, reflDir).rgb;
    vec3 baseColor = litRGB * texColor.rgb;
    vec3 rgb = mix(baseColor, envColor, ep.emissive_em.w) + ep.envMapSpec_pad.xyz;
    outColor = vec4(rgb, ep.diffuseColor.a * texColor.a);
}
