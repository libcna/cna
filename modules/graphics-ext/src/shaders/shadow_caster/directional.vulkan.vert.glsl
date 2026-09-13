#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 0) out float vDistance;

layout(set = 1, binding = 19, std140) uniform ShadowMatrices
{
    mat4 uLightViewProjection;
    mat4 uWorld;
} matrices;

void main()
{
    vec4 lightSpace = matrices.uLightViewProjection * matrices.uWorld
                    * vec4(aPosition, 1.0);
    // This off-screen map is sampled through the same XNA light projection. Flipping Y here as a
    // back-buffer shader would mirror the stored caster relative to the receiver lookup.
    gl_Position = lightSpace;
    vDistance = lightSpace.z / lightSpace.w * 0.5 + 0.5;
}
