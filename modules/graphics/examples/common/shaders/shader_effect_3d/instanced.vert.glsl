#version 450

// One per-vertex input and one per-instance input. The instance stream's location follows the
// per-vertex declaration's element count (one), so it is location 1.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inInstance;

layout(location = 0) out vec4 vProbe;

layout(push_constant) uniform PushConstants
{
    vec2 vpSize;
    mat4 uMatrix;
    vec4 uColor;
    float uFloat0;
} pc;

void main()
{
    gl_Position = pc.uMatrix * vec4(inPosition.xy * 0.5 + inInstance.xy, inPosition.z, 1.0);
    vProbe = vec4(inInstance.z, inInstance.w, 0.0, 1.0);
}
