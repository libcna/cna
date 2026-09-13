#version 450

layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConstants
{
    vec2 viewportSize;
    mat4 uMatrix;
    vec4 uEffectParams;
} pc;

void main()
{
    FragColor = pc.uEffectParams;
}
