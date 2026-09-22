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
    // Depth the way GL clips it: the stored distance is z*0.5+0.5, the GL mapping of a [-w, w]
    // clip range, and the cascade fit relies on GL keeping casters with z in [-w, 0) -- the ones
    // between the light and the near plane, which are exactly the tall buildings between the sun
    // and the street. Vulkan clips to [0, w], so they vanished; (z + w) / 2 keeps GL's range.
    gl_Position = vec4(lightSpace.x, -lightSpace.y, (lightSpace.z + lightSpace.w) * 0.5,
                       lightSpace.w);
    vDistance = lightSpace.z / lightSpace.w * 0.5 + 0.5;
}
