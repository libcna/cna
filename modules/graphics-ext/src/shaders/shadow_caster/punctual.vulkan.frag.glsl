#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform PushConstants
{
    vec2 vpSize;
    mat4 uMatrix;
    vec4 uLightPosition;
    float uLightRange;
} pc;

void main()
{
    float distance = clamp(length(vWorldPos - pc.uLightPosition.xyz) / pc.uLightRange,
                           0.0, 1.0);
    fragColor = vec4(distance, distance, distance, 1.0);
}
