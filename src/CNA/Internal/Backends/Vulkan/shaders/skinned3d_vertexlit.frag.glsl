#version 450

// Task 1103: per-vertex-lit sibling of skinned3d.frag.glsl — see the vertex shader's own header
// comment. Non-lighting math (texture sample, fog) is structurally identical to
// skinned3d.frag.glsl; only the SOURCE of the lit/specular RGB differs.

layout(location = 0) in vec2  vUV;
layout(location = 1) in float vFogFactor;
layout(location = 2) in vec3  vLitRGB;
layout(location = 3) in vec3  vSpecularRGB;

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

// Same UBO as the vertex shader (set=0, binding=2) — only fogColorEnabled.xyz is read here.
layout(set = 0, binding = 2) uniform FogParams {
    vec4 fogColorEnabled;
    vec4 fogStartEnd;
    vec4 light1Dir_pad;
    vec4 light1Diff_pad;
    vec4 light2Dir_pad;
    vec4 light2Diff_pad;
    mat4 world;
    vec4 eyePos_pad;
    vec4 specularColor_power;
    vec4 light0Spec_pad;
    vec4 light1Spec_pad;
    vec4 light2Spec_pad;
} fog;

void main() {
    vec4 tex = (pc.textureEnabled > 0.5) ? texture(uTexture, vUV) : vec4(1.0);
    outColor = vec4(vLitRGB * tex.rgb, pc.diffuseColor.a * tex.a);
    outColor.rgb += vSpecularRGB * outColor.a;
    outColor.rgb = mix(fog.fogColorEnabled.xyz, outColor.rgb, vFogFactor);
}
