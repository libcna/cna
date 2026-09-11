#version 450

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform PushConstants
{
    vec2 vpSize;
    mat4 uMatrix;
    vec4 uColor;
    float uFloat0;
} pc;

void main()
{
    fragColor = texture(uTex, vUV) * vColor * pc.uColor;
}
