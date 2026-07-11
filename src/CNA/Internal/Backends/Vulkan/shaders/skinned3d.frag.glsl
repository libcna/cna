#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in float vFogFactor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

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

layout(set = 0, binding = 2) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogStartEnd;      // x = fogStart, y = fogEnd, zw = unused
    // Task 893: DirectionalLight1/DirectionalLight2 diffuse forwarding.
    vec4 light1Dir_pad;
    vec4 light1Diff_pad;
    vec4 light2Dir_pad;
    vec4 light2Diff_pad;
} fog;

void main() {
    vec3  N       = normalize(vNormal);
    float NdotL0  = max(dot(N, -normalize(pc.light0Dir)), 0.0);
    float NdotL1  = max(dot(N, -normalize(fog.light1Dir_pad.xyz)), 0.0);
    float NdotL2  = max(dot(N, -normalize(fog.light2Dir_pad.xyz)), 0.0);
    vec3  lightSum = pc.light0Diffuse * NdotL0
                   + fog.light1Diff_pad.xyz * NdotL1
                   + fog.light2Diff_pad.xyz * NdotL2;
    vec3  litRGB = (pc.ambientColor + lightSum) * pc.diffuseColor.rgb;
    vec4  tex    = (pc.textureEnabled > 0.5) ? texture(uTexture, vUV) : vec4(1.0);
    outColor = vec4(litRGB * tex.rgb, pc.diffuseColor.a * tex.a);
    // Task 899: mix toward FogColor as vFogFactor -> 0 (matches the established Task 888 formula).
    outColor.rgb = mix(fog.fogColorEnabled.xyz, outColor.rgb, vFogFactor);
}
