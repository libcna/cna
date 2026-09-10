#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

layout(set = 1, binding = 15, std140) uniform Mat4Array
{
    mat4 uClusterMatrices[72];
};

layout(location = 0) out vec3 vWorldPosition;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec4 vClipPosition;
layout(location = 3) out float vViewDistance;

void main()
{
    vec4 world = uClusterMatrices[0] * vec4(aPosition, 1.0);
    vec4 view = uClusterMatrices[1] * world;
    gl_Position = uClusterMatrices[2] * view;
    vWorldPosition = world.xyz;
    vWorldNormal = mat3(uClusterMatrices[0]) * aNormal;
    vClipPosition = gl_Position;
    vViewDistance = -view.z;
}
