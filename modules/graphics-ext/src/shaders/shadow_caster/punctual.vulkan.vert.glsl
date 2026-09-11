#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 0) out vec3 vWorldPos;

layout(set = 1, binding = 19, std140) uniform ShadowMatrices
{
    mat4 uLightViewProjection;
    mat4 uWorld;
} matrices;

void main()
{
    vec4 world = matrices.uWorld * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    // Keep the off-screen map in the receiver's XNA projection orientation; a Vulkan back-buffer
    // Y flip here mirrors cube faces and spot casters relative to their lookups.
    gl_Position = matrices.uLightViewProjection * world;
}
