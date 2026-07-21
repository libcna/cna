#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in float vFogFactor;
layout(location = 3) in vec3 vWorldPos;

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
    vec4 fogVector;      // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
    // Task 893: DirectionalLight1/DirectionalLight2 diffuse forwarding.
    vec4 light1Dir_pad;
    vec4 light1Diff_pad;
    vec4 light2Dir_pad;
    vec4 light2Diff_pad;
    // Task 894: World matrix (unused here, vertex-shader only) + EyePosition + specular.
    mat4 world;
    vec4 eyePos_pad;
    vec4 specularColor_power; // xyz = SpecularColor, w = SpecularPower
    vec4 light0Spec_pad;
    vec4 light1Spec_pad;
    vec4 light2Spec_pad;
} fog;

void main() {
    vec3  N       = normalize(vNormal);
    vec3  E       = normalize(fog.eyePos_pad.xyz - vWorldPos);
    float dotL0   = dot(N, -normalize(pc.light0Dir));       float zeroL0 = step(0.0, dotL0);       float NdotL0 = max(dotL0, 0.0);
    float dotL1   = dot(N, -normalize(fog.light1Dir_pad.xyz)); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2   = dot(N, -normalize(fog.light2Dir_pad.xyz)); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    vec3  lightSum = pc.light0Diffuse * NdotL0
                   + fog.light1Diff_pad.xyz * NdotL1
                   + fog.light2Diff_pad.xyz * NdotL2;
    vec3  litRGB = (pc.ambientColor + lightSum) * pc.diffuseColor.rgb;
    float specularPower = fog.specularColor_power.w;
    vec3  h0 = normalize(E - normalize(pc.light0Dir));       float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, specularPower);
    vec3  h1 = normalize(E - normalize(fog.light1Dir_pad.xyz)); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, specularPower);
    vec3  h2 = normalize(E - normalize(fog.light2Dir_pad.xyz)); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, specularPower);
    vec3  specularRGB = (spec0 * fog.light0Spec_pad.xyz + spec1 * fog.light1Spec_pad.xyz
                        + spec2 * fog.light2Spec_pad.xyz) * fog.specularColor_power.xyz;
    vec4  tex    = (pc.textureEnabled > 0.5) ? texture(uTexture, vUV) : vec4(1.0);
    outColor = vec4(litRGB * tex.rgb, pc.diffuseColor.a * tex.a);
    outColor.rgb += specularRGB * outColor.a;
    // Task 899: mix toward FogColor as vFogFactor -> 0 (matches the established Task 888 formula).
    outColor.rgb = mix(fog.fogColorEnabled.xyz, outColor.rgb, vFogFactor);
}
