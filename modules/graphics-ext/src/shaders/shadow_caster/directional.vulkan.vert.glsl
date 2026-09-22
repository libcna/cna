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
    // plans/plan_street.md STREET-0007: flipped into Vulkan's clip space like every other 3D
    // program of this renderer. Without it the stored map matched the receiver's lookup but the
    // winding was mirrored against the pipelines' clockwise front face, so a caster drawn with
    // CullCounterClockwise lost the faces that face the light -- every roof and the ground -- and
    // kept the ones that face away. The receiver (shadow_sampling.glsl) reads the map top-down to
    // match.
    gl_Position = vec4(lightSpace.x, -lightSpace.y, lightSpace.z, lightSpace.w);
    vDistance = lightSpace.z / lightSpace.w * 0.5 + 0.5;
}
