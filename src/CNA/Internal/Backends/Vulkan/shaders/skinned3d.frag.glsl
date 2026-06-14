#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;

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

void main() {
    vec3  N      = normalize(vNormal);
    float NdotL  = max(dot(N, -normalize(pc.light0Dir)), 0.0);
    vec3  litRGB = (pc.ambientColor + pc.light0Diffuse * NdotL) * pc.diffuseColor.rgb;
    vec4  tex    = (pc.textureEnabled > 0.5) ? texture(uTexture, vUV) : vec4(1.0);
    outColor = vec4(litRGB * tex.rgb, pc.diffuseColor.a * tex.a);
}
