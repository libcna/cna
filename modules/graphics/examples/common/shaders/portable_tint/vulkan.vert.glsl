#version 450

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

layout(push_constant) uniform PushConstants
{
    vec2 vpSize;
    mat4 uMatrix;
    vec4 uColor;
    float uFloat0;
} pc;

void main()
{
    vec2 ndc = (aPos / pc.vpSize) * 2.0 - vec2(1.0);
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
