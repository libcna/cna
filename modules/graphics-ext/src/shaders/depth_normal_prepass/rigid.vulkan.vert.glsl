#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

layout(set = 1, binding = 12, std140) uniform FloatArray
{
    float uPrepassScalars[72];
};

// Slot zero stays reserved for the light/face matrix used by the shadow-caster packages.
layout(set = 1, binding = 19, std140) uniform EngineMatrices
{
    mat4 uReservedLightViewProjection;
    mat4 uWorld;
    mat4 uView;
    mat4 uProjection;
    mat4 uPreviousWorld;
    mat4 uPreviousViewProjection;
};

layout(location = 0) out vec3 vViewNormal;
layout(location = 1) out float vViewDepth;
layout(location = 2) out vec4 vCurrentClip;
layout(location = 3) out vec4 vPreviousClip;

void main()
{
    vec4 world = uWorld * vec4(aPosition, 1.0);
    vec4 view = uView * world;
    gl_Position = uProjection * view;
    vCurrentClip = gl_Position;
    // plans/plan_street.md STREET-0006: the Vulkan renderer's convention for every 3D vertex
    // program (pbr3d.vert.glsl, REMED-GFX-011) -- D3D-style clip space into Vulkan's, whose Y
    // points down. Without it the prepass images were the scene mirrored top to bottom, so SSAO
    // and every depth consumer read the wrong texel, and the mirrored winding culled front faces.
    // After vCurrentClip, which stays in the convention the velocity output is defined in.
    gl_Position.y = -gl_Position.y;
    vPreviousClip = uPreviousViewProjection * (uPreviousWorld * vec4(aPosition, 1.0));
    vViewNormal = normalize(mat3(uView) * mat3(uWorld) * aNormal);
    vViewDepth = clamp(-view.z / uPrepassScalars[0], 0.0, 1.0);
}
